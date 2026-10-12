#ifndef BRIDGE_ALERTS_H
#define BRIDGE_ALERTS_H

#include <stdint.h>

// Pump warnings sent by the sugarclock-bridge in "warnings": ["battery", ...].
// The bridge decides when a warning applies; the clock decides how it looks.
enum BridgeWarning : uint8_t {
    WARN_NONE = 0,
    WARN_SUSPENDED,  // "suspended": pump stopped by hand (not a SmartGuard auto-suspend)
    WARN_ALERT,      // "warning":   calibration, sensor signal, set connect, delivery stopped,
                     //              SmartGuard auto-suspend
    WARN_BATTERY,    // "battery":   pump battery low / replace now
    WARN_RESERVOIR,  // "reservoir": reservoir low / empty
    WARN_BG,         // "bg":        BG required
    WARN_SENSOR,     // "sensor":    change sensor
    WARN_SET,        // "set":       set change reminder
    WARN_COUNT
};

#define BRIDGE_MAX_WARNINGS 7
#define BRIDGE_WARNING_ICON_MS 5000UL

// "battery" -> WARN_BATTERY etc.; WARN_NONE for null/unknown ids.
uint8_t bridge_warning_from_id(const char* id);

// Inverse of bridge_warning_from_id ("" for WARN_NONE / out of range).
const char* bridge_warning_id(uint8_t warning);

// Draw a warning's 32x8 image into the frame buffer (no clear/show). False for
// WARN_NONE / unknown values.
bool bridge_warning_draw_icon(uint8_t warning);

// Colour of a warning's image (RGB565).
uint16_t bridge_warning_color(uint8_t warning);

// The bridge's reminder-clear URL derived from its clock URL:
// "https://x.workers.dev/clock" -> "https://x.workers.dev/reminders/clear?id=all".
// False (out = "") when the URL does not end in /clock (query string allowed),
// i.e. the clock is not pointed at a sugarclock-bridge.
bool bridge_reminders_clear_url(const char* clock_url, char* out, unsigned out_size);

#endif // BRIDGE_ALERTS_H
