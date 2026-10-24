// Settings, glucose, bridge parsing, schedule, image check and naming.
#include "core/bridge.h"
#include "core/glucose.h"
#include "core/identity.h"
#include "core/image_check.h"
#include "core/schedule.h"
#include "core/settings.h"
#include "test.h"

using namespace pk;

// ---------------------------------------------------------------- settings

static bool patchDisplayJson(DisplaySettings& d, const char* json, FieldError& err) {
    JsonDocument doc;
    deserializeJson(doc, json);
    return patch(d, doc.as<JsonObjectConst>(), err);
}

TEST(defaults_are_valid) {
    Settings s;
    setDefaults(s);
    FieldError e1, e2;
    CHECK(validate(s.display, e1));
    CHECK(validate(s.schedule, e2));
    CHECK(s.display.pet.id == PetId::Patch);
    CHECK(!s.display.alerts.replacePet);
}

TEST(display_patch_applies_one_field) {
    DisplaySettings d;
    setDefaults(d);
    FieldError err;
    CHECK(patchDisplayJson(d, R"({"glucose":{"low":75},"alerts":{"replace_pet":true}})", err));
    CHECK_EQ(d.glucose.low, 75);
    CHECK(d.alerts.replacePet);
    CHECK_EQ(d.glucose.high, 180);
}

TEST(display_patch_rejects_and_names_the_field) {
    DisplaySettings d;
    setDefaults(d);
    FieldError e1;
    CHECK(!patchDisplayJson(d, R"({"glucose":{"low":40}})", e1));
    CHECK_STR(e1.field, "glucose.urgent_low");
    CHECK_EQ(d.glucose.low, 70);  // unchanged on failure

    FieldError e2;
    CHECK(!patchDisplayJson(d, R"({"glucose":{"lowx":1}})", e2));
    CHECK_STR(e2.field, "glucose.lowx");

    FieldError e3;
    CHECK(!patchDisplayJson(d, R"({"rotation":{"seconds":"10"}})", e3));
    CHECK_STR(e3.field, "rotation.seconds");

    FieldError e4;
    CHECK(!patchDisplayJson(d, R"({"rotation":{"seconds":2}})", e4));
    CHECK_STR(e4.field, "rotation.seconds");

    FieldError e5;
    CHECK(!patchDisplayJson(d, R"({"pet":{"id":"pip"}})", e5));
    CHECK_STR(e5.field, "pet.id");

    FieldError e6;
    CHECK(!patchDisplayJson(d, R"({"weather":{}})", e6));
    CHECK_STR(e6.field, "weather");
}

TEST(display_json_roundtrip) {
    DisplaySettings a;
    setDefaults(a);
    a.glucose.unit = Unit::Mmol;
    a.pet.id = PetId::Kobi;
    a.time.date = DateFormat::MonthDay;
    JsonDocument doc;
    toJson(a, doc.to<JsonObject>());
    CHECK_STR(doc["glucose"]["unit"].as<const char*>(), "mmol");
    CHECK_STR(doc["pet"]["id"].as<const char*>(), "kobi");
    CHECK_EQ(doc["alerts"]["replace_pet"].as<bool>(), false);
    DisplaySettings b;
    setDefaults(b);
    fromStorage(b, doc.as<JsonObjectConst>());
    CHECK(b.glucose.unit == Unit::Mmol);
    CHECK(b.pet.id == PetId::Kobi);
    CHECK(b.time.date == DateFormat::MonthDay);
}

TEST(storage_skips_bad_values) {
    DisplaySettings d;
    setDefaults(d);
    JsonDocument doc;
    deserializeJson(doc, R"({"glucose":{"unit":"furlongs","low":"x"},"pet":{"id":"leo"},"junk":1})");
    fromStorage(d, doc.as<JsonObjectConst>());
    CHECK(d.glucose.unit == Unit::MgDl);
    CHECK_EQ(d.glucose.low, 70);
    CHECK(d.pet.id == PetId::Leo);
}

TEST(schedule_patch_days) {
    ScheduleSettings s;
    setDefaults(s);
    JsonDocument doc;
    deserializeJson(doc, R"({"enabled":true,"awake":{"sat":null,"mon":{"from":495,"to":945}}})");
    FieldError err;
    CHECK(patch(s, doc.as<JsonObjectConst>(), err));
    CHECK(s.enabled);
    CHECK(!s.awake[5].on);
    CHECK(s.awake[0].on);
    CHECK_EQ(s.awake[0].from, 495);
    CHECK(s.awake[1].on);  // untouched

    JsonDocument bad;
    deserializeJson(bad, R"({"awake":{"tue":{"from":600,"to":500}}})");
    FieldError e2;
    CHECK(!patch(s, bad.as<JsonObjectConst>(), e2));
    CHECK_STR(e2.field, "awake.tue");

    JsonDocument out;
    toJson(s, out.to<JsonObject>());
    CHECK(out["awake"]["sat"].isNull());
    CHECK_EQ(out["awake"]["mon"]["to"].as<int>(), 945);
}

TEST(source_storage_keeps_the_token) {
    SourceSettings s;
    setDefaults(s);
    copyStr(s.bridgeUrl, "https://x.workers.dev/clock");
    copyStr(s.token, "secret");
    JsonDocument doc;
    toStorage(s, doc.to<JsonObject>());
    SourceSettings t;
    setDefaults(t);
    fromStorage(t, doc.as<JsonObjectConst>());
    CHECK_STR(t.token, "secret");
    CHECK_STR(t.bridgeUrl, "https://x.workers.dev/clock");
}

TEST(urls) {
    CHECK(isHttpsUrl("https://a.b/clock"));
    CHECK(!isHttpsUrl("http://a.b/clock"));
    CHECK(!isHttpsUrl("https://"));
    char host[64];
    CHECK(hostOfUrl("https://pumpkin-bridge.x.workers.dev/clock?x=1", host, sizeof(host)));
    CHECK_STR(host, "pumpkin-bridge.x.workers.dev");
}

// ---------------------------------------------------------------- glucose

TEST(glucose_format) {
    char b[12];
    formatGlucose(b, sizeof(b), 142, Unit::MgDl);
    CHECK_STR(b, "142");
    formatGlucose(b, sizeof(b), 142, Unit::Mmol);
    CHECK_STR(b, "7.9");
    formatDelta(b, sizeof(b), 5, Unit::MgDl);
    CHECK_STR(b, "+5");
    formatDelta(b, sizeof(b), -6, Unit::Mmol);
    CHECK_STR(b, "-0.3");
}

TEST(glucose_bands) {
    DisplaySettings d;
    setDefaults(d);
    CHECK(bandOf(54, d) == Band::UrgentLow);
    CHECK(bandOf(55, d) == Band::Low);
    CHECK(bandOf(70, d) == Band::InRange);
    CHECK(bandOf(180, d) == Band::InRange);
    CHECK(bandOf(181, d) == Band::High);
    CHECK(bandOf(251, d) == Band::UrgentHigh);
    CHECK(bandColor(Band::InRange, ColorPreset::Calm) == rgb(0xE8E8E8));
}

// ---------------------------------------------------------------- bridge

static const char kClock[] =
    R"({"glucose":142,"trend":"SingleUp","timestamp":1791555420,"message":"","force_mode":-1,)"
    R"("tir":78,"tir_below":4,"tir_above":18,"iob":1.35,"next_poll_sec":200,"source":"carelink","trend_arrows":1,)"
    R"("alert":"CONNECT SET","warnings":["warning","set","unknown","set"],"temp_target":true,"bridge_alert":""})";

TEST(bridge_parses_clock) {
    BridgeData d;
    CHECK(parseClock(kClock, strlen(kClock), d) == ParseResult::Ok);
    CHECK_EQ(d.mgdl, 142);
    CHECK(d.trend == Trend::SingleUp);
    CHECK_EQ(d.tir, 78);
    CHECK(d.iobValid);
    CHECK(d.tempTarget);
    CHECK_EQ(d.warningCount, 2);  // unknown and duplicate ids dropped
    CHECK(d.warnings[0] == Warning::Alert);
    CHECK(d.warnings[1] == Warning::Set);
    CHECK_EQ(d.nextPollSec, 200);
    CHECK_STR(d.upstream, "carelink");
}

TEST(bridge_classify) {
    BridgeData d;
    parseClock(kClock, strlen(kClock), d);
    FetchOutcome ok{};
    ok.http = 200;
    ok.parsed = true;
    CHECK(classify(ok, d, 60, 20) == SourceState::Ok);
    CHECK(classify(ok, d, 20 * 60, 20) == SourceState::Stale);
    copyStr(d.bridgeAlert, "CARELINK LOGIN NODIG");
    CHECK(classify(ok, d, 60, 20) == SourceState::LoginNeeded);
    copyStr(d.bridgeAlert, "CARELINK ERROR");
    CHECK(classify(ok, d, 60, 20) == SourceState::CarelinkError);
    FetchOutcome u{};
    u.http = 401;
    CHECK(classify(u, d, 0, 20) == SourceState::Unauthorized);
    FetchOutcome t{};
    t.transportFailed = true;
    CHECK(classify(t, d, 0, 20) == SourceState::Unreachable);
    FetchOutcome s{};
    s.http = 503;
    CHECK(classify(s, d, 0, 20) == SourceState::Stale);
}

TEST(bridge_reminders_url) {
    char out[128];
    CHECK(remindersClearUrl("https://x.workers.dev/clock", out, sizeof(out)));
    CHECK_STR(out, "https://x.workers.dev/reminders/clear?id=all");
    CHECK(remindersClearUrl("https://x.dev/a/clock?t=1", out, sizeof(out)));
    CHECK_STR(out, "https://x.dev/a/reminders/clear?id=all");
    CHECK(!remindersClearUrl("https://x.dev/api", out, sizeof(out)));
}

TEST(parse_sensor_change_hides_yellow_sensor) {
    const char* body = R"({"glucose":120,"timestamp":1,"warnings":["bg","sensor_change","sensor","set"]})";
    BridgeData d;
    CHECK(parseClock(body, strlen(body), d) == ParseResult::Ok);
    CHECK_EQ(d.warningCount, 3);
    CHECK(d.warnings[0] == Warning::Bg);
    CHECK(d.warnings[1] == Warning::SensorChange);
    CHECK(d.warnings[2] == Warning::Set);
    const char* due = R"({"glucose":120,"timestamp":1,"warnings":["sensor"]})";
    CHECK(parseClock(due, strlen(due), d) == ParseResult::Ok);
    CHECK_EQ(d.warningCount, 1);
    CHECK(d.warnings[0] == Warning::Sensor);
}

TEST(demo_covers_every_screen) {
    bool low = false, high = false, sport = false;
    bool seen[(int)Warning::Count] = {};
    DisplaySettings ds;
    setDefaults(ds);
    for (uint32_t ms = 0; ms < 20 * 60 * 1000; ms += 5000) {
        BridgeData d;
        uint32_t age;
        demoData(ms, d, age);
        low |= d.mgdl < ds.glucose.low;
        high |= d.mgdl > ds.glucose.high;
        sport |= d.tempTarget;
        if (d.warningCount) seen[(int)d.warnings[0]] = true;
    }
    CHECK(low && high && sport);
    for (int w = 1; w < (int)Warning::Count; ++w) CHECK(seen[w]);
}

// ---------------------------------------------------------------- schedule

TEST(schedule_next_change) {
    ScheduleSettings s;
    setDefaults(s);
    s.enabled = true;
    for (int d = 0; d < 5; ++d) s.awake[d] = {true, 495, 945};
    s.awake[5].on = s.awake[6].on = false;
    // Friday 15:00: awake, night at 15:45.
    ScheduleNow n = scheduleNow(s, true, 4, 900);
    CHECK(n.state == DayState::Awake);
    CHECK(n.hasNext);
    CHECK_EQ(n.nextWeekday, 4);
    CHECK_EQ(n.nextMinute, 945);
    CHECK(!n.nextToAwake);
    // Friday 20:00: night until Monday 08:15.
    n = scheduleNow(s, true, 4, 1200);
    CHECK(n.state == DayState::Night);
    CHECK_EQ(n.nextWeekday, 0);
    CHECK_EQ(n.nextMinute, 495);
    // No time: awake. Disabled: off.
    CHECK(scheduleNow(s, false, -1, -1).state == DayState::Awake);
    s.enabled = false;
    CHECK(scheduleNow(s, true, 4, 1200).state == DayState::Off);
}

TEST(schedule_until_midnight) {
    ScheduleSettings s;
    setDefaults(s);
    s.enabled = true;
    for (auto& a : s.awake) a = {true, 420, 1440};
    CHECK(awakeAt(s, 2, 1439));
    CHECK(!awakeAt(s, 2, 419));
    ScheduleNow n = scheduleNow(s, true, 2, 1300);
    CHECK_EQ(n.nextWeekday, 3);
    CHECK_EQ(n.nextMinute, 0);
}

// ---------------------------------------------------------------- image check

static OtaImageVerdict verdictFor(const std::string& data) {
    OtaImageCheck c;
    ota_image_check_begin(c, PUMPKIN_IMAGE_MARKER, sizeof(PUMPKIN_IMAGE_MARKER_PREFIX) - 1);
    // Feed in small pieces: the marker may span chunks.
    for (size_t i = 0; i < data.size(); i += 7) {
        OtaImageVerdict v = ota_image_check_feed(c, (const uint8_t*)data.data() + i, std::min<size_t>(7, data.size() - i));
        if (v != OTA_IMAGE_PENDING) return v;
    }
    return ota_image_check_finish(c);
}

static std::string image(uint16_t chip, const char* marker) {
    std::string s(24, '\0');
    s[0] = (char)0xE9;
    s[12] = (char)(chip & 0xFF);
    s[13] = (char)(chip >> 8);
    s += std::string(1000, 'x') + marker + std::string(100, 'y');
    return s;
}

TEST(image_check_verdicts) {
    CHECK(verdictFor(image(0, PUMPKIN_IMAGE_MARKER)) == OTA_IMAGE_OK);
    CHECK(verdictFor(image(9, PUMPKIN_IMAGE_MARKER)) == OTA_IMAGE_WRONG_CHIP);
    CHECK(verdictFor(image(0, "PUMPKIN-IMAGE|other-board|")) == OTA_IMAGE_WRONG_HARDWARE);
    CHECK(verdictFor(image(0, "nothing")) == OTA_IMAGE_NOT_PUMPKIN);
    CHECK(verdictFor(std::string("\x89PNG") + std::string(100, 'z')) == OTA_IMAGE_NOT_FIRMWARE);
    CHECK_STR(ota_image_verdict_error(OTA_IMAGE_NOT_PUMPKIN), "not_pumpkin");
}

// ---------------------------------------------------------------- naming

TEST(clock_slugs) {
    char out[40];
    CHECK(clock_name_slug("Klas 1B Émilie", out, sizeof(out)));
    CHECK_STR(out, "klas-1b-emilie");
    CHECK(clock_name_slug("Straße", out, sizeof(out)));
    CHECK_STR(out, "strasse");
    CHECK(!clock_name_valid("   "));
    clock_slug_with_suffix("keuken", 2, out, sizeof(out));
    CHECK_STR(out, "keuken-2");
}
