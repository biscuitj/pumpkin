// What the matrix shows in the situations that matter.
#include "core/engine.h"
#include "core/glucose.h"
#include "test.h"

using namespace pk;

namespace {

struct Rig {
    Settings s;
    BridgeData data;
    EngineInputs in;
    Engine e;
    EngineOutput out;
    uint32_t t = 1000;

    Rig() {
        setDefaults(s);
        data.clear();
        data.valid = true;
        data.mgdl = 120;
        data.trend = Trend::Flat;
        data.timestamp = 1000;
        data.tir = 80;
        in.display = &s.display;
        in.schedule = &s.schedule;
        in.kind = SourceKind::Bridge;
        in.sourceConfigured = true;
        in.sourceState = SourceState::Ok;
        in.haveData = true;
        in.data = &data;
        in.readingAgeSec = 60;
        in.wifi = WifiMode::Connected;
        in.hasWifiCredentials = true;
        in.ip = "192.168.1.50";
        in.localName = "keuken.local";
        in.time = LocalTime{true, 2026, 10, 17, 15, 0, 0, 4};
        e.begin(t);
    }

    // Advance in render-loop steps.
    void run(uint32_t ms) {
        for (uint32_t i = 0; i < ms; i += 50) {
            t += 50;
            in.now = t;
            e.tick(in, out);
        }
    }
    void pastBoot() { run(6000); }
    bool lit(int x, int y) const { return out.frame.get(x, y) != Rgb{0, 0, 0}; }
    int litCount() const {
        int n = 0;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 32; ++x) n += lit(x, y);
        return n;
    }
};

} // namespace

TEST(engine_boots_then_shows_glucose) {
    Rig r;
    r.run(100);
    CHECK(r.out.screen == Screen::Boot);
    r.pastBoot();
    CHECK(r.out.screen == Screen::Glucose);
    CHECK(r.out.fade == 255);
    CHECK(r.litCount() > 10);
}

TEST(engine_rotates_in_order) {
    Rig r;
    r.s.display.rotation.seconds = 3;
    r.pastBoot();
    r.data.warningCount = 1;
    r.data.warnings[0] = Warning::Battery;
    Screen order[8];
    int n = 0;
    Screen last = r.out.screen;
    order[n++] = last;
    for (int i = 0; i < 400 && n < 8; ++i) {
        r.run(100);
        if (r.out.screen != last && r.out.fade == 255) {
            last = r.out.screen;
            order[n++] = last;
        }
    }
    CHECK(n >= 6);
    CHECK(order[0] == Screen::Glucose);
    CHECK(order[1] == Screen::Tir);
    CHECK(order[2] == Screen::Iob);
    CHECK(order[3] == Screen::Time);
    CHECK(order[4] == Screen::Pet);
    CHECK(order[5] == Screen::Alerts);
}

TEST(engine_alerts_can_replace_the_pet) {
    Rig r;
    r.s.display.rotation.autoCycle = false;
    r.s.display.alerts.replacePet = true;
    r.data.warningCount = 1;
    r.data.warnings[0] = Warning::Set;
    r.pastBoot();
    bool sawPet = false, sawAlerts = false;
    for (int i = 0; i < 8; ++i) {
        r.e.next(r.t);
        r.run(1500);
        sawPet |= r.out.screen == Screen::Pet;
        sawAlerts |= r.out.screen == Screen::Alerts;
    }
    CHECK(sawAlerts);
    CHECK(!sawPet);
    // Without alerts the pet is back.
    r.data.warningCount = 0;
    for (int i = 0; i < 8; ++i) {
        r.e.next(r.t);
        r.run(1500);
        sawPet |= r.out.screen == Screen::Pet;
    }
    CHECK(sawPet);
}

TEST(engine_urgent_glucose_keeps_the_pet) {
    // Removed behaviour: an urgent high or low used to swap the pet for the number.
    Rig r;
    r.s.display.rotation.autoCycle = false;
    r.s.display.glucose.lowLock = false;
    r.data.mgdl = 320;
    r.pastBoot();
    for (int i = 0; i < 6 && r.out.screen != Screen::Pet; ++i) {
        r.e.next(r.t);
        r.run(1500);
    }
    CHECK(r.out.screen == Screen::Pet);
    // The pet's "HIGH" letters are drawn on the right half, in the high colour.
    const Rgb high = bandColor(Band::High, r.s.display.glucose.colors);
    bool word = false;
    for (int x = 15; x < 32; ++x)
        for (int y = 2; y < 7; ++y) word |= r.out.frame.get(x, y) == high;
    CHECK(word);
}

TEST(engine_low_lock_pins_glucose) {
    Rig r;
    r.s.display.rotation.seconds = 3;
    r.pastBoot();
    r.data.mgdl = 62;
    r.data.timestamp++;
    r.run(100);
    CHECK(r.out.screen == Screen::Glucose);
    CHECK(r.out.fade == 255);  // no fade on the way to a low
    r.run(20000);
    CHECK(r.out.screen == Screen::Glucose);
    r.e.next(r.t);
    r.run(1000);
    CHECK(r.out.screen == Screen::Glucose);  // buttons wait
    Preview p{Preview::Pet, PetId::Kiko, 0, 0};
    CHECK(!r.e.preview(p, r.t));
    // Back in range: the rotation goes on from glucose, so next is time in range.
    r.data.mgdl = 110;
    r.data.timestamp++;
    r.run(500);
    r.e.next(r.t);
    r.run(1500);
    CHECK(r.out.screen == Screen::Tir);
}

TEST(engine_stale_reading_is_grey_and_warns_first) {
    Rig r;
    r.pastBoot();
    r.in.readingAgeSec = 11 * 60;  // warning stage: half of 20 min
    r.run(200);
    CHECK(r.lit(29, 0) || r.lit(29, 1) || r.lit(30, 2));  // the "!"
    const uint8_t dimmed = r.out.brightness;
    r.in.readingAgeSec = 25 * 60;
    r.run(200);
    const Rgb grey = presetColors(r.s.display.glucose.colors).stale;
    bool greyPixel = false;
    for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 8; ++y) greyPixel |= r.out.frame.get(x, y) == grey;
    CHECK(greyPixel);
    CHECK(dimmed >= 20);
}

TEST(engine_sport_bars) {
    Rig r;
    r.pastBoot();
    r.data.tempTarget = true;
    r.run(100);
    CHECK(r.lit(0, 0) && r.lit(1, 7) && r.lit(30, 3) && r.lit(31, 4));
}

TEST(engine_delta_flash_on_new_reading) {
    Rig r;
    r.pastBoot();
    r.data.mgdl = 128;
    r.data.timestamp += 300;
    r.run(100);
    // "+8" is short: the middle columns are used, the far right (arrow) isn't.
    CHECK(!r.lit(28, 3));
    r.run(3500);
    CHECK(r.litCount() > 10);
}

TEST(engine_night_mute_is_dark) {
    Rig r;
    r.s.schedule.enabled = true;
    for (auto& a : r.s.schedule.awake) a = {true, 8 * 60, 15 * 60};
    r.s.schedule.night = NightMode::Mute;
    r.in.time.hour = 20;
    r.data.mgdl = 50;  // not even an urgent low wakes it
    r.pastBoot();
    CHECK_EQ(r.out.brightness, 0);
    CHECK(r.litCount() > 0);  // the mirror still shows what it would show
    r.s.schedule.night = NightMode::Dim;
    r.run(100);
    CHECK_EQ(r.out.brightness, r.s.schedule.nightBrightness);
}

TEST(engine_status_screens) {
    Rig r;
    r.in.wifi = WifiMode::SetupAp;
    r.in.apSsid = "Pumpkin-Setup-E5F6";
    r.in.apIp = "192.168.4.1";
    r.pastBoot();
    CHECK(r.out.screen == Screen::Status);
    r.in.wifi = WifiMode::Connected;
    r.in.sourceConfigured = false;
    r.run(1500);
    CHECK(r.out.screen == Screen::Status);
    r.in.sourceConfigured = true;
    r.run(1500);
    CHECK(r.out.screen == Screen::Glucose);
    // Demo data shows readings even without Wi-Fi.
    r.in.kind = SourceKind::Demo;
    r.in.wifi = WifiMode::Disconnected;
    r.run(1500);
    CHECK(r.out.screen == Screen::Glucose);
}

TEST(engine_preview_then_back) {
    Rig r;
    r.s.display.rotation.autoCycle = false;
    r.pastBoot();
    Preview p{Preview::Pet, PetId::Kobi, 2, 0};
    CHECK(r.e.preview(p, r.t));
    r.run(1500);
    CHECK(r.out.screen == Screen::Pet);
    r.run(9000);
    CHECK(r.out.screen == Screen::Glucose);
}

TEST(engine_alert_preview_plays_all_once) {
    Rig r;
    r.s.display.rotation.autoCycle = false;
    r.pastBoot();
    Preview p{Preview::Alerts, PetId::Patch, 0, 0};
    CHECK(r.e.preview(p, r.t));
    r.run(1500);
    CHECK(r.out.screen == Screen::Alerts);
    r.run(20000);
    CHECK(r.out.screen == Screen::Alerts);  // still going: seven alerts, 5 s each
    r.run(16000);
    CHECK(r.out.screen == Screen::Glucose);
}

TEST(engine_message_scrolls_in_rotation) {
    Rig r;
    r.s.display.rotation.seconds = 3;
    copyStr(r.data.bridgeAlert, "CARELINK LOGIN NEEDED");
    r.pastBoot();
    bool seen = false;
    for (int i = 0; i < 100 && !seen; ++i) {
        r.run(200);
        seen = r.out.screen == Screen::BridgeAlert;
    }
    CHECK(seen);
}
