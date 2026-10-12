#include "clock_net.h"
#include "clock_identity.h"
#include "config_manager.h"
#include "wifi_manager.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <limits.h>
#include <string.h>

#ifndef PUMPKIN_VERSION
#define PUMPKIN_VERSION "dev"
#endif

#define CLOCK_NET_SCAN_INTERVAL_MS 60000UL
#define CLOCK_NET_TASK_STACK 6144

// ESPmDNS has char* and const char* overloads; string literals would make the
// calls ambiguous, so pass const char* variables.
static const char* const SVC_PUMPKIN = "pumpkin";
static const char* const SVC_HTTP = "http";
static const char* const PROTO_TCP = "tcp";
static const char* const TXT_NAME = "name";
static const char* const TXT_VERSION = "ver";
static const char* const TXT_ID = "id";

namespace {

uint8_t chip_mac[6];
char chip_id[13] = "";
char default_name[CLOCK_NAME_MAX + 1] = "pumpkin";
char setup_ssid[32] = "Pumpkin-Setup";
char local_host[CLOCK_SLUG_MAX + 8] = "";   // "keuken-2.local"

SemaphoreHandle_t lock = nullptr;
TaskHandle_t task = nullptr;

volatile bool rename_requested = false;
volatile bool rescan_requested = false;
volatile bool scanning = false;
bool mdns_up = false;

ClockPeer peers[CLOCK_NET_MAX_PEERS];
int peer_count = 0;
unsigned long last_scan_ms = 0;
bool scanned_once = false;

ClockCopyResult copy_results[CLOCK_NET_MAX_PEERS];
int copy_count = 0;
volatile bool copy_pending = false;
volatile bool copy_running = false;
String copy_body;

struct Lock {
    Lock() { if (lock) xSemaphoreTake(lock, portMAX_DELAY); }
    ~Lock() { if (lock) xSemaphoreGive(lock); }
};

void copy_text(char* dst, size_t size, const char* src) {
    strncpy(dst, src ? src : "", size - 1);
    dst[size - 1] = '\0';
}

void current_slug(char* out, size_t size) {
    if (!clock_name_slug(clock_net_name(), out, size)) {
        clock_name_slug(default_name, out, size);
    }
}

// (Re)announce this clock. A name another device already answers for gets
// "-2", "-3", ... (checked with a short query before claiming it).
void start_mdns() {
    if (mdns_up) {
        MDNS.end();
        mdns_up = false;
    }
    {
        Lock l;
        local_host[0] = '\0';
    }

    char slug[CLOCK_SLUG_MAX + 1];
    current_slug(slug, sizeof(slug));

    char probe[24];
    snprintf(probe, sizeof(probe), "sc-%s", chip_id);
    char chosen[CLOCK_SLUG_MAX + 1];
    clock_slug_with_suffix(slug, 1, chosen, sizeof(chosen));
    if (MDNS.begin(probe)) {
        const IPAddress self = WiFi.localIP();
        for (int n = 1; n <= 9; ++n) {
            char candidate[CLOCK_SLUG_MAX + 1];
            clock_slug_with_suffix(slug, n, candidate, sizeof(candidate));
            const IPAddress owner = MDNS.queryHost(candidate, 800);
            if (owner == IPAddress((uint32_t)0) || owner == self) {
                copy_text(chosen, sizeof(chosen), candidate);
                break;
            }
            Serial.printf("[CLOCKNET] %s.local is taken by %s\n", candidate, owner.toString().c_str());
        }
        MDNS.end();
    }

    if (!MDNS.begin(chosen)) {
        Serial.println("[CLOCKNET] mDNS start failed");
        return;
    }
    const char* name = clock_net_name();
    MDNS.setInstanceName(name);
    const char* version = PUMPKIN_VERSION;
    const char* id = chip_id;
    MDNS.addService(SVC_HTTP, PROTO_TCP, 80);
    MDNS.addService(SVC_PUMPKIN, PROTO_TCP, 80);
    MDNS.addServiceTxt(SVC_PUMPKIN, PROTO_TCP, TXT_NAME, name);
    MDNS.addServiceTxt(SVC_PUMPKIN, PROTO_TCP, TXT_VERSION, version);
    MDNS.addServiceTxt(SVC_PUMPKIN, PROTO_TCP, TXT_ID, id);
    mdns_up = true;
    {
        Lock l;
        snprintf(local_host, sizeof(local_host), "%s.local", chosen);
    }
    Serial.printf("[CLOCKNET] Announced as %s.local (\"%s\")\n", chosen, name);
}

void scan_peers() {
    scanning = true;
    ClockPeer found[CLOCK_NET_MAX_PEERS];
    int found_count = 0;
    const int n = MDNS.queryService(SVC_PUMPKIN, PROTO_TCP);
    const IPAddress self = WiFi.localIP();
    for (int i = 0; i < n && found_count < CLOCK_NET_MAX_PEERS; ++i) {
        const IPAddress ip = MDNS.IP(i);
        const String id = MDNS.txt(i, TXT_ID);
        if (ip == self || (id.length() && id == chip_id)) continue;
        bool dup = false;
        for (int j = 0; j < found_count; ++j) {
            dup |= (id.length() && id == found[j].id) || ip.toString() == found[j].ip;
        }
        if (dup) continue;
        ClockPeer& p = found[found_count++];
        memset(&p, 0, sizeof(p));
        const String host = MDNS.hostname(i);
        const String name = MDNS.txt(i, TXT_NAME);
        copy_text(p.name, sizeof(p.name), name.length() ? name.c_str() : host.c_str());
        snprintf(p.host, sizeof(p.host), "%s.local", host.c_str());
        copy_text(p.ip, sizeof(p.ip), ip.toString().c_str());
        copy_text(p.version, sizeof(p.version), MDNS.txt(i, TXT_VERSION).c_str());
        copy_text(p.id, sizeof(p.id), id.c_str());
    }
    {
        Lock l;
        memcpy(peers, found, sizeof(ClockPeer) * found_count);
        peer_count = found_count;
        last_scan_ms = millis();
        scanned_once = true;
    }
    scanning = false;
}

void run_copies() {
    copy_running = true;
    copy_pending = false;
    String body;
    int count;
    {
        Lock l;
        body = copy_body;
        count = copy_count;
    }
    for (int i = 0; i < count; ++i) {
        char ip[16];
        {
            Lock l;
            copy_text(ip, sizeof(ip), copy_results[i].ip);
        }
        WiFiClient client;
        HTTPClient http;
        int code = -1;
        char url[48];
        snprintf(url, sizeof(url), "http://%s/api/config", ip);
        if (http.begin(client, url)) {
            http.setConnectTimeout(3000);
            http.setTimeout(5000);
            http.addHeader("Content-Type", "application/json");
            // Other clocks with a web page password accept this clock's password
            // (user "pumpkin"); a different password answers 401.
            const char* password = config_get().web_password;
            if (password[0]) http.setAuthorization("pumpkin", password);
            code = http.POST(body);
            http.end();
        }
        Serial.printf("[CLOCKNET] Copy settings to %s: %d\n", ip, code);
        Lock l;
        copy_results[i].http_code = code;
        copy_results[i].state = code == 200 ? CLOCK_COPY_OK : CLOCK_COPY_FAILED;
    }
    {
        Lock l;
        copy_body = String();   // the body can hold the bridge token: don't keep it
    }
    copy_running = false;
}

void task_main(void*) {
    for (;;) {
        if (!wifi_is_connected() || wifi_is_ap_mode()) {
            if (mdns_up) {
                MDNS.end();
                mdns_up = false;
                Lock l;
                local_host[0] = '\0';
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (!mdns_up || rename_requested) {
            rename_requested = false;
            start_mdns();
            if (!mdns_up) {
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            rescan_requested = true;  // peers learn the new name; refresh ours too
        }
        if (copy_pending) run_copies();
        if (rescan_requested || !scanned_once || millis() - last_scan_ms >= CLOCK_NET_SCAN_INTERVAL_MS) {
            rescan_requested = false;
            scan_peers();
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

} // namespace

void clock_net_init() {
    if (!lock) lock = xSemaphoreCreateMutex();
    esp_efuse_mac_get_default(chip_mac);
    clock_id_from_mac(chip_mac, chip_id, sizeof(chip_id));
    clock_default_name(chip_mac, default_name, sizeof(default_name));
    clock_setup_ssid(chip_mac, setup_ssid, sizeof(setup_ssid));
    // The router's client list shows this name too (DHCP). It must be set
    // before the station starts, so a rename shows up there after a restart.
    char slug[CLOCK_SLUG_MAX + 1];
    current_slug(slug, sizeof(slug));
    WiFi.setHostname(slug);
}

void clock_net_start() {
    if (task) return;
    xTaskCreatePinnedToCore(task_main, "clocknet", CLOCK_NET_TASK_STACK, nullptr, 1, &task, 0);
}

const char* clock_net_id() { return chip_id; }

const char* clock_net_name() {
    const char* configured = config_get().clock_name;
    return configured[0] ? configured : default_name;
}

const char* clock_net_setup_ssid() { return setup_ssid; }

const char* clock_net_slug() {
    static char slug[CLOCK_SLUG_MAX + 1];
    current_slug(slug, sizeof(slug));
    return slug;
}

const char* clock_net_local_host() { return local_host; }

void clock_net_name_changed() { rename_requested = true; }

int clock_net_peers(ClockPeer* out, int max) {
    Lock l;
    const int n = peer_count < max ? peer_count : max;
    memcpy(out, peers, sizeof(ClockPeer) * n);
    return n;
}

bool clock_net_scanning() { return scanning; }

unsigned long clock_net_last_scan_age_ms() {
    Lock l;
    return scanned_once ? millis() - last_scan_ms : ULONG_MAX;
}

void clock_net_rescan() { rescan_requested = true; }

bool clock_net_copy_start(const char* const* target_ips, int count, const char* body) {
    if (!target_ips || count <= 0 || count > CLOCK_NET_MAX_PEERS || !body) return false;
    Lock l;
    if (copy_pending || copy_running) return false;
    ClockCopyResult next[CLOCK_NET_MAX_PEERS];
    for (int i = 0; i < count; ++i) {
        const ClockPeer* match = nullptr;
        for (int j = 0; j < peer_count; ++j) {
            if (target_ips[i] && strcmp(target_ips[i], peers[j].ip) == 0) match = &peers[j];
        }
        if (!match) return false;   // only clocks this clock has seen itself
        memset(&next[i], 0, sizeof(next[i]));
        copy_text(next[i].ip, sizeof(next[i].ip), match->ip);
        copy_text(next[i].name, sizeof(next[i].name), match->name);
        next[i].state = CLOCK_COPY_PENDING;
    }
    memcpy(copy_results, next, sizeof(ClockCopyResult) * count);
    copy_count = count;
    copy_body = body;
    copy_pending = true;
    return true;
}

int clock_net_copy_results(ClockCopyResult* out, int max, bool* running) {
    Lock l;
    if (running) *running = copy_pending || copy_running;
    const int n = copy_count < max ? copy_count : max;
    memcpy(out, copy_results, sizeof(ClockCopyResult) * n);
    return n;
}
