#include "frame.h"

#include "font.h"

namespace pk {

int textWidth(const char* text) { return text ? (int)strlen(text) * 6 : 0; }

int drawText(Frame& f, const char* text, int x, int y, Rgb c) {
    if (!text) return x;
    for (const char* p = text; *p; ++p, x += 6) {
        unsigned char ch = (unsigned char)*p;
        if (ch < 0x20 || ch > 0x7E) ch = '?';
        if (x > kWidth || x < -6) continue;
        const uint8_t* cols = FONT5X7[ch - 0x20];
        for (int cx = 0; cx < 5; ++cx) {
            for (int cy = 0; cy < 8; ++cy) {  // row 7: descenders (g, j, p, q, y)
                if (cols[cx] & (1 << cy)) f.set(x + cx, y + cy, c);
            }
        }
    }
    return x;
}

void drawCentered(Frame& f, const char* text, Rgb c) {
    const int w = textWidth(text) - 1;
    drawText(f, text, (kWidth - w) / 2, 0, c);
}

void drawRight(Frame& f, const char* text, Rgb c) {
    const int w = textWidth(text) - 1;
    drawText(f, text, kWidth - w, 0, c);
}

void drawBitmap5x7(Frame& f, const uint8_t rows[7], int x, int y, Rgb c) {
    for (int r = 0; r < 7; ++r)
        for (int col = 0; col < 5; ++col)
            if (rows[r] & (1 << (4 - col))) f.set(x + col, y + r, c);
}

uint32_t scrollDurationMs(const char* text, uint32_t stepMs) {
    return (uint32_t)(textWidth(text) + kWidth) * stepMs;
}

bool drawScroll(Frame& f, const char* text, Rgb c, uint32_t elapsedMs, uint32_t stepMs) {
    const uint32_t span = (uint32_t)(textWidth(text) + kWidth);
    if (stepMs == 0) stepMs = 1;
    const uint32_t steps = elapsedMs / stepMs;
    const int offset = (int)(steps % span);
    drawText(f, text, kWidth - offset, 0, c);
    return steps >= span;
}

} // namespace pk
