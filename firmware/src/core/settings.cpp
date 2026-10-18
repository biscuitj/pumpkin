#include "settings.h"

#include <stdio.h>

namespace pk {

// ---------------------------------------------------------------- names

const EnumName<Unit> kUnitNames[2] = {{Unit::MgDl, "mgdl"}, {Unit::Mmol, "mmol"}};
const EnumName<ColorPreset> kPresetNames[3] = {
    {ColorPreset::Classic, "classic"}, {ColorPreset::Colorblind, "colorblind"}, {ColorPreset::Calm, "calm"}};
const EnumName<PetId> kPetNames[8] = {
    {PetId::Patch, "patch"}, {PetId::Toffee, "toffee"}, {PetId::Kiko, "kiko"},     {PetId::Domino, "domino"},
    {PetId::Hoot, "hoot"},   {PetId::Leo, "leo"},       {PetId::Stella, "stella"}, {PetId::Kobi, "kobi"}};
const EnumName<PetStyle> kPetStyleNames[3] = {
    {PetStyle::Text, "text"}, {PetStyle::Icon, "icon"}, {PetStyle::PetOnly, "pet_only"}};
const EnumName<DateFormat> kDateNames[3] = {
    {DateFormat::Off, "off"}, {DateFormat::DayMonth, "day_month"}, {DateFormat::MonthDay, "month_day"}};
const EnumName<NightMode> kNightNames[2] = {{NightMode::Dim, "dim"}, {NightMode::Mute, "mute"}};
const EnumName<SourceKind> kSourceKindNames[2] = {{SourceKind::Bridge, "bridge"}, {SourceKind::Demo, "demo"}};
const EnumName<WifiSecurity> kSecurityNames[2] = {
    {WifiSecurity::Personal, "personal"}, {WifiSecurity::Enterprise, "enterprise"}};
const EnumName<EapMethod> kEapNames[2] = {{EapMethod::Peap, "peap"}, {EapMethod::Ttls, "ttls"}};
const char* const kWeekdays[7] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};

// ---------------------------------------------------------------- defaults

void setDefaults(DisplaySettings& d) {
    d.screens = {true, true, true, true, true};
    d.rotation = {true, 10};
    d.glucose.unit = Unit::MgDl;
    d.glucose.urgentLow = 55;
    d.glucose.low = 70;
    d.glucose.high = 180;
    d.glucose.urgentHigh = 250;
    d.glucose.staleAfterMin = 20;
    d.glucose.lowLock = true;
    d.glucose.deltaFlash = true;
    d.glucose.colors = ColorPreset::Classic;
    d.time = {true, DateFormat::DayMonth};
    d.pet = {PetId::Patch, PetStyle::Text, false, true};
    d.alerts = {false};
    d.brightness = {true, 40};
}

void setDefaults(ScheduleSettings& s) {
    s.enabled = false;
    for (auto& a : s.awake) a = {true, 7 * 60, 21 * 60};
    s.night = NightMode::Dim;
    s.nightBrightness = 10;
}

void setDefaults(SourceSettings& s) {
    s.kind = SourceKind::Bridge;
    s.bridgeUrl[0] = '\0';
    s.token[0] = '\0';
}

void setDefaults(WifiSettings& w) {
    memset(&w, 0, sizeof(w));
    w.security = WifiSecurity::Personal;
    w.eap = EapMethod::Peap;
}

void setDefaults(DeviceSettings& d) {
    memset(&d, 0, sizeof(d));
    copyStr(d.zone, "Europe/Brussels");
    copyStr(d.posix, "CET-1CEST,M3.5.0,M10.5.0/3");
}

void setDefaults(Settings& s) {
    setDefaults(s.display);
    setDefaults(s.schedule);
    setDefaults(s.source);
    setDefaults(s.wifi);
    setDefaults(s.device);
}

// ---------------------------------------------------------------- errors

void FieldError::fail(const char* f, const char* msg) {
    if (failed) return;  // keep the first problem
    failed = true;
    copyStr(field, f);
    copyStr(message, msg);
}

void FieldError::failf(const char* f, const char* fmt, int a, int b) {
    char buf[sizeof(message)];
    snprintf(buf, sizeof(buf), fmt, a, b);
    fail(f, buf);
}

// ---------------------------------------------------------------- reading JSON

namespace {

// Reads one JSON object with a known set of keys. In strict mode anything
// unexpected is an error naming "prefix.key"; in lenient mode it's skipped.
class Reader {
public:
    Reader(JsonObjectConst obj, const char* prefix, FieldError& err, Mode mode)
        : obj_(obj), prefix_(prefix), err_(err), strict_(mode == Mode::Strict) {}

    // Every key in the object must be one of `keys`.
    void onlyKeys(const char* const* keys, size_t n) {
        if (!strict_) return;
        for (JsonPairConst kv : obj_) {
            bool ok = false;
            for (size_t i = 0; i < n && !ok; ++i) ok = strcmp(kv.key().c_str(), keys[i]) == 0;
            if (!ok) {
                char f[48];
                field(f, sizeof(f), kv.key().c_str());
                char msg[96];
                snprintf(msg, sizeof(msg), "Unknown setting %s.", f);
                err_.fail(f, msg);
                return;
            }
        }
    }

    void boolean(const char* key, bool& v) {
        JsonVariantConst x = obj_[key];
        if (x.isNull()) return;
        if (x.is<bool>()) v = x.as<bool>();
        else wrongType(key);
    }

    // Integers only; the range is checked by validate(), except where the
    // storage type can't hold the value at all.
    template <typename T>
    void integer(const char* key, T& v, long lo, long hi) {
        JsonVariantConst x = obj_[key];
        if (x.isNull()) return;
        if (!x.is<long>()) return wrongType(key);
        long n = x.as<long>();
        if (n < lo || n > hi) {
            if (!strict_) return;
            char f[48];
            field(f, sizeof(f), key);
            err_.failf(f, "Use %d to %d.", (int)lo, (int)hi);
            return;
        }
        v = static_cast<T>(n);
    }

    template <typename E, size_t N>
    void enumeration(const char* key, E& v, const EnumName<E> (&table)[N]) {
        JsonVariantConst x = obj_[key];
        if (x.isNull()) return;
        if (!x.is<const char*>()) return wrongType(key);
        E parsed;
        if (enumFromName(table, x.as<const char*>(), parsed)) {
            v = parsed;
        } else if (strict_) {
            char f[48];
            field(f, sizeof(f), key);
            char msg[96];
            snprintf(msg, sizeof(msg), "Unknown value for %s.", f);
            err_.fail(f, msg);
        }
    }

    void string(const char* key, char* buf, size_t size) {
        JsonVariantConst x = obj_[key];
        if (x.isNull()) return;
        if (!x.is<const char*>()) return wrongType(key);
        const char* s = x.as<const char*>();
        if (strlen(s) >= size) {
            if (!strict_) return;
            char f[48];
            field(f, sizeof(f), key);
            err_.failf(f, "Use at most %d characters.", (int)size - 1, 0);
            return;
        }
        copyStr(buf, size, s);
    }

    JsonVariantConst operator[](const char* key) const { return obj_[key]; }

    void field(char* out, size_t size, const char* key) const {
        if (prefix_ && prefix_[0]) snprintf(out, size, "%s.%s", prefix_, key);
        else snprintf(out, size, "%s", key);
    }

private:
    void wrongType(const char* key) {
        if (!strict_) return;
        char f[48];
        field(f, sizeof(f), key);
        char msg[96];
        snprintf(msg, sizeof(msg), "%s has the wrong type.", f);
        err_.fail(f, msg);
    }

    JsonObjectConst obj_;
    const char* prefix_;
    FieldError& err_;
    bool strict_;
};

#define KEYS(...)                                        \
    do {                                                 \
        static const char* const k_[] = {__VA_ARGS__};   \
        r.onlyKeys(k_, sizeof(k_) / sizeof(k_[0]));      \
    } while (0)

// Each group of the display settings: { "screens": {...}, "glucose": {...}, ... }
bool groupObject(JsonObjectConst p, const char* group, JsonObjectConst& out, FieldError& err, Mode mode) {
    JsonVariantConst v = p[group];
    if (v.isNull()) return false;
    if (!v.is<JsonObjectConst>()) {
        if (mode == Mode::Strict) {
            char msg[96];
            snprintf(msg, sizeof(msg), "%s has the wrong type.", group);
            err.fail(group, msg);
        }
        return false;
    }
    out = v.as<JsonObjectConst>();
    return true;
}

void readDisplay(DisplaySettings& d, JsonObjectConst p, FieldError& err, Mode mode) {
    if (mode == Mode::Strict) {
        static const char* const groups[] = {"screens", "rotation", "glucose", "time", "pet", "alerts", "brightness"};
        Reader top(p, "", err, mode);
        top.onlyKeys(groups, sizeof(groups) / sizeof(groups[0]));
    }
    JsonObjectConst g;
    if (groupObject(p, "screens", g, err, mode)) {
        Reader r(g, "screens", err, mode);
        KEYS("tir", "iob", "alerts", "time", "pet");
        r.boolean("tir", d.screens.tir);
        r.boolean("iob", d.screens.iob);
        r.boolean("alerts", d.screens.alerts);
        r.boolean("time", d.screens.time);
        r.boolean("pet", d.screens.pet);
    }
    if (groupObject(p, "rotation", g, err, mode)) {
        Reader r(g, "rotation", err, mode);
        KEYS("auto", "seconds");
        r.boolean("auto", d.rotation.autoCycle);
        r.integer("seconds", d.rotation.seconds, 0, 65535);
    }
    if (groupObject(p, "glucose", g, err, mode)) {
        Reader r(g, "glucose", err, mode);
        KEYS("unit", "urgent_low", "low", "high", "urgent_high", "stale_after_min", "low_lock", "delta_flash", "colors");
        r.enumeration("unit", d.glucose.unit, kUnitNames);
        r.integer("urgent_low", d.glucose.urgentLow, 0, 65535);
        r.integer("low", d.glucose.low, 0, 65535);
        r.integer("high", d.glucose.high, 0, 65535);
        r.integer("urgent_high", d.glucose.urgentHigh, 0, 65535);
        r.integer("stale_after_min", d.glucose.staleAfterMin, 0, 255);
        r.boolean("low_lock", d.glucose.lowLock);
        r.boolean("delta_flash", d.glucose.deltaFlash);
        r.enumeration("colors", d.glucose.colors, kPresetNames);
    }
    if (groupObject(p, "time", g, err, mode)) {
        Reader r(g, "time", err, mode);
        KEYS("hour24", "date");
        r.boolean("hour24", d.time.hour24);
        r.enumeration("date", d.time.date, kDateNames);
    }
    if (groupObject(p, "pet", g, err, mode)) {
        Reader r(g, "pet", err, mode);
        KEYS("id", "style", "show_tir", "seasonal");
        r.enumeration("id", d.pet.id, kPetNames);
        r.enumeration("style", d.pet.style, kPetStyleNames);
        r.boolean("show_tir", d.pet.showTir);
        r.boolean("seasonal", d.pet.seasonal);
    }
    if (groupObject(p, "alerts", g, err, mode)) {
        Reader r(g, "alerts", err, mode);
        KEYS("replace_pet");
        r.boolean("replace_pet", d.alerts.replacePet);
    }
    if (groupObject(p, "brightness", g, err, mode)) {
        Reader r(g, "brightness", err, mode);
        KEYS("auto", "level");
        r.boolean("auto", d.brightness.autoLevel);
        r.integer("level", d.brightness.level, 0, 255);
    }
}

void readSchedule(ScheduleSettings& s, JsonObjectConst p, FieldError& err, Mode mode) {
    Reader r(p, "", err, mode);
    KEYS("enabled", "awake", "night", "night_brightness");
    r.boolean("enabled", s.enabled);
    r.enumeration("night", s.night, kNightNames);
    r.integer("night_brightness", s.nightBrightness, 0, 255);

    JsonVariantConst awake = p["awake"];
    if (awake.isNull()) return;
    if (!awake.is<JsonObjectConst>()) {
        if (mode == Mode::Strict) err.fail("awake", "awake has the wrong type.");
        return;
    }
    Reader days(awake.as<JsonObjectConst>(), "awake", err, mode);
    days.onlyKeys(kWeekdays, 7);
    for (int i = 0; i < 7; ++i) {
        JsonVariantConst day = awake[kWeekdays[i]];
        char f[24];
        snprintf(f, sizeof(f), "awake.%s", kWeekdays[i]);
        if (day.isUnbound()) continue;  // key absent: unchanged
        if (day.isNull()) {             // explicit null: night all day
            s.awake[i].on = false;
            continue;
        }
        if (!day.is<JsonObjectConst>()) {
            if (mode == Mode::Strict) err.fail(f, "Every day needs hours or null.");
            continue;
        }
        JsonVariantConst from = day["from"], to = day["to"];
        if (!from.is<long>() || !to.is<long>()) {
            if (mode == Mode::Strict) err.fail(f, "Every day needs hours or null.");
            continue;
        }
        long a = from.as<long>(), b = to.as<long>();
        if (a < 0 || a > 1440 || b < 0 || b > 1440) {
            if (mode == Mode::Strict) err.fail(f, "Times run from 00:00 to 24:00.");
            continue;
        }
        s.awake[i] = {true, (uint16_t)a, (uint16_t)b};
    }
}

} // namespace

// ---------------------------------------------------------------- validation

bool validate(const DisplaySettings& d, FieldError& err) {
    const auto& g = d.glucose;
    const struct {
        const char* f;
        int v;
    } th[] = {{"glucose.urgent_low", g.urgentLow}, {"glucose.low", g.low}, {"glucose.high", g.high}, {"glucose.urgent_high", g.urgentHigh}};
    for (const auto& t : th) {
        if (t.v < kGlucoseMin || t.v > kGlucoseMax) {
            err.failf(t.f, "Use a value from %d to %d mg/dL.", kGlucoseMin, kGlucoseMax);
            return false;
        }
    }
    if (!(g.urgentLow < g.low)) return err.fail("glucose.urgent_low", "Urgent low has to be below low."), false;
    if (!(g.low < g.high)) return err.fail("glucose.low", "Low has to be below high."), false;
    if (!(g.high < g.urgentHigh)) return err.fail("glucose.urgent_high", "Urgent high has to be above high."), false;
    if (g.staleAfterMin < kStaleMin || g.staleAfterMin > kStaleMax)
        return err.failf("glucose.stale_after_min", "Use %d to %d minutes.", kStaleMin, kStaleMax), false;
    if (d.rotation.seconds < kRotationMin || d.rotation.seconds > kRotationMax)
        return err.failf("rotation.seconds", "Use %d to %d seconds.", kRotationMin, kRotationMax), false;
    if (d.brightness.level < kBrightnessMin)
        return err.failf("brightness.level", "Use %d to %d.", kBrightnessMin, kBrightnessMax), false;
    return true;
}

bool validate(const ScheduleSettings& s, FieldError& err) {
    for (int i = 0; i < 7; ++i) {
        const AwakeHours& a = s.awake[i];
        if (!a.on) continue;
        if (a.from >= a.to) {
            char f[24];
            snprintf(f, sizeof(f), "awake.%s", kWeekdays[i]);
            err.fail(f, "The end has to be after the start.");
            return false;
        }
    }
    if (s.nightBrightness < kBrightnessMin)
        return err.failf("night_brightness", "Use %d to %d.", kBrightnessMin, kBrightnessMax), false;
    return true;
}

// ---------------------------------------------------------------- patch

bool patch(DisplaySettings& d, JsonObjectConst p, FieldError& err, Mode mode) {
    DisplaySettings next = d;
    readDisplay(next, p, err, mode);
    if (err.failed || !validate(next, err)) return false;
    d = next;
    return true;
}

bool patch(ScheduleSettings& s, JsonObjectConst p, FieldError& err, Mode mode) {
    ScheduleSettings next = s;
    readSchedule(next, p, err, mode);
    if (err.failed || !validate(next, err)) return false;
    s = next;
    return true;
}

// ---------------------------------------------------------------- writing JSON

void toJson(const DisplaySettings& d, JsonObject out) {
    JsonObject s = out["screens"].to<JsonObject>();
    s["tir"] = d.screens.tir;
    s["iob"] = d.screens.iob;
    s["alerts"] = d.screens.alerts;
    s["time"] = d.screens.time;
    s["pet"] = d.screens.pet;

    JsonObject r = out["rotation"].to<JsonObject>();
    r["auto"] = d.rotation.autoCycle;
    r["seconds"] = d.rotation.seconds;

    JsonObject g = out["glucose"].to<JsonObject>();
    g["unit"] = enumToName(kUnitNames, d.glucose.unit);
    g["urgent_low"] = d.glucose.urgentLow;
    g["low"] = d.glucose.low;
    g["high"] = d.glucose.high;
    g["urgent_high"] = d.glucose.urgentHigh;
    g["stale_after_min"] = d.glucose.staleAfterMin;
    g["low_lock"] = d.glucose.lowLock;
    g["delta_flash"] = d.glucose.deltaFlash;
    g["colors"] = enumToName(kPresetNames, d.glucose.colors);

    JsonObject t = out["time"].to<JsonObject>();
    t["hour24"] = d.time.hour24;
    t["date"] = enumToName(kDateNames, d.time.date);

    JsonObject p = out["pet"].to<JsonObject>();
    p["id"] = enumToName(kPetNames, d.pet.id);
    p["style"] = enumToName(kPetStyleNames, d.pet.style);
    p["show_tir"] = d.pet.showTir;
    p["seasonal"] = d.pet.seasonal;

    JsonObject a = out["alerts"].to<JsonObject>();
    a["replace_pet"] = d.alerts.replacePet;

    JsonObject b = out["brightness"].to<JsonObject>();
    b["auto"] = d.brightness.autoLevel;
    b["level"] = d.brightness.level;
}

void toJson(const ScheduleSettings& s, JsonObject out) {
    out["enabled"] = s.enabled;
    JsonObject awake = out["awake"].to<JsonObject>();
    for (int i = 0; i < 7; ++i) {
        if (!s.awake[i].on) {
            awake[kWeekdays[i]] = nullptr;
            continue;
        }
        JsonObject day = awake[kWeekdays[i]].to<JsonObject>();
        day["from"] = s.awake[i].from;
        day["to"] = s.awake[i].to;
    }
    out["night"] = enumToName(kNightNames, s.night);
    out["night_brightness"] = s.nightBrightness;
}

// ---------------------------------------------------------------- storage

void toStorage(const DisplaySettings& d, JsonObject out) { toJson(d, out); }
void toStorage(const ScheduleSettings& s, JsonObject out) { toJson(s, out); }

void toStorage(const SourceSettings& s, JsonObject out) {
    out["kind"] = enumToName(kSourceKindNames, s.kind);
    out["bridge_url"] = s.bridgeUrl;
    out["token"] = s.token;
}

void toStorage(const WifiSettings& w, JsonObject out) {
    out["ssid"] = w.ssid;
    out["security"] = enumToName(kSecurityNames, w.security);
    out["eap"] = enumToName(kEapNames, w.eap);
    out["password"] = w.password;
    out["identity"] = w.identity;
    out["anon_identity"] = w.anonIdentity;
    out["validate_ca"] = w.validateCa;
}

void toStorage(const DeviceSettings& d, JsonObject out) {
    out["name"] = d.name;
    out["zone"] = d.zone;
    out["posix"] = d.posix;
    out["password"] = d.password;
}

void fromStorage(DisplaySettings& d, JsonObjectConst in) {
    FieldError ignore;
    DisplaySettings next = d;
    readDisplay(next, in, ignore, Mode::Lenient);
    FieldError err;
    if (validate(next, err)) d = next;  // an invalid stored set falls back as a whole
}

void fromStorage(ScheduleSettings& s, JsonObjectConst in) {
    FieldError ignore;
    ScheduleSettings next = s;
    readSchedule(next, in, ignore, Mode::Lenient);
    FieldError err;
    if (validate(next, err)) s = next;
}

void fromStorage(SourceSettings& s, JsonObjectConst in) {
    FieldError ignore;
    Reader r(in, "", ignore, Mode::Lenient);
    r.enumeration("kind", s.kind, kSourceKindNames);
    r.string("bridge_url", s.bridgeUrl, sizeof(s.bridgeUrl));
    r.string("token", s.token, sizeof(s.token));
}

void fromStorage(WifiSettings& w, JsonObjectConst in) {
    FieldError ignore;
    Reader r(in, "", ignore, Mode::Lenient);
    r.string("ssid", w.ssid, sizeof(w.ssid));
    r.enumeration("security", w.security, kSecurityNames);
    r.enumeration("eap", w.eap, kEapNames);
    r.string("password", w.password, sizeof(w.password));
    r.string("identity", w.identity, sizeof(w.identity));
    r.string("anon_identity", w.anonIdentity, sizeof(w.anonIdentity));
    r.boolean("validate_ca", w.validateCa);
}

void fromStorage(DeviceSettings& d, JsonObjectConst in) {
    FieldError ignore;
    Reader r(in, "", ignore, Mode::Lenient);
    r.string("name", d.name, sizeof(d.name));
    r.string("zone", d.zone, sizeof(d.zone));
    r.string("posix", d.posix, sizeof(d.posix));
    r.string("password", d.password, sizeof(d.password));
}

// ---------------------------------------------------------------- URLs

bool isHttpsUrl(const char* url) {
    if (!url || strncmp(url, "https://", 8) != 0) return false;
    const char* host = url + 8;
    return *host && *host != '/' && !strchr(url, ' ');
}

bool hostOfUrl(const char* url, char* out, size_t size) {
    if (!out || size == 0) return false;
    out[0] = '\0';
    if (!url) return false;
    const char* p = strstr(url, "://");
    p = p ? p + 3 : url;
    const char* at = strchr(p, '@');
    const char* slash = strchr(p, '/');
    if (at && (!slash || at < slash)) p = at + 1;
    size_t i = 0;
    while (p[i] && p[i] != '/' && p[i] != ':' && p[i] != '?' && i + 1 < size) {
        out[i] = p[i];
        ++i;
    }
    out[i] = '\0';
    return i > 0;
}

} // namespace pk
