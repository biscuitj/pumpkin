#include "net.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>

#include "clock_time.h"
#include "core/identity.h"
#include "core/net_jobs.h"
#include "tls_roots.h"
#include "wifi.h"

#ifndef PUMPKIN_VERSION
#define PUMPKIN_VERSION "dev"
#endif

namespace hal {

namespace {

using pk::CheckResult;
using pk::Locked;

constexpr uint32_t kSettleMs = 4000;               // let DHCP and DNS settle after joining
constexpr uint32_t kRecheckEveryMs = 15 * 60000UL;  // a school firewall can change under us
constexpr size_t kMaxBody = 16 * 1024;

pk::App* app = nullptr;
char chipId[13] = "";

volatile bool recheckRequested = false;
volatile bool wakeRequested = false;
volatile bool renameRequested = false;

pk::Mutex nameLock;
char localName[pk::kNameMax + 16] = "";
bool mdnsUp = false;

// ---------------------------------------------------------------- HTTPS

// Collects a response body into std::string, up to kMaxBody.
class Sink : public Stream {
public:
    explicit Sink(std::string& s) : s_(s) {}
    bool overflow = false;
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* b, size_t n) override {
        if (s_.size() + n > kMaxBody) {
            overflow = true;
            return 0;
        }
        s_.append(reinterpret_cast<const char*>(b), n);
        return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}

private:
    std::string& s_;
};

class EspHttp : public pk::HttpTransport {
public:
    void get(const char* url, const char* token, pk::HttpResult& out) override { request("GET", url, token, out); }
    void post(const char* url, const char* token, pk::HttpResult& out) override { request("POST", url, token, out); }

private:
    void request(const char* method, const char* url, const char* token, pk::HttpResult& out) {
        out = pk::HttpResult{};
        const uint32_t start = millis();
        WiFiClientSecure client;
        // The roots Cloudflare and the other big hosts issue from. mbedTLS here
        // doesn't check certificate dates, so this works before the clock has the time.
        client.setCACert(kTlsRoots);
        client.setHandshakeTimeout(12);
        HTTPClient http;
        http.setReuse(false);
        http.setConnectTimeout(8000);
        http.setTimeout(12000);
        http.setUserAgent("Pumpkin/" PUMPKIN_VERSION);
        if (!http.begin(client, url)) {
            out.transportFailed = true;
            pk::copyStr(out.error, "That address doesn't work");
            return;
        }
        if (token && token[0]) http.addHeader("Authorization", String("Bearer ") + token);
        http.addHeader("Accept", "application/json");
        static const char* kHeaders[] = {"Date"};
        http.collectHeaders(kHeaders, 1);
        const int code = !strcmp(method, "POST") ? http.POST((uint8_t*)nullptr, 0) : http.GET();
        if (code <= 0) {
            out.transportFailed = true;
            char tls[80] = "";
            const int tlsErr = client.lastError(tls, sizeof(tls));
            if (tlsErr == -0x2700 || strstr(tls, "verif"))
                pk::copyStr(out.error, "The bridge's certificate isn't trusted");
            else if (code == HTTPC_ERROR_CONNECTION_REFUSED)
                pk::copyStr(out.error, tlsErr ? "Secure connection failed" : "Can't connect to the bridge");
            else if (code == HTTPC_ERROR_READ_TIMEOUT)
                pk::copyStr(out.error, "The bridge took too long to answer");
            else
                pk::copyStr(out.error, HTTPClient::errorToString(code).c_str());
            http.end();
            out.ms = millis() - start;
            return;
        }
        out.status = code;
        if (http.hasHeader("Date")) timeFromHttpDate(http.header("Date").c_str());
        const int size = http.getSize();
        if (size > (int)kMaxBody) {
            out.body.clear();
            pk::copyStr(out.error, "The bridge's answer is too big");
            out.status = 0;
            out.transportFailed = true;
        } else {
            // HTTPClient undoes chunked encoding (Workers use it) on the way.
            Sink sink(out.body);
            http.writeToStream(&sink);
            if (sink.overflow) {
                out.body.clear();
                out.transportFailed = true;
                out.status = 0;
                pk::copyStr(out.error, "The bridge's answer is too big");
            }
        }
        http.end();
        out.ms = millis() - start;
    }
};

EspHttp http;
pk::NetJobs* jobs = nullptr;

// ---------------------------------------------------------------- checks

enum class Step : uint8_t { Idle, Settle, Dns, Data, Ntp, Done };
Step step = Step::Idle;
uint32_t stepAt = 0, checkedAt = 0;
char dataHost[64] = "";

void setChecks(CheckResult dns, CheckResult data, CheckResult ntp, bool running) {
    Locked l(app->mutex);
    app->net.running = running;
    app->net.dns = dns;
    app->net.data = data;
    app->net.ntp = ntp;
    pk::copyStr(app->net.dataHost, dataHost);
}

void startChecks() {
    {
        Locked l(app->mutex);
        dataHost[0] = '\0';
        if (app->settings.source.kind == pk::SourceKind::Bridge)
            pk::hostOfUrl(app->settings.source.bridgeUrl, dataHost, sizeof(dataHost));
    }
    step = Step::Settle;
    stepAt = millis() + kSettleMs;
    setChecks(CheckResult::Pending, dataHost[0] ? CheckResult::Pending : CheckResult::Skipped, CheckResult::Pending, true);
}

bool probeNtp() {
    IPAddress addr;
    if (!WiFi.hostByName("pool.ntp.org", addr)) return false;
    WiFiUDP udp;
    if (!udp.begin(0)) return false;
    uint8_t packet[48] = {0x23};  // LI 0, version 4, client
    udp.beginPacket(addr, 123);
    udp.write(packet, sizeof(packet));
    udp.endPacket();
    const uint32_t start = millis();
    bool ok = false;
    while (millis() - start < 3000) {
        if (udp.parsePacket() >= 48) {
            ok = true;
            break;
        }
        delay(20);
    }
    udp.stop();
    return ok;
}

// One probe per call: each can block a few seconds.
void checksStep() {
    if (step == Step::Idle || step == Step::Done) return;
    if (step == Step::Settle) {
        if ((int32_t)(millis() - stepAt) >= 0) step = Step::Dns;
        return;
    }
    CheckResult dns, data, ntp;
    {
        Locked l(app->mutex);
        dns = app->net.dns;
        data = app->net.data;
        ntp = app->net.ntp;
    }
    if (step == Step::Dns) {
        IPAddress a;
        dns = WiFi.hostByName(dataHost[0] ? dataHost : "pool.ntp.org", a) ? CheckResult::Ok : CheckResult::Fail;
        step = dns == CheckResult::Ok ? (dataHost[0] ? Step::Data : Step::Ntp) : Step::Done;
        if (dns == CheckResult::Fail) {
            // Nothing else can work without names.
            if (data == CheckResult::Pending) data = CheckResult::Skipped;
            ntp = CheckResult::Skipped;
        }
    } else if (step == Step::Data) {
        WiFiClientSecure c;
        c.setInsecure();  // reachability only; the real fetch checks the certificate
        c.setHandshakeTimeout(8);
        data = c.connect(dataHost, 443, 8000) ? CheckResult::Ok : CheckResult::Fail;
        c.stop();
        step = Step::Ntp;
    } else if (step == Step::Ntp) {
        ntp = probeNtp() ? CheckResult::Ok : CheckResult::Fail;
        step = Step::Done;
    }
    const bool done = step == Step::Done;
    if (done) checkedAt = millis();
    setChecks(dns, data, ntp, !done);
    if (done) Serial.printf("[net] checks: dns %d, %s %d, ntp %d\n", (int)dns, dataHost, (int)data, (int)ntp);
}

// ---------------------------------------------------------------- mDNS

void mdnsStop() {
    if (mdnsUp) MDNS.end();
    mdnsUp = false;
    Locked l(nameLock);
    localName[0] = '\0';
}

// Claims "<slug>.local", or "<slug>-2.local" and so on when another device
// already answers for it.
void mdnsStart() {
    mdnsStop();
    char name[pk::kNameMax + 1], slug[CLOCK_SLUG_MAX + 1];
    {
        Locked l(app->mutex);
        pk::copyStr(name, app->name());
    }
    if (!clock_name_slug(name, slug, sizeof(slug))) pk::copyStr(slug, "pumpkin");

    char chosen[CLOCK_SLUG_MAX + 1];
    clock_slug_with_suffix(slug, 1, chosen, sizeof(chosen));
    char probe[24];
    snprintf(probe, sizeof(probe), "pk-%s", chipId);
    if (MDNS.begin(probe)) {
        const IPAddress self = WiFi.localIP();
        for (int n = 1; n <= 9; ++n) {
            char candidate[CLOCK_SLUG_MAX + 1];
            clock_slug_with_suffix(slug, n, candidate, sizeof(candidate));
            const IPAddress owner = MDNS.queryHost(candidate, 800);
            if (owner == IPAddress((uint32_t)0) || owner == self) {
                pk::copyStr(chosen, candidate);
                break;
            }
        }
        MDNS.end();
    }
    if (!MDNS.begin(chosen)) {
        Serial.println("[net] mDNS didn't start");
        return;
    }
    // ESPmDNS has char* and const char* overloads: literals would be ambiguous.
    const char* svcHttp = "http";
    const char* pumpkin = "pumpkin";
    const char* tcp = "tcp";
    const char* ver = "ver";
    const char* version = PUMPKIN_VERSION;
    const char* id = "id";
    const char* idValue = chipId;
    MDNS.setInstanceName(name);
    MDNS.addService(svcHttp, tcp, 80);
    MDNS.addService(pumpkin, tcp, 80);
    MDNS.addServiceTxt(pumpkin, tcp, ver, version);
    MDNS.addServiceTxt(pumpkin, tcp, id, idValue);
    mdnsUp = true;
    Locked l(nameLock);
    snprintf(localName, sizeof(localName), "%s.local", chosen);
    Serial.printf("[net] %s\n", localName);
}

// ---------------------------------------------------------------- task

void task(void*) {
    bool wasOnline = false;
    for (;;) {
        const bool online = wifiConnected();
        if (online != wasOnline) {
            wasOnline = online;
            if (online) {
                timeOnline();
                startChecks();
                jobs->fetchSoon();
            } else {
                mdnsStop();
                step = Step::Idle;
                setChecks(CheckResult::Skipped, CheckResult::Skipped, CheckResult::Skipped, false);
            }
        }
        if (online) {
            if (!mdnsUp || renameRequested) {
                renameRequested = false;
                mdnsStart();
            }
            if (recheckRequested) {
                recheckRequested = false;
                startChecks();
            } else if ((step == Step::Done) && millis() - checkedAt > kRecheckEveryMs) {
                startChecks();
            }
            checksStep();
        }
        if (wakeRequested) {
            wakeRequested = false;
            jobs->fetchSoon();
        }
        uint32_t wait = jobs->step(online, timeUnix());
        if (step != Step::Idle && step != Step::Done) wait = 50;
        if (wait < 20) wait = 20;
        if (wait > 250) wait = 250;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

} // namespace

void netStart(pk::App& a, const char* id) {
    app = &a;
    pk::copyStr(chipId, id);
    static pk::NetJobs j(a, http);
    jobs = &j;
    // TLS needs the room. Core 0, next to the Wi-Fi driver; the render loop is on core 1.
    xTaskCreatePinnedToCore(task, "net", 12288, nullptr, 1, nullptr, 0);
}

void netRecheck() { recheckRequested = true; }
void netWake() { wakeRequested = true; }
void netNameChanged() { renameRequested = true; }

void netLocalName(char* out, size_t size) {
    Locked l(nameLock);
    pk::copyStr(out, size, localName);
}

} // namespace hal
