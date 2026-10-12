#include "bridge_alerts.h"
#include "display.h"

#include <stdio.h>
#include <string.h>

namespace {

// 32x8 images, one word per row, bit 31 = column 0 (generated from the approved
// mockups in clock-visuals-overview.png).

// Pause: two 2x6 bars, 2 px apart.
static const uint32_t ICON_PAUSE[8] = {
    0x00000000UL, 0x00066000UL, 0x00066000UL, 0x00066000UL,
    0x00066000UL, 0x00066000UL, 0x00066000UL, 0x00000000UL};
// Battery: outline with the last sliver of charge and a 2x4 terminal.
static const uint32_t ICON_BATTERY[8] = {
    0x00FFFE00UL, 0x00800200UL, 0x00A00380UL, 0x00A00380UL,
    0x00A00380UL, 0x00A00380UL, 0x00800200UL, 0x00FFFE00UL};
// Reservoir: shorter battery with rounded corners towards the lid; terminal + 2x6 lid.
static const uint32_t ICON_RESERVOIR[8] = {
    0x00FFE000UL, 0x00801300UL, 0x00A01F00UL, 0x00A01F00UL,
    0x00A01F00UL, 0x00A01F00UL, 0x00801300UL, 0x00FFE000UL};
// Blood drop with a two-pixel diagonal reflection.
static const uint32_t ICON_BG[8] = {
    0x00010000UL, 0x00010000UL, 0x00038000UL, 0x0006C000UL,
    0x000DE000UL, 0x000FE000UL, 0x000FE000UL, 0x0007C000UL};
// Warning triangle with an unlit 2 px exclamation mark.
static const uint32_t ICON_ALERT[8] = {
    0x00018000UL, 0x0003C000UL, 0x00066000UL, 0x000E7000UL,
    0x001E7800UL, 0x003FFC00UL, 0x007E7E00UL, 0x00FFFF00UL};
// "SENSOR" in narrow 4x7 letters, so the whole word fits on one screen.
static const uint32_t ICON_SENSOR[8] = {
    0x3BD27338UL, 0x421A84A4UL, 0x421A84A4UL, 0x339664B8UL,
    0x0A1614A8UL, 0x0A1214A4UL, 0x73D2E324UL, 0x00000000UL};
// "SET" in the normal 5x7 font, centred.
static const uint32_t ICON_SET[8] = {
    0x00E7DF00UL, 0x01141500UL, 0x01040400UL, 0x00E78400UL,
    0x00140400UL, 0x01140400UL, 0x00E7C400UL, 0x00000000UL};

struct WarningDef {
    const char* id;
    const uint32_t* icon;
    uint8_t r, g, b;
};

static const WarningDef DEFS[WARN_COUNT] = {
    {"", nullptr, 0, 0, 0},
    {"suspended", ICON_PAUSE, 0xFF, 0x6D, 0x00},      // orange
    {"warning", ICON_ALERT, 0xFB, 0xBC, 0x04},        // amber
    {"battery", ICON_BATTERY, 0x64, 0xB5, 0xF6},      // light blue
    {"reservoir", ICON_RESERVOIR, 0x34, 0xA8, 0x53},  // green
    {"bg", ICON_BG, 0xEA, 0x43, 0x35},                // red
    {"sensor", ICON_SENSOR, 0xFF, 0xE0, 0x00},        // yellow
    {"set", ICON_SET, 0xFF, 0xE0, 0x00},              // yellow
};

} // namespace

uint8_t bridge_warning_from_id(const char* id) {
    if (!id) return WARN_NONE;
    for (uint8_t i = 1; i < WARN_COUNT; ++i) {
        if (strcmp(id, DEFS[i].id) == 0) return i;
    }
    return WARN_NONE;
}

const char* bridge_warning_id(uint8_t warning) {
    return warning < WARN_COUNT ? DEFS[warning].id : "";
}

uint16_t bridge_warning_color(uint8_t warning) {
    if (warning >= WARN_COUNT) warning = WARN_NONE;
    const WarningDef& d = DEFS[warning];
    return display_color(d.r, d.g, d.b);
}

bool bridge_warning_draw_icon(uint8_t warning) {
    if (warning == WARN_NONE || warning >= WARN_COUNT || !DEFS[warning].icon) return false;
    const uint32_t* rows = DEFS[warning].icon;
    const uint16_t color = bridge_warning_color(warning);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 32; ++x) {
            if (rows[y] & (1UL << (31 - x))) display_draw_pixel(x, y, color);
        }
    }
    return true;
}

bool bridge_reminders_clear_url(const char* clock_url, char* out, unsigned out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    if (!clock_url) return false;
    const char* query = strchr(clock_url, '?');
    const size_t path_len = query ? (size_t)(query - clock_url) : strlen(clock_url);
    static const char SUFFIX[] = "/clock";
    const size_t suffix_len = sizeof(SUFFIX) - 1;
    if (path_len <= suffix_len || strncmp(clock_url + path_len - suffix_len, SUFFIX, suffix_len) != 0) {
        return false;
    }
    const int base_len = (int)(path_len - suffix_len);
    const int n = snprintf(out, out_size, "%.*s/reminders/clear?id=all", base_len, clock_url);
    if (n < 0 || (unsigned)n >= out_size) {
        out[0] = '\0';
        return false;
    }
    return true;
}
