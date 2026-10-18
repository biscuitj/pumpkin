#include "glucose.h"

#include <stdio.h>

namespace pk {

namespace {
const PresetColors kClassic = {rgb(0xEA4335), rgb(0xFBBC04), rgb(0x34A853), rgb(0xFBBC04), rgb(0xEA4335), rgb(0x808080)};
// Low is warm, high is cool, in range is blue: no red/green pair to tell apart.
const PresetColors kColorblind = {rgb(0xFF3B1F), rgb(0xFF9F00), rgb(0x2D9CFF), rgb(0xB57BFF), rgb(0xE040FB), rgb(0x808080)};
// Only out-of-range readings get colour.
const PresetColors kCalm = {rgb(0xFF3020), rgb(0xFFB000), rgb(0xE8E8E8), rgb(0xFFB000), rgb(0xFF3020), rgb(0x606060)};
} // namespace

const PresetColors& presetColors(ColorPreset p) {
    switch (p) {
        case ColorPreset::Colorblind: return kColorblind;
        case ColorPreset::Calm: return kCalm;
        default: return kClassic;
    }
}

Rgb bandColor(Band b, ColorPreset p) {
    const PresetColors& c = presetColors(p);
    switch (b) {
        case Band::UrgentLow: return c.urgentLow;
        case Band::Low: return c.low;
        case Band::InRange: return c.inRange;
        case Band::High: return c.high;
        case Band::UrgentHigh: return c.urgentHigh;
        default: return c.stale;
    }
}

Band bandOf(int mgdl, const DisplaySettings& d) {
    const auto& g = d.glucose;
    if (mgdl < g.urgentLow) return Band::UrgentLow;
    if (mgdl < g.low) return Band::Low;
    if (mgdl > g.urgentHigh) return Band::UrgentHigh;
    if (mgdl > g.high) return Band::High;
    return Band::InRange;
}

namespace {
// mg/dL to mmol/L tenths without floating point: mg/dL / 18, rounded.
void format(char* out, size_t size, int mgdl, Unit unit, bool sign) {
    if (unit == Unit::MgDl) {
        snprintf(out, size, sign ? "%+d" : "%d", mgdl);
        return;
    }
    const long v = mgdl;
    const long mag = v < 0 ? -v : v;
    const long tenths = (mag * 5 + 4) / 9;
    const char* s = v < 0 ? "-" : sign ? "+" : "";
    snprintf(out, size, "%s%ld.%ld", s, tenths / 10, tenths % 10);
}
} // namespace

void formatGlucose(char* out, size_t size, int mgdl, Unit unit) { format(out, size, mgdl, unit, false); }
void formatDelta(char* out, size_t size, int d, Unit unit) { format(out, size, d, unit, true); }

namespace {
const EnumName<Trend> kTrendNames[] = {
    {Trend::Unknown, "NONE"},           {Trend::DoubleUp, "DoubleUp"},     {Trend::SingleUp, "SingleUp"},
    {Trend::FortyFiveUp, "FortyFiveUp"}, {Trend::Flat, "Flat"},             {Trend::FortyFiveDown, "FortyFiveDown"},
    {Trend::SingleDown, "SingleDown"},   {Trend::DoubleDown, "DoubleDown"},
};
} // namespace

Trend trendFromName(const char* name) {
    Trend t = Trend::Unknown;
    if (enumFromName(kTrendNames, name, t)) return t;
    // Older aliases some sources send.
    if (name) {
        if (!strcmp(name, "TripleUp")) return Trend::DoubleUp;
        if (!strcmp(name, "TripleDown")) return Trend::DoubleDown;
    }
    return Trend::Unknown;
}

const char* trendName(Trend t) { return enumToName(kTrendNames, t); }

} // namespace pk
