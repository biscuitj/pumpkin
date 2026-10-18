// Buttons and the light sensor. Polled from the main loop.
#pragma once

#include <stdint.h>

#include "core/runner.h"

namespace hal {

void inputsBegin();
void inputsLoop();

// The next button event, if any.
bool nextButton(pk::Button& out);

// Raw states for diagnostics (true = held down).
void buttonStates(bool& left, bool& middle, bool& right);

// Smoothed light level, 0 (dark) to 4095.
uint16_t lightLevel();

} // namespace hal
