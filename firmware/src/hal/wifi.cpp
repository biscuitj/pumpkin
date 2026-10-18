#include "wifi.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wpa2.h>

#include "core/sync.h"
#include "storage.h"

namespace hal {

namespace {

using pk::Locked;
using pk::TrialState;
using pk::WifiJoinRequest;

// Enterprise sign-in adds a RADIUS round trip on top of the 4-way handshake;
// 15 s gave false failures.
constexpr uint32_t kConnectTimeoutMs = 25000;
constexpr uint32_t kRetryEveryMs = 30000;
// How long joining may keep failing before the setup hotspot comes up. The
// clock keeps retrying underneath: the hotspot is extra, never instead.
constexpr uint32_t kSetupApAfterMs = 120000;
// After a successful join the hotspot stays a little longer, so the phone on
// it can still read the result.
constexpr uint32_t kApGraceMs = 10000;
constexpr int kScanMax = 24;
constexpr size_t kCaMax = 4096;

enum class State : uint8_t { Idle, Connecting, Connected, RetryWait };

pk::Mutex lock;  // guards everything below that the web server reads

State state = State::Idle;
uint32_t attemptAt = 0, retryAt = 0, failingSince = 0;
bool bootConnectPending = false;

bool apUp = false;
uint32_t apDownAt = 0;
char setupSsid[33] = "Pumpkin-Setup";
uint32_t apIp = 0;
int apStations = 0;
DNSServer dns;

char ip[16] = "";
int rssi = 0;
pk::WifiSecurity linkSecurity = pk::WifiSecurity::Personal;
char linkSsid[33] = "";

// Set from the Wi-Fi event task.
volatile int lastReason = 0;
volatile uint32_t disconnects = 0;
volatile bool associated = false;

// Trial
WifiJoinRequest pending{}, trial{};
bool trialRequested = false, trialActive = false;
uint32_t trialAt = 0;
TrialState trialState = TrialState::Idle;
char trialDetail[96] = "";
bool joinedReady = false;
pk::WifiSettings joined{};

// Scan cache
pk::ScanEntry scan[kScanMax];
int scanCount = 0;
bool scanRunning = false, scanRequested = false;
uint32_t scanDoneAt = 0;

bool isEnterprise(uint8_t mode) {
    if (mode == WIFI_AUTH_WPA2_ENTERPRISE) return true;
#ifdef WIFI_AUTH_WPA3_ENT_192
    if (mode == WIFI_AUTH_WPA3_ENT_192) return true;
#endif
    return false;
}

// "No such network" and "wrong password" have different fixes; say which.
TrialState classify(int reason) {
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND: return TrialState::FailedNoAp;
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_802_1X_AUTH_FAILED:
        case WIFI_REASON_IE_IN_4WAY_DIFFERS: return TrialState::FailedAuth;
        default: return TrialState::FailedTimeout;
    }
}

void onEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
        associated = true;
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        lastReason = info.wifi_sta_disconnected.reason;
        disconnects = disconnects + 1;
        associated = false;
    }
}

void fromSaved(const pk::WifiSettings& s, WifiJoinRequest& r) {
    memset(&r, 0, sizeof(r));
    pk::copyStr(r.ssid, s.ssid);
    r.security = s.security;
    r.eap = s.eap;
    pk::copyStr(r.password, s.password);
    pk::copyStr(r.identity, s.identity);
    pk::copyStr(r.anonIdentity, s.anonIdentity);
    r.validateCa = s.validateCa;
}

void applyEnterprise(const WifiJoinRequest& r) {
    // A blank outer identity means "the real user name", like a phone does.
    const char* outer = r.anonIdentity[0] ? r.anonIdentity : r.identity;
    esp_wifi_sta_wpa2_ent_set_identity((const uint8_t*)outer, strlen(outer));
    esp_wifi_sta_wpa2_ent_set_username((const uint8_t*)r.identity, strlen(r.identity));
    esp_wifi_sta_wpa2_ent_set_password((const uint8_t*)r.password, strlen(r.password));
    // PEAP negotiates MSCHAPv2 by itself; TTLS needs to be told.
    if (r.eap == pk::EapMethod::Ttls) esp_wifi_sta_wpa2_ent_set_ttls_phase2_method(ESP_EAP_TTLS_PHASE2_MSCHAPV2);

    static char ca[kCaMax + 1];
    const size_t n = r.validateCa ? caRead(ca, sizeof(ca)) : 0;
    if (n) {
        esp_wifi_sta_wpa2_ent_set_ca_cert((const uint8_t*)ca, n + 1);  // the NUL counts
    } else {
        // No validation: the same trust decision as tapping "Trust" on a phone.
        esp_wifi_sta_wpa2_ent_clear_ca_cert();
    }
    // No real time before NTP, so certificate dates can't be checked.
    esp_wifi_sta_wpa2_ent_set_disable_time_check(true);
    esp_wifi_sta_wpa2_ent_enable();
    Serial.printf("[wifi] Enterprise %s as '%s' (%s)\n", r.eap == pk::EapMethod::Ttls ? "TTLS" : "PEAP", r.identity,
                  n ? "server checked" : "server trusted");
}

void startAttempt(const WifiJoinRequest& r) {
    disconnects = 0;
    lastReason = 0;
    associated = false;
    WiFi.disconnect(false);
    if (r.security == pk::WifiSecurity::Enterprise) {
        applyEnterprise(r);
        WiFi.begin(r.ssid);
    } else {
        // Stale EAP state would break a later personal network.
        esp_wifi_sta_wpa2_ent_disable();
        WiFi.begin(r.ssid, r.password[0] ? r.password : nullptr);
    }
    attemptAt = millis();
    Locked l(lock);
    state = State::Connecting;
    pk::copyStr(linkSsid, r.ssid);
    linkSecurity = r.security;
}

void apStart() {
    if (apUp) return;
    // Station and hotspot together: the station keeps retrying and scans need it.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(setupSsid);  // open on purpose: nothing to type before the page
    const IPAddress a = WiFi.softAPIP();
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", a);
    Locked l(lock);
    apUp = true;
    apDownAt = 0;
    apIp = (uint32_t)a;
    Serial.printf("[wifi] Setup hotspot %s on %s\n", setupSsid, a.toString().c_str());
}

void apStop() {
    if (!apUp) return;
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    Locked l(lock);
    apUp = false;
    apDownAt = 0;
    apIp = 0;
    apStations = 0;
    Serial.println("[wifi] Setup hotspot off");
}

bool linkUp() { return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress((uint32_t)0); }

void setIp() {
    Locked l(lock);
    pk::copyStr(ip, WiFi.localIP().toString().c_str());
}

bool beginScan() {
    if (scanRunning) return false;
    if (WiFi.getMode() == WIFI_MODE_NULL) WiFi.mode(WIFI_STA);
    if (WiFi.scanNetworks(true, true) == WIFI_SCAN_FAILED) return false;
    scanRunning = true;
    return true;
}

// One row per network name, strongest first.
void collectScan(int n) {
    pk::ScanEntry found[kScanMax];
    int count = 0;
    for (int i = 0; i < n; ++i) {
        const String name = WiFi.SSID(i);
        if (!name.length()) continue;
        const int8_t r = (int8_t)WiFi.RSSI(i);
        const uint8_t enc = (uint8_t)WiFi.encryptionType(i);
        int at = -1;
        for (int j = 0; j < count; ++j)
            if (name.equals(found[j].ssid)) at = j;
        if (at < 0) {
            if (count >= kScanMax) continue;
            at = count++;
            pk::copyStr(found[at].ssid, name.c_str());
        } else if (r <= found[at].rssi) {
            continue;
        }
        pk::ScanEntry& e = found[at];
        e.rssi = r;
        e.secure = enc != WIFI_AUTH_OPEN;
        e.enterprise = isEnterprise(enc);
    }
    for (int i = 1; i < count; ++i) {
        const pk::ScanEntry key = found[i];
        int j = i - 1;
        while (j >= 0 && found[j].rssi < key.rssi) {
            found[j + 1] = found[j];
            --j;
        }
        found[j + 1] = key;
    }
    WiFi.scanDelete();
    Locked l(lock);
    memcpy(scan, found, sizeof(pk::ScanEntry) * count);
    scanCount = count;
    scanRunning = false;
    scanDoneAt = millis();
}

void trialBegin() {
    if (scanRunning) {  // a join cancels the scan in the driver anyway
        WiFi.scanDelete();
        scanRunning = false;
    }
    {
        Locked l(lock);
        trial = pending;
        trialRequested = false;
        trialActive = true;
        trialAt = millis();
        trialState = TrialState::Associating;
        trialDetail[0] = '\0';
    }
    Serial.printf("[wifi] Trying '%s'\n", trial.ssid);
    startAttempt(trial);
}

void trialFail(TrialState st, int reason, const pk::WifiSettings& saved) {
    {
        Locked l(lock);
        trialState = st;
        trialActive = false;
        if (st == TrialState::FailedNoAp)
            snprintf(trialDetail, sizeof(trialDetail), "No network called '%s' here (2.4 GHz only).", trial.ssid);
        else if (st == TrialState::FailedAuth)
            snprintf(trialDetail, sizeof(trialDetail), "The network turned down the sign-in (reason %d).", reason);
        else
            snprintf(trialDetail, sizeof(trialDetail), "No answer after %u seconds (reason %d).",
                     (unsigned)(kConnectTimeoutMs / 1000), reason);
    }
    Serial.printf("[wifi] Trial failed: %s\n", trialDetail);
    // Back to the saved network, if any. With the hotspot up, wait a retry
    // interval first so the phone can read the failure.
    if (saved.ssid[0] && !apUp) {
        WifiJoinRequest r;
        fromSaved(saved, r);
        startAttempt(r);
    } else {
        WiFi.disconnect(false);
        Locked l(lock);
        state = saved.ssid[0] ? State::RetryWait : State::Idle;
        retryAt = millis();
    }
}

void trialLoop(const pk::WifiSettings& saved) {
    const uint32_t elapsed = millis() - trialAt;
    if (linkUp()) {
        setIp();
        {
            Locked l(lock);
            trialState = TrialState::Connected;
            trialActive = false;
            state = State::Connected;
            failingSince = 0;
            snprintf(trialDetail, sizeof(trialDetail), "Connected as %s.", ip);
            // Only now do the credentials get kept.
            joined = pk::WifiSettings{};
            pk::copyStr(joined.ssid, trial.ssid);
            joined.security = trial.security;
            joined.eap = trial.eap;
            pk::copyStr(joined.password, trial.password);
            if (trial.security == pk::WifiSecurity::Enterprise) {
                pk::copyStr(joined.identity, trial.identity);
                pk::copyStr(joined.anonIdentity, trial.anonIdentity);
            }
            joined.validateCa = trial.validateCa;
            joinedReady = true;
            if (apUp) apDownAt = millis() + kApGraceMs;
        }
        Serial.printf("[wifi] Trial worked, %s\n", ip);
        return;
    }
    if (associated && trialState == TrialState::Associating) {
        Locked l(lock);
        trialState = TrialState::Authenticating;  // link is up, waiting for an address
    }
    const int reason = lastReason;
    if (reason != 0) {
        const TrialState cls = classify(reason);
        // The supplicant retries by itself; wait for a pattern, not one event.
        if (cls != TrialState::FailedTimeout && (disconnects >= 3 || elapsed > 12000)) return trialFail(cls, reason, saved);
    }
    if (elapsed > kConnectTimeoutMs) trialFail(reason ? classify(reason) : TrialState::FailedTimeout, reason, saved);
}

} // namespace

void wifiBegin(const pk::WifiSettings& saved, const char* ssid, const char* hostname) {
    pk::copyStr(setupSsid, ssid);
    WiFi.onEvent(onEvent);
    WiFi.persistent(false);
    // The loop below owns retry timing; auto-reconnect would retune the radio
    // under a phone that's on the setup hotspot.
    WiFi.setAutoReconnect(false);
    WiFi.setHostname(hostname);  // the router's client list (DHCP)

    failingSince = millis();
    if (!saved.ssid[0]) {
        Serial.println("[wifi] No network saved: setup hotspot");
        apStart();
        beginScan();
        return;
    }
    WiFi.mode(WIFI_STA);
    // One scan at boot fills the list in case setup is needed; the join starts
    // when it's done.
    bootConnectPending = beginScan();
    if (!bootConnectPending) {
        WifiJoinRequest r;
        fromSaved(saved, r);
        startAttempt(r);
    }
}

void wifiLoop(const pk::WifiSettings& saved) {
    const uint32_t now = millis();
    if (apUp) dns.processNextRequest();

    static uint32_t statsAt = 0;
    if (now - statsAt > 1000) {
        statsAt = now;
        const int r = linkUp() ? WiFi.RSSI() : 0;
        const int st = apUp ? (int)WiFi.softAPgetStationNum() : 0;
        Locked l(lock);
        rssi = r;
        apStations = st;
    }

    bool startScan;
    {
        Locked l(lock);
        startScan = scanRequested;
        scanRequested = false;
    }
    if (startScan && !trialActive) beginScan();
    if (scanRunning) {
        const int rc = WiFi.scanComplete();
        if (rc >= 0 || rc == WIFI_SCAN_FAILED) {
            if (rc >= 0) collectScan(rc);
            else scanRunning = false;
            if (bootConnectPending && saved.ssid[0]) {
                bootConnectPending = false;
                WifiJoinRequest r;
                fromSaved(saved, r);
                startAttempt(r);
            }
        }
    }

    if (apUp && apDownAt && (int32_t)(now - apDownAt) >= 0) apStop();

    bool requested;
    {
        Locked l(lock);
        requested = trialRequested;
    }
    if (requested && !trialActive) return trialBegin();
    if (trialActive) return trialLoop(saved);
    if (bootConnectPending) return;

    if (linkUp()) {
        if (state != State::Connected) {
            setIp();
            Serial.printf("[wifi] Connected to '%s', %s, %d dBm\n", linkSsid, ip, WiFi.RSSI());
            Locked l(lock);
            state = State::Connected;
            failingSince = 0;
            if (apUp && !apDownAt) apDownAt = now + kApGraceMs;
        }
        return;
    }

    if (state == State::Connected) {
        Serial.println("[wifi] Connection lost");
        Locked l(lock);
        state = State::RetryWait;
        retryAt = now;
        failingSince = now;
        ip[0] = '\0';
        if (trialState == TrialState::Connected) trialState = TrialState::Idle;
    }

    if (!saved.ssid[0]) {
        apStart();  // nothing to retry: setup is the only way on
        return;
    }
    if (!failingSince) failingSince = now;
    if (!apUp && now - failingSince > kSetupApAfterMs) {
        Serial.println("[wifi] Two minutes without Wi-Fi: setup hotspot");
        apStart();
    }

    // A join retunes the shared radio and drops the phone on the hotspot:
    // stop and don't retry while someone is on it.
    const bool phoneOnAp = apUp && apStations > 0;
    if (state == State::Connecting) {
        if (phoneOnAp || now - attemptAt > kConnectTimeoutMs) {
            WiFi.disconnect(false);
            Locked l(lock);
            state = State::RetryWait;
            retryAt = now;
        }
        return;
    }
    if (phoneOnAp) return;
    if (now - retryAt > kRetryEveryMs) {
        WifiJoinRequest r;
        fromSaved(saved, r);
        Serial.printf("[wifi] Retrying '%s'\n", r.ssid);
        startAttempt(r);
    }
}

bool wifiConnected() {
    Locked l(lock);
    return state == State::Connected;
}

bool wifiApUp() {
    Locked l(lock);
    return apUp;
}

bool wifiOnAp(uint32_t localIp) {
    Locked l(lock);
    return apUp && apIp != 0 && localIp == apIp;
}

void wifiView(pk::WifiView& out) {
    Locked l(lock);
    out = pk::WifiView{};
    const bool joining = trialActive || state == State::Connecting;
    if (state == State::Connected) out.mode = pk::WifiMode::Connected;
    else if (apUp) out.mode = pk::WifiMode::SetupAp;
    else if (joining) out.mode = pk::WifiMode::Connecting;
    else out.mode = pk::WifiMode::Disconnected;

    if (state == State::Connected || joining) {
        pk::copyStr(out.ssid, linkSsid);
        out.hasSecurity = true;
        out.security = linkSecurity;
    }
    if (state == State::Connected) {
        out.hasRssi = true;
        out.rssi = rssi;
        pk::copyStr(out.ip, ip);
    }
    out.ap = apUp;
    if (apUp) {
        pk::copyStr(out.apSsid, setupSsid);
        pk::copyStr(out.apIp, IPAddress(apIp).toString().c_str());
        out.apStations = apStations;
    }
    out.trial = trialState;
    pk::copyStr(out.trialSsid, trialActive || trialState != TrialState::Idle ? trial.ssid : "");
    pk::copyStr(out.trialDetail, trialDetail);
}

int wifiScan(pk::ScanEntry* out, int max, bool& scanning, int32_t& ageSec) {
    Locked l(lock);
    scanning = scanRunning || scanRequested;
    ageSec = scanDoneAt ? (int32_t)((millis() - scanDoneAt) / 1000) : -1;
    const int n = scanCount < max ? scanCount : max;
    memcpy(out, scan, sizeof(pk::ScanEntry) * n);
    return n;
}

bool wifiStartScan() {
    Locked l(lock);
    if (scanRunning || scanRequested) return false;
    scanRequested = true;
    return true;
}

bool wifiJoin(const WifiJoinRequest& r) {
    Locked l(lock);
    if (!r.ssid[0] || trialRequested || trialActive) return false;
    pending = r;
    trialRequested = true;
    trialState = TrialState::Associating;  // the dashboard polls right away
    pk::copyStr(trial.ssid, r.ssid);
    trialDetail[0] = '\0';
    return true;
}

bool wifiTakeJoined(pk::WifiSettings& out) {
    Locked l(lock);
    if (!joinedReady) return false;
    out = joined;
    joinedReady = false;
    memset(&joined, 0, sizeof(joined));
    return true;
}

} // namespace hal
