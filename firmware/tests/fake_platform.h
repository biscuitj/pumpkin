// A Platform for tests: fixed answers, records what the core asked for.
#pragma once

#include "core/platform.h"

struct FakePlatform : pk::Platform {
    uint32_t now = 100000;
    pk::WifiView wifi;
    bool joinOk = true;
    int joins = 0, scans = 0, rechecks = 0, wakes = 0, renames = 0, restarts = 0, resets = 0;
    char posix[64] = "";
    bool ca = false;
    pk::LocalTime time{true, 2026, 10, 17, 15, 0, 0, 4};

    FakePlatform() {
        wifi.mode = pk::WifiMode::Connected;
        pk::copyStr(wifi.ssid, "Thuis");
        pk::copyStr(wifi.ip, "192.168.1.50");
        pk::copyStr(wifi.localName, "keuken.local");
    }

    uint32_t millis() override { return now; }
    void wifiView(pk::WifiView& out) override { out = wifi; }
    int wifiScan(pk::ScanEntry* out, int max, bool& scanning, int32_t& age) override {
        scanning = false;
        age = 3;
        if (max < 1) return 0;
        out[0] = {"Thuis", -50, true, false};
        return 1;
    }
    bool wifiStartScan() override { return ++scans, true; }
    bool wifiJoin(const pk::WifiJoinRequest&) override { return ++joins, joinOk; }
    bool caWrite(const char*, size_t) override { return ca = true; }
    bool caDelete() override { return !(ca = false); }
    bool caInstalled() override { return ca; }
    void netRecheck() override { ++rechecks; }
    void wakeNetwork() override { ++wakes; }
    void localTime(pk::LocalTime& out, pk::TimeSource& src) override {
        out = time;
        src = time.valid ? pk::TimeSource::Ntp : pk::TimeSource::None;
    }
    void applyTimezone(const char* p) override { pk::copyStr(posix, p); }
    const char* defaultName() override { return "pumpkin-e5f6"; }
    void nameChanged() override { ++renames; }
    void diagnostics(pk::Diagnostics& d) override { d = pk::Diagnostics{60, "power_on", 150000, 120000, 80000, 2000, 40, false, false, false}; }
    void restartSoon() override { ++restarts; }
    void factoryResetSoon() override { ++resets; }
    const char* firmwareVersion() override { return "1.0.0"; }
    const char* hardware() override { return "Ulanzi TC001"; }
    bool rolledBack(char*, size_t) override { return false; }
    void updateView(pk::UpdateView& u) override { u = pk::UpdateView{}; }
};
