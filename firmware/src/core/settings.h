// Every setting the dashboard can change, with defaults, limits, validation and
// JSON. The same JSON (plus secrets) is what gets stored on the clock, so
// adding a setting means: a field here, a line in toJson/patch, a default.
// Field names and limits follow dashboard/src/contract.ts.
#pragma once

#include <ArduinoJson.h>

#include "types.h"

namespace pk {

// ---------------------------------------------------------------- limits

constexpr int kGlucoseMin = 40, kGlucoseMax = 400;
constexpr int kStaleMin = 10, kStaleMax = 60;
constexpr int kRotationMin = 3, kRotationMax = 300;
constexpr int kBrightnessMin = 1, kBrightnessMax = 255;
constexpr int kNameMax = 32;
constexpr int kUrlMax = 255, kTokenMax = 255;
constexpr int kPasswordMin = 4, kPasswordMax = 64;

// ---------------------------------------------------------------- groups

struct DisplaySettings {
    struct {
        bool tir, iob, alerts, time, pet;
    } screens;
    struct {
        bool autoCycle;
        uint16_t seconds;
    } rotation;
    struct {
        Unit unit;
        uint16_t urgentLow, low, high, urgentHigh;  // mg/dL
        uint8_t staleAfterMin;
        bool lowLock;
        bool deltaFlash;
        ColorPreset colors;
    } glucose;
    struct {
        bool hour24;
        DateFormat date;
    } time;
    struct {
        PetId id;
        PetStyle style;
        bool showTir;
        bool seasonal;
    } pet;
    struct {
        bool replacePet;  // pump alerts take the pet's place instead of following it
    } alerts;
    struct {
        bool autoLevel;
        uint8_t level;
    } brightness;
};

struct AwakeHours {
    bool on;            // false: night all day
    uint16_t from, to;  // minutes since midnight, from < to <= 1440
};

struct ScheduleSettings {
    bool enabled;
    AwakeHours awake[7];  // 0 = Monday
    NightMode night;
    uint8_t nightBrightness;
};

struct SourceSettings {
    SourceKind kind;
    char bridgeUrl[kUrlMax + 1];
    char token[kTokenMax + 1];
};

struct WifiSettings {
    char ssid[33];
    WifiSecurity security;
    EapMethod eap;
    char password[128];  // PSK or 802.1X password
    char identity[128];
    char anonIdentity[128];
    bool validateCa;
};

struct DeviceSettings {
    char name[kNameMax + 1];  // "" = default "pumpkin-xxxx"
    char zone[48];            // IANA id, for the dashboard
    char posix[64];           // what the clock actually uses
    char password[kPasswordMax + 1];
};

struct Settings {
    DisplaySettings display;
    ScheduleSettings schedule;
    SourceSettings source;
    WifiSettings wifi;
    DeviceSettings device;
};

void setDefaults(Settings& s);
void setDefaults(DisplaySettings& d);
void setDefaults(ScheduleSettings& s);
void setDefaults(SourceSettings& s);
void setDefaults(WifiSettings& w);
void setDefaults(DeviceSettings& d);

// ---------------------------------------------------------------- errors

// What the API answers with: { "error": message, "field": field }.
struct FieldError {
    bool failed = false;
    char field[48] = "";
    char message[120] = "";
    void fail(const char* f, const char* msg);
    void failf(const char* f, const char* fmt, int a, int b);
};

// strict: an API request; unknown keys and wrong types are errors.
// lenient: loading from storage; anything odd is skipped and the default kept.
enum class Mode : uint8_t { Strict, Lenient };

// ---------------------------------------------------------------- JSON

// API shape (no secrets).
void toJson(const DisplaySettings& d, JsonObject out);
void toJson(const ScheduleSettings& s, JsonObject out);

// Apply a patch onto a copy and validate the result; `d` changes only on success.
bool patch(DisplaySettings& d, JsonObjectConst p, FieldError& err, Mode mode = Mode::Strict);
bool patch(ScheduleSettings& s, JsonObjectConst p, FieldError& err, Mode mode = Mode::Strict);

bool validate(const DisplaySettings& d, FieldError& err);
bool validate(const ScheduleSettings& s, FieldError& err);

// Storage shape: includes secrets. Never sent over the API.
void toStorage(const DisplaySettings& d, JsonObject out);
void toStorage(const ScheduleSettings& s, JsonObject out);
void toStorage(const SourceSettings& s, JsonObject out);
void toStorage(const WifiSettings& w, JsonObject out);
void toStorage(const DeviceSettings& d, JsonObject out);
void fromStorage(DisplaySettings& d, JsonObjectConst in);
void fromStorage(ScheduleSettings& s, JsonObjectConst in);
void fromStorage(SourceSettings& s, JsonObjectConst in);
void fromStorage(WifiSettings& w, JsonObjectConst in);
void fromStorage(DeviceSettings& d, JsonObjectConst in);

// ---------------------------------------------------------------- names

extern const EnumName<Unit> kUnitNames[2];
extern const EnumName<ColorPreset> kPresetNames[3];
extern const EnumName<PetId> kPetNames[8];
extern const EnumName<PetStyle> kPetStyleNames[3];
extern const EnumName<DateFormat> kDateNames[3];
extern const EnumName<NightMode> kNightNames[2];
extern const EnumName<SourceKind> kSourceKindNames[2];
extern const EnumName<WifiSecurity> kSecurityNames[2];
extern const EnumName<EapMethod> kEapNames[2];
extern const char* const kWeekdays[7];

// Helpers shared with the API.
bool isHttpsUrl(const char* url);
bool hostOfUrl(const char* url, char* out, size_t size);

} // namespace pk
