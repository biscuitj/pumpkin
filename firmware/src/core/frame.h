// The 32x8 picture: RGB per pixel, row by row (the mirror's wire format), plus
// text in the clock's 5x7 font.
#pragma once

#include "types.h"

namespace pk {

constexpr int kWidth = 32;
constexpr int kHeight = 8;
constexpr int kFrameBytes = kWidth * kHeight * 3;

struct Frame {
    uint8_t rgb[kFrameBytes];

    void clear() { memset(rgb, 0, sizeof(rgb)); }
    void set(int x, int y, Rgb c) {
        if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
        uint8_t* p = rgb + (y * kWidth + x) * 3;
        p[0] = c.r;
        p[1] = c.g;
        p[2] = c.b;
    }
    Rgb get(int x, int y) const {
        if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return Rgb{0, 0, 0};
        const uint8_t* p = rgb + (y * kWidth + x) * 3;
        return Rgb{p[0], p[1], p[2]};
    }
    bool operator==(const Frame& o) const { return memcmp(rgb, o.rgb, sizeof(rgb)) == 0; }
};

// Width of `text` in pixels: 6 per glyph (5 + 1 space), trailing space included.
int textWidth(const char* text);

// Draws `text` at (x, y); returns the x after the last glyph's spacing.
int drawText(Frame& f, const char* text, int x, int y, Rgb c);

// Centered without the trailing glyph space.
void drawCentered(Frame& f, const char* text, Rgb c);

// Right-aligned on the last column.
void drawRight(Frame& f, const char* text, Rgb c);

// A 5-wide, 7-high bitmap, one byte per row, bit 4 = leftmost pixel.
void drawBitmap5x7(Frame& f, const uint8_t rows[7], int x, int y, Rgb c);

// Scrolling text from the right edge to past the left one. `elapsedMs` since
// the scroll started; one pixel per `stepMs`. Returns true once it has gone
// through at least once (it keeps looping after that).
bool drawScroll(Frame& f, const char* text, Rgb c, uint32_t elapsedMs, uint32_t stepMs = 55);
uint32_t scrollDurationMs(const char* text, uint32_t stepMs = 55);

} // namespace pk
