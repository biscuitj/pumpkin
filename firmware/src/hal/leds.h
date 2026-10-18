// The 32x8 matrix.
#pragma once

#include "core/frame.h"

namespace hal {

void ledsBegin();
// `level` 0-255 scales the whole frame (brightness x fade); 0 = dark.
void ledsShow(const pk::Frame& frame, uint8_t level);

} // namespace hal
