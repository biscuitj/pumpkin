// Glucose bands, colours, formatting and trends. Readings and thresholds stay
// in mg/dL everywhere; mmol/L exists only as text at the display edge.
#pragma once

#include "settings.h"
#include "types.h"

namespace pk {

// Range colours per preset, before brightness. Same table as COLOR_PRESETS in
// dashboard/src/contract.ts: change both together.
struct PresetColors {
    Rgb urgentLow, low, inRange, high, urgentHigh, stale;
};
const PresetColors& presetColors(ColorPreset p);
Rgb bandColor(Band b, ColorPreset p);

Band bandOf(int mgdl, const DisplaySettings& d);

// "142", "7.9"; deltas "+5", "-0.3".
void formatGlucose(char* out, size_t size, int mgdl, Unit unit);
void formatDelta(char* out, size_t size, int mgdlDelta, Unit unit);

// Nightscout/Dexcom direction names ("SingleUp", "FortyFiveDown", ...).
Trend trendFromName(const char* name);
const char* trendName(Trend t);

// Fixed colours on the clock (not glucose ranges).
constexpr Rgb kOrange = rgb(0xFF7518);   // Pumpkin
constexpr Rgb kCyan = rgb(0x26C6DA);     // active insulin
constexpr Rgb kWhite = rgb(0xFFFFFF);
constexpr Rgb kGrey = rgb(0x646464);
constexpr Rgb kTeal = rgb(0x00C8C8);     // setup and address messages
constexpr Rgb kRed = rgb(0xFF0000);
constexpr Rgb kAmber = rgb(0xFF8C00);
constexpr Rgb kYellow = rgb(0xFFFF00);

} // namespace pk
