// What the matrix shows: screen rotation, overlays, rendering, fades and
// brightness. Pure: the main loop fills EngineInputs, calls tick(), and pushes
// the frame to the LEDs at the brightness it gets back.
#pragma once

#include "bridge.h"
#include "frame.h"
#include "settings.h"

namespace pk {

enum class WifiMode : uint8_t { Connected, Connecting, Disconnected, SetupAp };
enum class TrialState : uint8_t { Idle, Associating, Authenticating, Connected, FailedAuth, FailedNoAp, FailedTimeout };

struct LocalTime {
    bool valid;
    int year, month, day;  // month 1-12
    int hour, minute, second;
    int weekday;           // 0 = Monday
};

struct EngineInputs {
    uint32_t now = 0;  // millis
    const DisplaySettings* display = nullptr;
    const ScheduleSettings* schedule = nullptr;

    SourceKind kind = SourceKind::Bridge;
    bool sourceConfigured = false;
    SourceState sourceState = SourceState::Waiting;
    bool haveData = false;
    const BridgeData* data = nullptr;
    uint32_t readingAgeSec = UINT32_MAX;
    uint8_t failures = 0;

    WifiMode wifi = WifiMode::Disconnected;
    bool hasWifiCredentials = false;
    const char* ip = "";
    const char* localName = "";  // "keuken.local" or ""
    const char* apSsid = "";
    const char* apIp = "";
    TrialState trial = TrialState::Idle;
    const char* trialSsid = "";
    bool dnsBlocked = false, dataBlocked = false;
    const char* dataHost = "";

    LocalTime time{};
    uint16_t light = 2048;  // light sensor, 0-4095

    bool updating = false;
    int updateProgress = -1;  // %, -1 unknown
    bool restarting = false;
};

struct EngineOutput {
    Frame frame;          // before brightness: this is what the mirror shows
    uint8_t brightness;   // 0-255, 0 = dark (night mute)
    uint8_t fade;         // 0-255 transition level, multiply with brightness
    Screen screen;
};

struct Preview {
    enum Kind : uint8_t { Pet, Alerts, Glucose } kind;
    PetId pet;
    int range;  // 0 in range, 1 low, 2 high
    int mgdl;
};

const char* screenName(Screen s);

class Engine {
public:
    void begin(uint32_t now);
    void tick(const EngineInputs& in, EngineOutput& out);

    // Buttons and web requests (main loop only).
    void next(uint32_t now) { step(1, now); }
    void prev(uint32_t now) { step(-1, now); }
    void wave(uint32_t now);
    void showAddress(uint32_t now);
    bool preview(const Preview& p, uint32_t now);  // false during an urgent low

    bool lowLocked() const { return lowLocked_; }
    bool urgent() const { return urgent_; }  // previews wait

private:
    enum class Fresh : uint8_t { None, Fresh, Warning, Stale };

    void step(int dir, uint32_t now);
    void buildRotation(const EngineInputs& in);
    int indexOf(Screen s) const;
    Fresh freshness(const EngineInputs& in) const;
    bool chooseStatus(const EngineInputs& in, char* text, size_t size, Rgb& color, bool& alternate);

    void render(Screen s, const EngineInputs& in, Frame& f);
    void renderGlucose(const EngineInputs& in, Frame& f, int mgdlOverride);
    void renderTir(const EngineInputs& in, Frame& f);
    void renderIob(const EngineInputs& in, Frame& f);
    void renderTime(const EngineInputs& in, Frame& f);
    void renderPet(const EngineInputs& in, Frame& f, bool preview);
    void renderAlerts(const EngineInputs& in, Frame& f);
    void renderStatus(const EngineInputs& in, Frame& f);
    uint8_t brightness(const EngineInputs& in, Screen shown) const;

    uint32_t bootAt_ = 0;

    // Rotation
    Screen rotation_[10];
    int rotationCount_ = 0;
    Screen current_ = Screen::Glucose;  // the rotation screen
    uint32_t screenStart_ = 0;
    bool needAdvance_ = false;          // a scroll/alert screen finished its run

    // What's on the matrix (rotation screen or overlay) and the fade between them.
    Screen shown_ = Screen::Boot;
    uint32_t shownSince_ = 0;
    enum class Phase : uint8_t { None, Out, In } phase_ = Phase::None;
    Screen target_ = Screen::Boot;
    uint32_t fadeStart_ = 0;
    uint8_t fadeFrom_ = 255;
    uint8_t fade_ = 255;

    // Overlays
    bool previewActive_ = false;
    Preview preview_{};
    uint32_t previewUntil_ = 0;
    bool addressActive_ = false;
    uint32_t addressStart_ = 0;
    uint32_t happyUntil_ = 0;

    // Status message (setup, no Wi-Fi, ...)
    char status_[112] = "";
    Rgb statusColor_{0, 0, 0};
    bool statusAlternate_ = false;
    uint32_t statusStart_ = 0;

    // Glucose
    bool lowLocked_ = false;
    bool urgent_ = false;
    uint32_t lastTimestamp_ = 0;
    int lastMgdl_ = 0;
    int delta_ = 0;
    uint32_t flashUntil_ = 0;

    // Alerts screen
    Warning alertCurrent_ = Warning::None;
    uint16_t alertsPlayed_ = 0;
    uint32_t alertStarted_ = 0;
    bool alertsDone_ = false;
    uint32_t alertsLastRender_ = 0;

    // Last brightness inputs that depend on the screen being rendered
    Fresh fresh_ = Fresh::None;
};

} // namespace pk
