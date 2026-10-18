// Fixed pixel art: trend arrows, pump alert images, the boot pumpkin.
#pragma once

#include "frame.h"
#include "types.h"

namespace pk {

// 5x7 trend arrow at (x, 0). Nothing for Trend::Unknown.
void drawTrend(Frame& f, Trend t, int x, Rgb c);

// A pump alert's 32x8 image in its own colour. False for Warning::None.
bool drawWarning(Frame& f, Warning w);
Rgb warningColor(Warning w);

// 8x8 pumpkin in the middle of the screen.
void drawBootPumpkin(Frame& f);

} // namespace pk
