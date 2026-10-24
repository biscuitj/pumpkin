// Shared small types for the Pumpkin core. Everything under src/core is plain
// C++ (no Arduino, no ESP-IDF), so it runs in the host tests and the simulator.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace pk {

struct Rgb {
    uint8_t r, g, b;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Rgb& o) const { return !(*this == o); }
};

constexpr Rgb rgb(uint32_t hex) {
    return Rgb{uint8_t((hex >> 16) & 0xFF), uint8_t((hex >> 8) & 0xFF), uint8_t(hex & 0xFF)};
}

enum class Unit : uint8_t { MgDl, Mmol };
enum class ColorPreset : uint8_t { Classic, Colorblind, Calm };
enum class PetId : uint8_t { Patch, Toffee, Kiko, Domino, Hoot, Leo, Stella, Kobi, Count };
enum class PetStyle : uint8_t { Text, Icon, PetOnly };
enum class DateFormat : uint8_t { Off, DayMonth, MonthDay };
enum class NightMode : uint8_t { Dim, Mute };
enum class SourceKind : uint8_t { Bridge, Demo };
enum class WifiSecurity : uint8_t { Personal, Enterprise };
enum class EapMethod : uint8_t { Peap, Ttls };

enum class Band : uint8_t { UrgentLow, Low, InRange, High, UrgentHigh, Stale };

enum class Trend : uint8_t { DoubleUp, SingleUp, FortyFiveUp, Flat, FortyFiveDown, SingleDown, DoubleDown, Unknown };

// Pump alerts the bridge sends in "warnings" (the bridge sets the order).
// Sensor: runs out soon (yellow). SensorChange: the pump's replace-sensor alert (red).
enum class Warning : uint8_t { None, Suspended, Alert, Battery, Reservoir, Bg, Sensor, Set, SensorChange, Count };

// Screens in rotation order (contract SCREENS), plus the ones the engine
// shows by itself. Names, never numbers, go over the API.
enum class Screen : uint8_t {
    Glucose, Tir, Iob, Alerts, Time, Pet,           // rotation
    Message, BridgeAlert,                           // from the bridge, in the rotation while present
    Boot, Status, Update, Address                   // engine overlays
};

// Copy a C string into a fixed buffer, always terminated.
inline void copyStr(char* dst, size_t size, const char* src) {
    if (!dst || size == 0) return;
    size_t i = 0;
    if (src) {
        for (; i + 1 < size && src[i]; ++i) dst[i] = src[i];
    }
    dst[i] = '\0';
}

template <size_t N>
inline void copyStr(char (&dst)[N], const char* src) { copyStr(dst, N, src); }

// Name tables for enums that travel as JSON strings.
template <typename E>
struct EnumName {
    E value;
    const char* name;
};

template <typename E, size_t N>
const char* enumToName(const EnumName<E> (&table)[N], E v) {
    for (const auto& e : table) if (e.value == v) return e.name;
    return table[0].name;
}

template <typename E, size_t N>
bool enumFromName(const EnumName<E> (&table)[N], const char* name, E& out) {
    if (!name) return false;
    for (const auto& e : table) {
        if (strcmp(e.name, name) == 0) {
            out = e.value;
            return true;
        }
    }
    return false;
}

} // namespace pk
