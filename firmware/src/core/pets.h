// Pet artwork. petFrame() fills a 32x8 grid of palette keys ('.' = off);
// petPixelColor() turns a key into a colour.
#pragma once

#include "types.h"

namespace pk {

// range: 0 in range, 1 low, 2 high. sleepy: night; happy: waving hello.
void petFrame(PetId id, uint32_t ms, bool sleepy, bool happy, PetStyle style, int range, char (&pixels)[8][32]);

// 'a' / 'i' / 'r' (LOW, OK, HIGH status pixels) use the given colours.
uint32_t petPixelColor(char key, const Rgb& low, const Rgb& inRange, const Rgb& high);

} // namespace pk
