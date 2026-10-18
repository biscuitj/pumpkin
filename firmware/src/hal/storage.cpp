#include "storage.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>

#include "core/app.h"
#include "core/identity.h"

namespace hal {

namespace {

const char* const kNamespace = "pumpkin";
const char* const kOldNamespace = "tc001cfg";  // SugarClock-based firmware up to 0.12
constexpr uint32_t kOldMagic = 0x474C5543;
const char* const kCaPath = "/wifi_ca.pem";

struct GroupKey {
    uint8_t group;
    const char* key;
};
const GroupKey kKeys[] = {{pk::kGroupDisplay, "display"},
                          {pk::kGroupSchedule, "schedule"},
                          {pk::kGroupSource, "source"},
                          {pk::kGroupWifi, "wifi"},
                          {pk::kGroupDevice, "device"}};

int caCached = -1;  // -1 unknown, 0 no, 1 yes: asked every frame, so don't hit the filesystem

bool fsReady() {
    static bool mounted = false;
    if (!mounted) mounted = LittleFS.begin(true, "/littlefs", 4, "spiffs");
    return mounted;
}

void serializeGroup(const pk::Settings& s, uint8_t group, String& out) {
    JsonDocument doc;
    JsonObject o = doc.to<JsonObject>();
    switch (group) {
        case pk::kGroupDisplay: pk::toStorage(s.display, o); break;
        case pk::kGroupSchedule: pk::toStorage(s.schedule, o); break;
        case pk::kGroupSource: pk::toStorage(s.source, o); break;
        case pk::kGroupWifi: pk::toStorage(s.wifi, o); break;
        case pk::kGroupDevice: pk::toStorage(s.device, o); break;
    }
    serializeJson(doc, out);
}

void loadGroup(pk::Settings& s, uint8_t group, const String& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok || !doc.is<JsonObject>()) return;
    JsonObjectConst o = doc.as<JsonObjectConst>();
    switch (group) {
        case pk::kGroupDisplay: pk::fromStorage(s.display, o); break;
        case pk::kGroupSchedule: pk::fromStorage(s.schedule, o); break;
        case pk::kGroupSource: pk::fromStorage(s.source, o); break;
        case pk::kGroupWifi: pk::fromStorage(s.wifi, o); break;
        case pk::kGroupDevice: pk::fromStorage(s.device, o); break;
    }
}

// ---------------------------------------------------------------- migration

void getStr(Preferences& p, const char* key, char* out, size_t size) {
    const String v = p.getString(key, "");
    pk::copyStr(out, size, v.c_str());
}

// The old companion list (fish, ghost, ... pumpkin = 7, cat, frog, penguin,
// owl, lion, unicorn, crab) -> the pets Pumpkin kept.
pk::PetId petFromOld(int id) {
    switch (id) {
        case 8: return pk::PetId::Toffee;
        case 9: return pk::PetId::Kiko;
        case 10: return pk::PetId::Domino;
        case 11: return pk::PetId::Hoot;
        case 12: return pk::PetId::Leo;
        case 13: return pk::PetId::Stella;
        case 14: return pk::PetId::Kobi;
        default: return pk::PetId::Patch;
    }
}

bool migrateOld(pk::Settings& s) {
    Preferences p;
    if (!p.begin(kOldNamespace, true)) return false;
    if (p.getUInt("magic", 0) != kOldMagic) {
        p.end();
        return false;
    }

    // Wi-Fi
    pk::WifiSettings& w = s.wifi;
    getStr(p, "wifi_ssid", w.ssid, sizeof(w.ssid));
    w.security = p.getInt("wifi_sec", 0) == 1 ? pk::WifiSecurity::Enterprise : pk::WifiSecurity::Personal;
    w.eap = p.getInt("wifi_eap", 0) == 1 ? pk::EapMethod::Ttls : pk::EapMethod::Peap;
    if (w.security == pk::WifiSecurity::Enterprise) {
        getStr(p, "wifi_epass", w.password, sizeof(w.password));
        getStr(p, "wifi_ident", w.identity, sizeof(w.identity));
        getStr(p, "wifi_anon", w.anonIdentity, sizeof(w.anonIdentity));
    } else {
        getStr(p, "wifi_pass", w.password, sizeof(w.password));
    }
    w.validateCa = p.getBool("wifi_ca_val", false);

    // Source: the bridge address (custom URL) or demo; Dexcom/Libre are gone.
    const int src = p.getInt("data_src", 0);
    if (src == 2) {
        s.source.kind = pk::SourceKind::Demo;
    } else if (src == 0) {
        getStr(p, "server_url", s.source.bridgeUrl, sizeof(s.source.bridgeUrl));
        getStr(p, "auth_token", s.source.token, sizeof(s.source.token));
        if (!pk::isHttpsUrl(s.source.bridgeUrl)) s.source.bridgeUrl[0] = '\0';
    }

    // Device
    getStr(p, "clk_name", s.device.name, sizeof(s.device.name));
    if (s.device.name[0] && !clock_name_valid(s.device.name)) s.device.name[0] = '\0';
    getStr(p, "web_pw", s.device.password, sizeof(s.device.password));
    // The time zone isn't carried over: the old firmware stored only POSIX
    // rules (US Eastern unless changed) and the dashboard picks zones by name.
    // New clocks start on Europe/Brussels; pick another one in Device.

    // Display
    pk::DisplaySettings d = s.display;
    d.screens.tir = p.getBool("tir_en", true);
    d.screens.iob = p.getBool("iob_en", true);
    d.screens.alerts = p.getBool("alrt_en", true);
    d.screens.time = p.getBool("time_en", true);
    d.screens.pet = p.getBool("amb_en", false);
    d.rotation.autoCycle = p.getBool("acyc_en", true);
    d.rotation.seconds = (uint16_t)constrain(p.getInt("acyc_sec", 10), pk::kRotationMin, pk::kRotationMax);
    d.glucose.unit = p.getBool("use_mmol", false) ? pk::Unit::Mmol : pk::Unit::MgDl;
    d.glucose.urgentLow = (uint16_t)p.getInt("t_ulow", 55);
    d.glucose.low = (uint16_t)p.getInt("t_low", 70);
    d.glucose.high = (uint16_t)p.getInt("t_high", 180);
    d.glucose.urgentHigh = (uint16_t)p.getInt("t_uhigh", 250);
    d.glucose.staleAfterMin = (uint8_t)constrain(p.getInt("stale_min", 20), pk::kStaleMin, pk::kStaleMax);
    d.time.hour24 = p.getBool("use_24h", true);
    const int fmt = p.getInt("date_fmt", 0);
    d.time.date = !p.getBool("date_en", false) ? pk::DateFormat::Off
                : fmt == 2                     ? pk::DateFormat::DayMonth
                                               : pk::DateFormat::MonthDay;
    d.pet.id = petFromOld(p.getInt("pal_type", 7));
    const int style = p.getInt("pal_style", 0);
    d.pet.style = style == 1 ? pk::PetStyle::Icon : style == 2 ? pk::PetStyle::PetOnly : pk::PetStyle::Text;
    d.pet.showTir = p.getBool("pal_tir", true);
    d.pet.seasonal = p.getBool("amb_season", true);
    d.alerts.replacePet = p.getBool("alrt_pet", false);
    d.brightness.autoLevel = p.getBool("auto_brt", true);
    d.brightness.level = (uint8_t)constrain((int)p.getUChar("brightness", 40), pk::kBrightnessMin, pk::kBrightnessMax);
    pk::FieldError err;
    if (!pk::validate(d, err)) {
        // Thresholds the new firmware doesn't accept: keep the rest.
        pk::DisplaySettings defaults;
        pk::setDefaults(defaults);
        d.glucose.urgentLow = defaults.glucose.urgentLow;
        d.glucose.low = defaults.glucose.low;
        d.glucose.high = defaults.glucose.high;
        d.glucose.urgentHigh = defaults.glucose.urgentHigh;
    }
    if (pk::validate(d, err)) s.display = d;

    // Night mode -> awake hours (same every day)
    if (p.getBool("night_en", false)) {
        const int start = p.getInt("night_start", 22), end = p.getInt("night_end", 7);
        if (end < start && start <= 24 && end >= 0) {
            s.schedule.enabled = true;
            for (auto& a : s.schedule.awake) a = {true, (uint16_t)(end * 60), (uint16_t)(start * 60)};
            s.schedule.night = p.getBool("night_mute", false) ? pk::NightMode::Mute : pk::NightMode::Dim;
            s.schedule.nightBrightness = p.getUChar("night_brt", 10);
        }
    }
    p.end();
    return true;
}

} // namespace

bool storageLoad(pk::Settings& s) {
    Preferences p;
    bool any = false;
    if (p.begin(kNamespace, true)) {
        for (const auto& k : kKeys) {
            if (!p.isKey(k.key)) continue;
            loadGroup(s, k.group, p.getString(k.key, ""));
            any = true;
        }
        p.end();
    }
    if (any) return true;

    if (migrateOld(s)) {
        Serial.println("[storage] Settings carried over from the old firmware");
        storageSave(s, pk::kGroupAll);
        Preferences old;
        if (old.begin(kOldNamespace, false)) {
            old.clear();  // holds passwords and tokens: don't leave a second copy
            old.end();
        }
        return true;
    }
    return false;
}

void storageSave(const pk::Settings& s, uint8_t groups) {
    Preferences p;
    if (!p.begin(kNamespace, false)) {
        Serial.println("[storage] NVS open failed");
        return;
    }
    for (const auto& k : kKeys) {
        if (!(groups & k.group)) continue;
        String json;
        serializeGroup(s, k.group, json);
        if (p.isKey(k.key) && p.getString(k.key, "") == json) continue;  // spare the flash
        if (p.putString(k.key, json) == 0) {
            Serial.printf("[storage] Saving %s failed\n", k.key);
        }
    }
    p.end();
}

void storageErase() {
    Preferences p;
    if (p.begin(kNamespace, false)) {
        p.clear();
        p.end();
    }
    if (p.begin(kOldNamespace, false)) {
        p.clear();
        p.end();
    }
    caDelete();
}

bool caWrite(const char* pem, size_t len) {
    if (!fsReady()) return false;
    File f = LittleFS.open(kCaPath, "w");
    if (!f) return false;
    const size_t n = f.write(reinterpret_cast<const uint8_t*>(pem), len);
    f.close();
    caCached = -1;
    return n == len;
}

bool caDelete() {
    if (!fsReady()) return false;
    caCached = -1;
    return !LittleFS.exists(kCaPath) || LittleFS.remove(kCaPath);
}

bool caExists() {
    if (caCached < 0) caCached = fsReady() && LittleFS.exists(kCaPath) ? 1 : 0;
    return caCached == 1;
}

size_t caRead(char* out, size_t size) {
    if (!fsReady()) return 0;
    File f = LittleFS.open(kCaPath, "r");
    if (!f) return 0;
    const size_t n = f.size();
    if (n == 0 || n >= size) {
        f.close();
        return 0;
    }
    const size_t got = f.read(reinterpret_cast<uint8_t*>(out), n);
    f.close();
    out[got] = '\0';
    return got == n ? n : 0;
}

} // namespace hal
