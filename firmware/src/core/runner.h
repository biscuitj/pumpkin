// One render-loop pass: gather everything the engine needs from App and the
// platform, run it, and publish the mirror frame. Shared by the clock's main
// loop and the simulator, so both behave the same.
#pragma once

#include "app.h"
#include "engine.h"

namespace pk {

enum class Button : uint8_t { Left, Right, Middle, MiddleLong };

struct HardwareInputs {
    uint16_t light = 2048;
    bool updating = false;
    int updateProgress = -1;
    bool restarting = false;
};

class Runner {
public:
    Runner(App& app) : app_(app) {}
    void begin() { engine_.begin(app_.platform.millis()); }

    // Call every ~50 ms. The output stays valid until the next call.
    const EngineOutput& frame(const HardwareInputs& hw);
    void button(Button b);

private:
    App& app_;
    Engine engine_;
    EngineOutput out_{};
    // Copies taken under the lock, so the engine runs without it.
    Settings settings_{};
    BridgeData data_{};
    WifiView wifi_{};
    char dataHost_[64] = "";
};

} // namespace pk
