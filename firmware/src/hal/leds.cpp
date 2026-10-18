#include "leds.h"

#include <FastLED.h>

#include "pins.h"

namespace hal {

namespace {

CRGB leds[pk::kWidth * pk::kHeight];

// Gamma correction, the same as the old firmware got from Framebuffer_GFX /
// Adafruit_NeoMatrix (gamma.h): colours are quantized to RGB565 and looked up.
// Without it, mid-level channels are far too bright on WS2812s and hues shift
// (amber turns yellow, orange washes out). The mirror keeps the uncorrected
// frame: that's what looks right in a browser.
const uint8_t kGamma5[32] = {0x00, 0x01, 0x02, 0x03, 0x05, 0x07, 0x09, 0x0b, 0x0e, 0x11, 0x14,
                             0x18, 0x1d, 0x22, 0x28, 0x2e, 0x36, 0x3d, 0x46, 0x4f, 0x59, 0x64,
                             0x6f, 0x7c, 0x89, 0x97, 0xa6, 0xb6, 0xc7, 0xd9, 0xeb, 0xff};
const uint8_t kGamma6[64] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x08, 0x09, 0x0a, 0x0b, 0x0d, 0x0e,
                             0x10, 0x12, 0x13, 0x15, 0x17, 0x19, 0x1b, 0x1d, 0x20, 0x22, 0x25, 0x27, 0x2a,
                             0x2d, 0x30, 0x33, 0x37, 0x3a, 0x3e, 0x41, 0x45, 0x49, 0x4d, 0x52, 0x56, 0x5b,
                             0x5f, 0x64, 0x69, 0x6e, 0x74, 0x79, 0x7f, 0x85, 0x8b, 0x91, 0x97, 0x9d, 0xa4,
                             0xab, 0xb2, 0xb9, 0xc0, 0xc7, 0xcf, 0xd6, 0xde, 0xe6, 0xee, 0xf7, 0xff};

} // namespace

void ledsBegin() {
    FastLED.addLeds<WS2812B, pins::kMatrix, GRB>(leds, pk::kWidth * pk::kHeight);
    // Brightness is applied per show() call; rapid setBrightness() writes can
    // make some WS2812 matrices sparkle.
    FastLED.setBrightness(255);
    FastLED.setDither(DISABLE_DITHER);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 2000);
    FastLED.clear(true);
}

void ledsShow(const pk::Frame& frame, uint8_t level) {
    for (int y = 0; y < pk::kHeight; ++y) {
        for (int x = 0; x < pk::kWidth; ++x) {
            const uint8_t* p = frame.rgb + (y * pk::kWidth + x) * 3;
            // Odd rows run right to left.
            const int i = y * pk::kWidth + ((y & 1) ? pk::kWidth - 1 - x : x);
            leds[i] = CRGB(kGamma5[p[0] >> 3], kGamma6[p[1] >> 2], kGamma5[p[2] >> 3]);
        }
    }
    FastLED.show(level);
}

} // namespace hal
