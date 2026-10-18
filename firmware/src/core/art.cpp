#include "art.h"

namespace pk {

namespace {

// One byte per row, bit 4 = leftmost of 5 columns.
const uint8_t kArrows[7][7] = {
    {0b01010, 0b11111, 0b01010, 0b01010, 0b01010, 0b01010, 0b01010},  // DoubleUp
    {0b00100, 0b01110, 0b10101, 0b00100, 0b00100, 0b00100, 0b00100},  // SingleUp
    {0b11111, 0b00011, 0b00101, 0b01001, 0b10001, 0b00000, 0b00000},  // FortyFiveUp
    {0b00000, 0b00100, 0b00010, 0b11111, 0b00010, 0b00100, 0b00000},  // Flat
    {0b00000, 0b00000, 0b10001, 0b01001, 0b00101, 0b00011, 0b11111},  // FortyFiveDown
    {0b00100, 0b00100, 0b00100, 0b00100, 0b10101, 0b01110, 0b00100},  // SingleDown
    {0b01010, 0b01010, 0b01010, 0b01010, 0b01010, 0b11111, 0b01010},  // DoubleDown
};

// 32x8 images, one word per row, bit 31 = column 0 (from the approved mockups).
const uint32_t kPause[8] = {0x00000000, 0x00066000, 0x00066000, 0x00066000, 0x00066000, 0x00066000, 0x00066000, 0x00000000};
const uint32_t kBattery[8] = {0x00FFFE00, 0x00800200, 0x00A00380, 0x00A00380, 0x00A00380, 0x00A00380, 0x00800200, 0x00FFFE00};
const uint32_t kReservoir[8] = {0x00FFE000, 0x00801300, 0x00A01F00, 0x00A01F00, 0x00A01F00, 0x00A01F00, 0x00801300, 0x00FFE000};
const uint32_t kBloodDrop[8] = {0x00010000, 0x00010000, 0x00038000, 0x0006C000, 0x000DE000, 0x000FE000, 0x000FE000, 0x0007C000};
const uint32_t kTriangle[8] = {0x00018000, 0x0003C000, 0x00066000, 0x000E7000, 0x001E7800, 0x003FFC00, 0x007E7E00, 0x00FFFF00};
const uint32_t kSensor[8] = {0x3BD27338, 0x421A84A4, 0x421A84A4, 0x339664B8, 0x0A1614A8, 0x0A1214A4, 0x73D2E324, 0x00000000};
const uint32_t kSet[8] = {0x00E7DF00, 0x01141500, 0x01040400, 0x00E78400, 0x00140400, 0x01140400, 0x00E7C400, 0x00000000};

struct WarningArt {
    const uint32_t* rows;
    uint32_t color;
};

const WarningArt kWarningArt[(int)Warning::Count] = {
    {nullptr, 0},
    {kPause, 0xFF6D00},      // suspended: orange
    {kTriangle, 0xFBBC04},   // warning: amber
    {kBattery, 0x64B5F6},    // battery: light blue
    {kReservoir, 0x34A853},  // reservoir: green
    {kBloodDrop, 0xEA4335},  // bg: red
    {kSensor, 0xFFE000},     // sensor: yellow
    {kSet, 0xFFE000},        // set: yellow
};

} // namespace

void drawTrend(Frame& f, Trend t, int x, Rgb c) {
    if (t == Trend::Unknown || (int)t > 6) return;
    drawBitmap5x7(f, kArrows[(int)t], x, 0, c);
}

Rgb warningColor(Warning w) {
    if ((int)w <= 0 || (int)w >= (int)Warning::Count) return Rgb{0, 0, 0};
    return rgb(kWarningArt[(int)w].color);
}

bool drawWarning(Frame& f, Warning w) {
    if ((int)w <= 0 || (int)w >= (int)Warning::Count) return false;
    const WarningArt& a = kWarningArt[(int)w];
    const Rgb c = rgb(a.color);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 32; ++x)
            if (a.rows[y] & (1UL << (31 - x))) f.set(x, y, c);
    return true;
}

void drawBootPumpkin(Frame& f) {
    static const char* const kRows[8] = {
        "...gg...", "..oOOo..", ".oOooOo.", "ooOooOoo", "ooOooOoo", "ooOooOoo", ".oOooOo.", "..oooo..",
    };
    const Rgb orange = rgb(0xFF7518), ridge = rgb(0xC84600), stem = rgb(0x3CA028);
    const int x0 = (kWidth - 8) / 2;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const char c = kRows[y][x];
            if (c != '.') f.set(x0 + x, y, c == 'g' ? stem : c == 'O' ? ridge : orange);
        }
}

} // namespace pk
