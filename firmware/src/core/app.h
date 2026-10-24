// The shared state of a running clock: settings, the latest bridge data,
// requests waiting for the network task, and the mirror frame. Every task
// takes `mutex` before touching it.
#pragma once

#include "bridge.h"
#include "engine.h"
#include "platform.h"
#include "settings.h"
#include "sync.h"

namespace pk {

enum Group : uint8_t {
    kGroupDisplay = 1,
    kGroupSchedule = 2,
    kGroupSource = 4,
    kGroupWifi = 8,
    kGroupDevice = 16,
    kGroupAll = 31,
};

struct SourceRuntime {
    bool haveData = false;
    BridgeData data{};
    uint32_t readingAtMs = 0;    // millis() when the reading was taken (may be before boot: unsigned maths)
    bool fetched = false;        // at least one fetch finished since the settings changed
    uint32_t lastFetchAtMs = 0;
    uint8_t failures = 0;
    SourceState state = SourceState::Waiting;
    FetchOutcome last{};
};

struct SourceTest {
    enum State : uint8_t { Idle, Requested, Running, Done } state = Idle;
    char url[kUrlMax + 1] = "";
    char token[kTokenMax + 1] = "";
    FetchOutcome outcome{};
    BridgeData data{};
    SourceState result = SourceState::Waiting;
    uint32_t readingAgeSec = UINT32_MAX;
};

struct AlertsClear {
    enum State : uint8_t { Idle, Requested, Running, Done, Failed } state = Idle;
    char error[64] = "";
};

// Web requests for the engine, picked up by the main loop.
struct EngineCommands {
    bool next = false, prev = false;
    bool preview = false;
    Preview p{};
};

struct Mirror {
    Frame frame{};
    Screen screen = Screen::Boot;
    uint32_t sequence = 0;
    bool ready = false;
    // Why the rotation is held, updated every frame: the dashboard says so
    // instead of next/prev silently doing nothing.
    bool lowLocked = false;
    bool urgent = false;
    uint32_t requestedAt = 0;  // the matrix is only copied while someone watches
    bool watched(uint32_t now) const { return requestedAt != 0 && now - requestedAt < 5000; }
};

class App {
public:
    explicit App(Platform& p) : platform(p) { setDefaults(settings); }

    Platform& platform;
    Mutex mutex;

    Settings settings;
    SourceRuntime source;
    SourceTest test;
    AlertsClear clear;
    NetChecks net;
    EngineCommands commands;
    Mirror mirror;

    uint8_t dirty = 0;        // groups to save
    uint32_t dirtyAt = 0;
    void markDirty(uint8_t groups);

    // Derived, under the lock
    bool sourceConfigured() const;
    uint32_t readingAgeSec(uint32_t now) const;
    SourceState sourceState() const;
    const char* name() { return settings.device.name[0] ? settings.device.name : platform.defaultName(); }

    // The source settings changed: forget data from the old source.
    void resetSource();
};

} // namespace pk
