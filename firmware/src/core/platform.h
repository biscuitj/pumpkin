// What the core needs from the hardware. The ESP32 build implements this in
// src/hal; the host simulator (host/) fakes it.
#pragma once

#include "engine.h"
#include "settings.h"

namespace pk {

enum class CheckResult : uint8_t { Pending, Ok, Fail, Skipped };
enum class TimeSource : uint8_t { None, Ntp, Rtc, Http };

struct NetChecks {
    bool running = false;
    CheckResult dns = CheckResult::Skipped, data = CheckResult::Skipped, ntp = CheckResult::Skipped;
    char dataHost[64] = "";
};

struct WifiView {
    WifiMode mode = WifiMode::Disconnected;
    char ssid[33] = "";
    bool hasSecurity = false;
    WifiSecurity security = WifiSecurity::Personal;
    bool hasRssi = false;
    int rssi = 0;
    char ip[16] = "";
    char localName[40] = "";  // "keuken.local", "" before mDNS is up
    bool ap = false;
    char apSsid[33] = "";
    char apIp[16] = "";
    int apStations = 0;
    bool caInstalled = false;
    TrialState trial = TrialState::Idle;
    char trialSsid[33] = "";
    char trialDetail[96] = "";
};

struct ScanEntry {
    char ssid[33];
    int8_t rssi;
    bool secure;
    bool enterprise;
};

struct WifiJoinRequest {
    char ssid[33];
    WifiSecurity security;
    EapMethod eap;
    char password[128];
    char identity[128];
    char anonIdentity[128];
    bool validateCa;
};

struct Diagnostics {
    uint32_t uptimeSec;
    const char* resetReason;  // contract ResetReason
    uint32_t freeHeap, minFreeHeap, largestBlock;
    uint16_t light;
    uint8_t brightnessNow;
    bool left, middle, right;
};

struct UpdateView {
    enum State : uint8_t { Idle, Receiving, Restarting, Failed } state = Idle;
    uint32_t received = 0;
    char error[24] = "";  // contract UpdateError
};

class Platform {
public:
    virtual ~Platform() {}
    virtual uint32_t millis() = 0;

    // Wi-Fi. Joins and scans only start work; the Wi-Fi loop does it.
    virtual void wifiView(WifiView& out) = 0;
    virtual int wifiScan(ScanEntry* out, int max, bool& scanning, int32_t& ageSec) = 0;
    virtual bool wifiStartScan() = 0;
    virtual bool wifiJoin(const WifiJoinRequest& r) = 0;  // false while a join runs
    virtual bool caWrite(const char* pem, size_t len) = 0;
    virtual bool caDelete() = 0;
    virtual bool caInstalled() = 0;

    // Network task
    virtual void netRecheck() = 0;
    virtual void wakeNetwork() = 0;  // fetch now: the source changed

    // Time
    virtual void localTime(LocalTime& out, TimeSource& source) = 0;
    virtual void applyTimezone(const char* posix) = 0;

    // Device
    virtual const char* defaultName() = 0;  // "pumpkin-e5f6"
    virtual void nameChanged() = 0;         // re-announce the .local name
    virtual void diagnostics(Diagnostics& out) = 0;
    virtual void restartSoon() = 0;         // after the response has gone out
    virtual void factoryResetSoon() = 0;
    virtual const char* firmwareVersion() = 0;
    virtual const char* hardware() = 0;
    // An update that failed its first boot was rolled back; set once.
    virtual bool rolledBack(char* fromVersion, size_t size) = 0;
    virtual void updateView(UpdateView& out) = 0;
};

} // namespace pk
