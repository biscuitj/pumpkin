// Pumpkin firmware for the Ulanzi TC001: glucose from the Pumpkin bridge on a
// 32x8 matrix, set up and configured from the dashboard the clock serves.
//
// Tasks: the main loop (this file: render at 20 fps, buttons, Wi-Fi, saving),
// the network task (hal/net: bridge fetches, checks, mDNS) and the web server
// (hal/web, on AsyncTCP's task). They share pk::App under its mutex.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include "core/app.h"
#include "core/identity.h"
#include "core/runner.h"
#include "hal/clock_time.h"
#include "hal/inputs.h"
#include "hal/leds.h"
#include "hal/net.h"
#include "hal/ota.h"
#include "hal/pins.h"
#include "hal/storage.h"
#include "hal/web.h"
#include "hal/web_assets.h"
#include "hal/wifi.h"

#ifndef PUMPKIN_VERSION
#error "PUMPKIN_VERSION comes from the VERSION file (scripts/pre_build.py)"
#endif

namespace {

constexpr uint32_t kFrameMs = 50;
constexpr uint32_t kSaveAfterMs = 1500;  // settle a burst of dashboard edits into one flash write
constexpr uint32_t kRestartDelayMs = 1500;
constexpr int kWatchdogSec = 30;

char chipId[13] = "";
char defaultName[CLOCK_NAME_MAX + 1] = "pumpkin";
char setupSsid[33] = "Pumpkin-Setup";

volatile bool restartPending = false;
volatile bool resetPending = false;
uint32_t restartAt = 0;
volatile uint8_t brightnessNow = 0;

const char* resetReasonName() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "power_on";
        case ESP_RST_SW: return "software";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_BROWNOUT: return "brownout";
        default: return "other";
    }
}

class EspPlatform : public pk::Platform {
public:
    uint32_t millis() override {
        const uint32_t m = ::millis();
        return m ? m : 1;  // 0 means "never" in a few places
    }

    void wifiView(pk::WifiView& out) override {
        hal::wifiView(out);
        if (out.mode == pk::WifiMode::Connected) hal::netLocalName(out.localName, sizeof(out.localName));
        out.caInstalled = hal::caExists();
    }
    int wifiScan(pk::ScanEntry* out, int max, bool& scanning, int32_t& ageSec) override {
        return hal::wifiScan(out, max, scanning, ageSec);
    }
    bool wifiStartScan() override { return hal::wifiStartScan(); }
    bool wifiJoin(const pk::WifiJoinRequest& r) override { return hal::wifiJoin(r); }
    bool caWrite(const char* pem, size_t len) override { return hal::caWrite(pem, len); }
    bool caDelete() override { return hal::caDelete(); }
    bool caInstalled() override { return hal::caExists(); }

    void netRecheck() override { hal::netRecheck(); }
    void wakeNetwork() override { hal::netWake(); }

    void localTime(pk::LocalTime& out, pk::TimeSource& source) override { hal::timeLocal(out, source); }
    void applyTimezone(const char* posix) override { hal::timeApplyZone(posix); }

    const char* defaultName() override { return ::defaultName; }
    void nameChanged() override {
        hal::netNameChanged();
        // The router's client list picks this up at the next join.
        char slug[CLOCK_SLUG_MAX + 1];
        if (clock_name_slug(app_->name(), slug, sizeof(slug))) WiFi.setHostname(slug);
    }
    void diagnostics(pk::Diagnostics& d) override {
        d.uptimeSec = ::millis() / 1000;
        d.resetReason = resetReasonName();
        d.freeHeap = ESP.getFreeHeap();
        d.minFreeHeap = ESP.getMinFreeHeap();
        d.largestBlock = ESP.getMaxAllocHeap();
        d.light = hal::lightLevel();
        d.brightnessNow = brightnessNow;
        hal::buttonStates(d.left, d.middle, d.right);
    }
    void restartSoon() override { restartPending = true; }
    void factoryResetSoon() override {
        resetPending = true;
        restartPending = true;
    }
    const char* firmwareVersion() override { return PUMPKIN_VERSION; }
    const char* hardware() override { return "Ulanzi TC001"; }
    bool rolledBack(char* from, size_t size) override { return hal::otaRolledBack(from, size); }
    void updateView(pk::UpdateView& out) override { hal::otaView(out); }

    pk::App* app_ = nullptr;
};

EspPlatform platform;
pk::App app(platform);
pk::Runner runner(app);
pk::WifiSettings wifiSaved{};
bool settingsLoaded = false;
bool webStarted = false;
bool addressShown = false;

void saveIfDirty(uint32_t now) {
    pk::Settings copy;
    uint8_t groups;
    {
        pk::Locked l(app.mutex);
        if (!app.dirty || now - app.dirtyAt < kSaveAfterMs) return;
        groups = app.dirty;
        app.dirty = 0;
        copy = app.settings;
    }
    hal::storageSave(copy, groups);
}

} // namespace

void setup() {
    // An undriven buzzer pin can chirp: hold it low. Pumpkin makes no sound.
    pinMode(pins::kBuzzer, OUTPUT);
    digitalWrite(pins::kBuzzer, LOW);

    Serial.begin(115200);
    Serial.printf("\n[boot] Pumpkin %s, reset: %s\n", PUMPKIN_VERSION, resetReasonName());

    hal::ledsBegin();
    platform.app_ = &app;
    runner.begin();
    hal::ledsShow(runner.frame(pk::HardwareInputs{}).frame, 40);  // the pumpkin, right away

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    clock_id_from_mac(mac, chipId, sizeof(chipId));
    clock_default_name(mac, defaultName, sizeof(defaultName));
    clock_setup_ssid(mac, setupSsid, sizeof(setupSsid));

    hal::storageLoad(app.settings);
    settingsLoaded = true;
    wifiSaved = app.settings.wifi;

    hal::inputsBegin();
    hal::timeBegin(app.settings.device.posix);
    hal::otaBegin();

    char slug[CLOCK_SLUG_MAX + 1];
    if (!clock_name_slug(app.name(), slug, sizeof(slug))) pk::copyStr(slug, "pumpkin");
    hal::wifiBegin(wifiSaved, setupSsid, slug);
    hal::netStart(app, chipId);

    esp_task_wdt_init(kWatchdogSec, true);
    esp_task_wdt_add(nullptr);
    Serial.printf("[boot] Ready, %u bytes free\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
    const uint32_t start = millis();
    esp_task_wdt_reset();

    {
        pk::Locked l(app.mutex);
        wifiSaved = app.settings.wifi;
    }
    hal::wifiLoop(wifiSaved);

    // A join from the dashboard worked: keep those credentials.
    pk::WifiSettings joined;
    if (hal::wifiTakeJoined(joined)) {
        pk::Locked l(app.mutex);
        app.settings.wifi = joined;
        app.markDirty(pk::kGroupWifi);
        addressShown = false;
    }

    // Start serving once the network stack is up (hotspot or joined).
    if (!webStarted && (hal::wifiConnected() || hal::wifiApUp())) {
        hal::webBegin(app);
        webStarted = true;
    }

    hal::inputsLoop();
    pk::Button b;
    while (hal::nextButton(b)) runner.button(b);

    pk::UpdateView update;
    hal::otaView(update);
    pk::HardwareInputs hw;
    hw.light = hal::lightLevel();
    hw.updating = update.state == pk::UpdateView::Receiving;
    hw.updateProgress = hal::otaProgress();
    hw.restarting = restartPending || update.state == pk::UpdateView::Restarting;
    const pk::EngineOutput& out = runner.frame(hw);
    brightnessNow = out.brightness;
    hal::ledsShow(out.frame, (uint8_t)(((uint16_t)out.brightness * out.fade + 127) / 255));

    // Show the new address once after a join from the setup page.
    if (!addressShown) {
        pk::WifiView v;
        platform.wifiView(v);
        if (v.trial == pk::TrialState::Connected && v.mode == pk::WifiMode::Connected && v.localName[0]) {
            runner.button(pk::Button::MiddleLong);
            addressShown = true;
        }
    }

    saveIfDirty(start);
    hal::otaLoop(settingsLoaded && kWebAssetCount > 0);

    if (restartPending) {
        if (!restartAt) restartAt = start + kRestartDelayMs;  // let the answer go out first
        if ((int32_t)(start - restartAt) >= 0) {
            if (resetPending) {
                hal::storageErase();
                Serial.println("[boot] Factory reset");
            } else {
                saveIfDirty(start + kSaveAfterMs);  // flush pending edits
            }
            Serial.flush();
            ESP.restart();
        }
    }

    const uint32_t spent = millis() - start;
    if (spent < kFrameMs) delay(kFrameMs - spent);
}
