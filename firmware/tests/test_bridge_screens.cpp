// Bridge screens: sport-mode bars, IOB screen, pump alerts screen + rotation.
// Compiles the complete production engine; fakes only its hardware/services.
// GCC diagnoses the existing timer/stopwatch snprintf bounds when compiling
// the whole engine. Keep those warnings visible without making this display
// policy regression test depend on unrelated timer formatting changes.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic warning "-Wformat-truncation"
#endif
#include "../src/glucose_engine.cpp"
#include "../src/bridge_alerts.cpp"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include <cassert>
#include <initializer_list>
#include <string>
#include <cstring>

static AppConfig cfg = {};
static GlucoseReading reading = {};
static unsigned long now_ms = 0, age_ms = 0;
static int failures = 0, drawn_glucose = 0, delta_frames = 0, button_actions = 0;
static uint16_t drawn_color = 0;
static uint8_t frame_level = 0;
static bool connected = true, ap_mode = false, notification = false, ever_received = true;
static NetCheckResult network = NC_OK, data_network = NC_OK;

unsigned long millis() { return now_ms; }
AppConfig& config_get() { return cfg; }
bool config_has_wifi() { return true; }
bool config_has_server() { return true; }
const GlucoseReading& http_get_reading() { return reading; }
unsigned long http_time_since_last_reading() { return age_ms; }
int http_get_failure_count() { return failures; }
bool http_has_ever_received() { return ever_received; }
int http_get_delta() { return -5; }
const TirStats& http_get_tir() { static TirStats t = {-1, -1, -1, false}; return t; }
static BridgeExtras extras = {};
const BridgeExtras& http_get_extras() { return extras; }
int http_get_history(GlucoseHistoryEntry*, int) { return 0; }
bool wifi_is_connected() { return connected; }
bool wifi_is_ap_mode() { return ap_mode; }
const char* wifi_get_ip() { return "192.0.2.1"; }
static const char* local_host_stub = "";
const char* clock_net_local_host() { return local_host_stub; }
const char* wifi_get_ap_ip() { return "192.0.2.2"; }
const char* wifi_get_ap_ssid() { return "SugarClock"; }
int wifi_ap_station_count() { return 0; }
WifiTrialState wifi_trial_get_state() { return WIFI_TRIAL_IDLE; }
const char* wifi_trial_ssid() { return "test"; }
NetCheckResult netcheck_dns() { return network; }
NetCheckResult netcheck_data() { return data_network; }
const char* netcheck_summary() { return "offline"; }
bool notify_has_active() { return notification; }
bool notify_is_urgent() { return true; }
const char* notify_get_text() { return "message"; }
bool time_is_available() { return true; }
static int hour_now = 12;
int time_get_hour() { return hour_now; }
int time_get_minute() { return 0; }
int time_get_second() { return 0; }
int time_get_day() { return 1; }
int time_get_month() { return 1; }
const char* time_get_month_abbr() { return "JAN"; }
uint8_t sensors_get_auto_brightness() { return 40; }
static int beeps = 0;
void buzzer_beep(int, int, int) { ++beeps; }
void ambient_fish_init() {}
void ambient_fish_render() {}
void ambient_fish_interact() { ++button_actions; }
void timer_toggle_start_pause() { ++button_actions; }
void timer_reset() { ++button_actions; }
TimerState timer_get_state() { return TIMER_IDLE; }
int timer_get_remaining_sec() { return 60; }
void stopwatch_toggle_start_pause() { ++button_actions; }
void stopwatch_reset() { ++button_actions; }
StopwatchState stopwatch_get_state() { return SW_IDLE; }
int stopwatch_get_elapsed_sec() { return 0; }
bool sysmon_has_data() { return true; }
int sysmon_get_value() { return 50; }
int sysmon_get_max() { return 100; }
const char* sysmon_get_label() { return "CPU"; }
bool countdown_is_configured() { return false; }
long countdown_get_remaining_sec() { return 0; }
bool weather_has_data() { return false; }
const WeatherReading& weather_get_reading() { static WeatherReading wx = {}; return wx; }
void weather_set_pre_fetch_callback(WeatherPreFetchCallback) {}
static uint8_t set_level = 0, stale_level = 0;
void display_set_brightness(uint8_t level) { set_level = level; }
void display_set_transition_level(uint8_t level) { frame_level = level; }
static uint16_t fb[8][32];
void display_clear() { memset(fb, 0, sizeof(fb)); }
void display_show() {}
static int scroll_frames = 0, scroll_passes = 0;
static std::string scrolled;
void display_scroll_reset() { scroll_frames = 0; }
bool display_scroll_text(const char* t, int, uint16_t, unsigned int) {
    scrolled = t; if (++scroll_frames >= 20) { scroll_frames = 0; ++scroll_passes; return true; } return false;
}
void display_draw_text(const char*, int, int, uint16_t) {}
void display_draw_pixel(int x, int y, uint16_t c) { if (x >= 0 && x < 32 && y >= 0 && y < 8) fb[y][x] = c; }
static std::string centered; static uint16_t centered_color = 0;
void display_draw_centered_text(const char* t, int, uint16_t c) { centered = t; centered_color = c; }
int display_text_width(const char* t) { return t ? (int)strlen(t) * 6 : 0; }
void display_draw_time(int, int, bool, bool, uint16_t) {}
void display_draw_trend(int, int, int, uint16_t) {}
void display_draw_bar(int, int, uint16_t) {}
int display_draw_glucose(int value, uint16_t color, bool) {
    memset(fb, 0, sizeof(fb)); drawn_glucose = value; drawn_color = color; return 24;
}
uint16_t display_color(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}
void display_draw_glucose_delta(int, int, uint16_t, bool) {}
void glucose_render_delta_flash(int, uint16_t, bool) { ++delta_frames; }
void glucose_render_stale(const GlucoseReading&, const AppConfig&, uint8_t level) { stale_level = level; }


static void tick(unsigned long elapsed = 50) { now_ms += elapsed; engine_loop(); }
static void run(unsigned long ms) { for (unsigned long t = 0; t < ms; t += 50) tick(); }
static bool in_order(DisplayState s) {
    for (int i = 0; i < toggle_count; ++i) if (toggle_order[i] == s) return true;
    return false;
}
static int order_index(DisplayState s) {
    for (int i = 0; i < toggle_count; ++i) if (toggle_order[i] == s) return i;
    return -1;
}
static bool icon_on_screen(const uint32_t* rows) {
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 32; ++x)
            if (((rows[y] >> (31 - x)) & 1) != (fb[y][x] != 0)) return false;
    return true;
}
static void set_warnings(std::initializer_list<uint8_t> ws) {
    extras.warning_count = 0;
    for (uint8_t w : ws) extras.warnings[extras.warning_count++] = w;
}

int main() {
    cfg.thresh_low = 80; cfg.thresh_urgent_low = 70; cfg.thresh_high = 180; cfg.thresh_urgent_high = 250;
    cfg.stale_timeout_min = 20; cfg.auto_cycle_sec = 10; cfg.auto_cycle_enabled = false;
    cfg.trend_display_enabled = true; cfg.tir_display_enabled = true; cfg.iob_display_enabled = true;
    cfg.time_display_enabled = true; cfg.ambient_enabled = true; cfg.timer_enabled = true;
    cfg.alerts_display_enabled = true; cfg.alerts_replace_pet = false;
    cfg.brightness = 100; cfg.color_in_range = 0x34A853; cfg.color_stale = 0x808080;
    cfg.data_source = 0;
    reading.valid = true; reading.glucose = 129; reading.force_mode = -1; reading.trend = TREND_FLAT;
    engine_init(); run(4000);

    // ---- warning ids
    assert(bridge_warning_from_id("battery") == WARN_BATTERY);
    assert(bridge_warning_from_id("warning") == WARN_ALERT);
    assert(bridge_warning_from_id("nope") == WARN_NONE && bridge_warning_from_id(nullptr) == WARN_NONE);
    for (uint8_t w = 1; w < WARN_COUNT; ++w) assert(bridge_warning_from_id(bridge_warning_id(w)) == w);
    assert(bridge_warning_from_id("suspended") == WARN_SUSPENDED);
    assert(bridge_warning_color(WARN_SUSPENDED) == display_color(0xFF, 0x6D, 0x00));   // orange
    assert(bridge_warning_color(WARN_BATTERY) == display_color(0x64, 0xB5, 0xF6));     // light blue
    assert(bridge_warning_color(WARN_RESERVOIR) == display_color(0x34, 0xA8, 0x53));   // green
    assert(bridge_warning_color(WARN_SENSOR) == display_color(0xFF, 0xE0, 0x00));      // yellow
    assert(bridge_warning_color(WARN_SET) == bridge_warning_color(WARN_SENSOR));
    for (uint8_t w = 1; w < WARN_COUNT; ++w) {                                          // every warning is an image
        display_clear(); assert(bridge_warning_draw_icon(w));
    }
    assert(!bridge_warning_draw_icon(WARN_NONE) && !bridge_warning_draw_icon(WARN_COUNT));

    // ---- bridge reminder-clear URL from the clock URL
    {
        char url[128];
        assert(bridge_reminders_clear_url("https://b.example.workers.dev/clock", url, sizeof(url)));
        assert(std::string(url) == "https://b.example.workers.dev/reminders/clear?id=all");
        assert(bridge_reminders_clear_url("https://b.example/x/clock?fmt=1", url, sizeof(url)));
        assert(std::string(url) == "https://b.example/x/reminders/clear?id=all");
        assert(!bridge_reminders_clear_url("https://ns.example/api/v1/entries/sgv.json?count=1", url, sizeof(url)) && !url[0]);
        assert(!bridge_reminders_clear_url("https://b.example/clocks", url, sizeof(url)));
        assert(!bridge_reminders_clear_url("/clock", url, 4) && !url[0]);                 // too small
        assert(!bridge_reminders_clear_url(nullptr, url, sizeof(url)));
    }

    // ---- rotation: IOB after TIR; alerts only while present, right after the pet
    engine_rebuild_toggle_order();
    assert(order_index(STATE_IOB_DISPLAY) == order_index(STATE_TIR_DISPLAY) + 1);
    assert(!in_order(STATE_ALERTS_DISPLAY));
    set_warnings({WARN_BATTERY});
    engine_rebuild_toggle_order();
    assert(order_index(STATE_ALERTS_DISPLAY) == order_index(STATE_AMBIENT_CREATURE_DISPLAY) + 1);
    cfg.alerts_replace_pet = true; engine_rebuild_toggle_order();
    assert(in_order(STATE_ALERTS_DISPLAY) && !in_order(STATE_AMBIENT_CREATURE_DISPLAY));
    cfg.alerts_replace_pet = false; cfg.ambient_enabled = false; engine_rebuild_toggle_order();
    assert(order_index(STATE_ALERTS_DISPLAY) == order_index(STATE_TIME_DISPLAY) + 1);  // where the pet would be
    cfg.ambient_enabled = true;
    cfg.alerts_display_enabled = false; engine_rebuild_toggle_order();
    assert(!in_order(STATE_ALERTS_DISPLAY));
    cfg.alerts_display_enabled = true;
    cfg.iob_display_enabled = false; engine_rebuild_toggle_order();
    assert(!in_order(STATE_IOB_DISPLAY));                                             // toggled like any screen
    cfg.iob_display_enabled = true;
    age_ms = 25UL * 60000; engine_rebuild_toggle_order();
    assert(!in_order(STATE_ALERTS_DISPLAY));                                          // old data: no alerts screen
    age_ms = 60000; set_warnings({}); engine_rebuild_toggle_order();

    // ---- sport mode: 2 px side bars in the value colour, only on fresh data
    engine_set_default_mode(STATE_GLUCOSE_DISPLAY); engine_rebuild_toggle_order();
    extras.temp_target = true; run(1500);
    assert(engine_get_state() == STATE_GLUCOSE_DISPLAY);
    for (int y = 0; y < 8; ++y)
        for (int x : {0, 1, 30, 31}) assert(fb[y][x] == drawn_color && drawn_color != 0);
    for (int y = 0; y < 8; ++y) assert(fb[y][2] == 0 && fb[y][29] == 0);
    extras.temp_target = false; run(100);
    for (int y = 0; y < 8; ++y) assert(fb[y][0] == 0 && fb[y][31] == 0);
    extras.temp_target = true; age_ms = 15UL * 60000; run(100);                      // stale warning: no bars
    for (int y = 0; y < 8; ++y) assert(fb[y][0] == 0);
    age_ms = 60000; extras.temp_target = false;

    // ---- IOB screen
    engine_force_state(STATE_IOB_DISPLAY);
    extras.iob_valid = true; extras.iob = 0.35f; run(1500);
    assert(engine_get_state() == STATE_IOB_DISPLAY && centered == "0.35U");
    extras.iob = 12.43f; run(100); assert(centered == "12.4U");
    extras.iob = 9.996f; run(100); assert(centered == "10.0U");
    extras.iob_valid = false; run(100); assert(centered == "--U");
    extras.iob_valid = true; extras.iob = 1.0f; failures = 5; run(100); assert(centered == "--U");  // not fresh
    failures = 0; engine_clear_force(); run(1500);

    // ---- alerts screen: each alert 5 s, then on to the next screen
    cfg.auto_cycle_enabled = true;
    set_warnings({WARN_BATTERY, WARN_SENSOR});
    engine_set_default_mode(STATE_AMBIENT_CREATURE_DISPLAY); engine_rebuild_toggle_order();
    assert(engine_get_user_mode() == STATE_AMBIENT_CREATURE_DISPLAY);
    last_cycle_ms = now_ms; run(10100);                                               // pet for its 10 s
    assert(engine_get_user_mode() == STATE_ALERTS_DISPLAY);
    run(700);                                                                          // fade
    assert(engine_get_state() == STATE_ALERTS_DISPLAY);
    run(1000); assert(icon_on_screen(ICON_BATTERY));
    run(4300);                                                                         // ~5 s after entry
    assert(icon_on_screen(ICON_SENSOR));                                               // static, no scrolling
    run(4500); assert(icon_on_screen(ICON_SENSOR) && engine_get_user_mode() == STATE_ALERTS_DISPLAY);
    run(600);
    assert(engine_get_user_mode() != STATE_ALERTS_DISPLAY);                           // 2 x 5 s, then on
    assert(engine_get_user_mode() == STATE_TIMER_DISPLAY);

    // an alert clears while another plays: nothing is skipped (tracked by type)
    set_warnings({WARN_SUSPENDED, WARN_BATTERY, WARN_SET});
    engine_set_default_mode(STATE_AMBIENT_CREATURE_DISPLAY); engine_rebuild_toggle_order();
    last_cycle_ms = now_ms; run(10100); run(800);
    assert(engine_get_state() == STATE_ALERTS_DISPLAY && icon_on_screen(ICON_PAUSE));
    run(4900); assert(icon_on_screen(ICON_BATTERY));
    set_warnings({WARN_BATTERY, WARN_SET});                                            // pump running again
    run(200); assert(icon_on_screen(ICON_BATTERY));                                   // battery keeps its turn
    run(4900); assert(icon_on_screen(ICON_SET));                                      // and set still follows
    set_warnings({WARN_SET}); run(200); assert(icon_on_screen(ICON_SET));
    run(5000); assert(engine_get_user_mode() == STATE_TIMER_DISPLAY);
    // the alert on screen clears: the next one starts straight away with its full time
    set_warnings({WARN_BG, WARN_SET});
    engine_set_default_mode(STATE_AMBIENT_CREATURE_DISPLAY); engine_rebuild_toggle_order();
    last_cycle_ms = now_ms; run(10100); run(800);
    run(2000); assert(icon_on_screen(ICON_BG));
    set_warnings({WARN_SET}); run(100); assert(icon_on_screen(ICON_SET));
    run(4700); assert(icon_on_screen(ICON_SET) && engine_get_user_mode() == STATE_ALERTS_DISPLAY);
    run(400); assert(engine_get_user_mode() == STATE_TIMER_DISPLAY);

    // alerts clear while shown: straight on to the next screen
    engine_set_default_mode(STATE_AMBIENT_CREATURE_DISPLAY); engine_rebuild_toggle_order();
    last_cycle_ms = now_ms; run(10100); run(800);
    assert(engine_get_state() == STATE_ALERTS_DISPLAY);
    set_warnings({}); run(200);
    assert(engine_get_user_mode() != STATE_ALERTS_DISPLAY);
    run(5100); assert(!in_order(STATE_ALERTS_DISPLAY));

    // ---- preview from the web UI: plays once, then back to where it was
    cfg.auto_cycle_enabled = false;
    engine_set_default_mode(STATE_TIME_DISPLAY); engine_rebuild_toggle_order(); run(1500);
    assert(engine_get_state() == STATE_TIME_DISPLAY);
    const uint8_t bg[] = {WARN_BG};
    assert(engine_preview_alerts(bg, 1));
    run(800); assert(engine_get_state() == STATE_ALERTS_DISPLAY);
    run(500); assert(icon_on_screen(ICON_BG));
    run(5000); run(800);
    assert(engine_get_state() == STATE_TIME_DISPLAY);
    // preview works with the alerts screen off, and not over a low
    cfg.alerts_display_enabled = false;
    const uint8_t tri[] = {WARN_ALERT};
    assert(engine_preview_alerts(tri, 1)); run(1200); assert(icon_on_screen(ICON_ALERT));
    run(6000);
    const uint8_t pause[] = {WARN_SUSPENDED};
    assert(engine_preview_alerts(pause, 1)); run(1200); assert(icon_on_screen(ICON_PAUSE));
    run(6000);
    cfg.glucose_only_when_low = true; reading.glucose = 65;
    assert(!engine_preview_alerts(tri, 1));
    cfg.glucose_only_when_low = false; reading.glucose = 129; run(2000);

    // ---- Full Mute: black during night hours, buzzer silent, nothing wakes it
    cfg.night_mode_enabled = true; cfg.night_start_hour = 16; cfg.night_end_hour = 8;
    cfg.night_brightness = 10; cfg.alert_enabled = true; cfg.alert_low = 80; cfg.alert_high = 250;
    engine_set_default_mode(STATE_GLUCOSE_DISPLAY); engine_rebuild_toggle_order();
    hour_now = 20; run(1500);
    assert(set_level == 10);                                                          // plain night mode: dimmed
    cfg.night_full_mute = true; run(200);
    assert(set_level == 0);                                                           // muted: black
    age_ms = 15UL * 60000; run(200); assert(set_level == 0);                          // no stale floor
    age_ms = 25UL * 60000; run(1500); assert(stale_level == 0);
    age_ms = 60000; reading.glucose = 55; beeps = 0; run(3000);                       // urgent low
    assert(set_level == 0 && beeps == 0);                                             // stays black and silent
    set_warnings({WARN_BATTERY}); engine_rebuild_toggle_order(); run(20000);
    assert(set_level == 0);
    hour_now = 9; run(1500);                                                          // school hours
    assert(set_level == 100 && beeps > 0);
    hour_now = 20; cfg.night_full_mute = false; beeps = 0; run(10500);                // night, not muted
    assert(set_level == 10 && beeps > 0);
    cfg.night_mode_enabled = false; reading.glucose = 129; set_warnings({});
    // ---- connection info names the .local address once mDNS is up
    engine_show_connection_info();
    assert(std::string(connection_info_buf) == "To connect, visit 192.0.2.1");
    local_host_stub = "keuken.local";
    engine_show_connection_info();
    assert(std::string(connection_info_buf) == "To connect, visit keuken.local or 192.0.2.1");
    engine_dismiss_connection_info();
    puts("bridge screens: ok");
    return 0;
}
