#include "web_server.h"
#include "config_manager.h"
#include "companion.h"
#include "wifi_manager.h"
#include "http_client.h"
#include "libre_client.h"
#include "glucose_engine.h"
#include "time_engine.h"
#include "sensors.h"
#include "display.h"
#include "timer_engine.h"
#include "countdown_engine.h"
#include "buzzer.h"
#include "buttons.h"
#include "hardware_pins.h"
#include "captive_portal.h"
#include "net_check.h"
#include "web_assets.h"
#include "ota_manager.h"
#include "bridge_alerts.h"
#include "clock_identity.h"
#include "clock_net.h"
#include <limits.h>

#include <ESPAsyncWebServer.h>
#include <WebResponseImpl.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Arduino.h>

static AsyncWebServer server(80);

// Web page password (optional). User name is always "pumpkin". A global
// middleware asks for it on every request; handlers that receive a body or an
// upload get their data *before* the middleware runs, so they check it
// themselves (and ignore the data) before acting on anything.
static const char* const WEB_USER = "pumpkin";

static bool web_auth_ok(AsyncWebServerRequest* request) {
    const AppConfig& cfg = config_get();
    return !cfg.web_password[0] || request->authenticate(WEB_USER, cfg.web_password);
}

static bool started = false;

#ifndef PUMPKIN_VERSION
#define PUMPKIN_VERSION "unknown"
#endif
#ifndef PUMPKIN_HARDWARE_ID
#define PUMPKIN_HARDWARE_ID "ulanzi-tc001-esp32-4mb"
#endif

// Helper: convert uint32_t RGB to "#RRGGBB" hex string
static String color_to_hex(uint32_t c) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    return String(buf);
}

// Helper: parse "#RRGGBB" hex string to uint32_t RGB
static uint32_t hex_to_color(const char* hex) {
    if (!hex || hex[0] != '#' || strlen(hex) < 7) return 0;
    return strtoul(hex + 1, NULL, 16);
}

// GET /api/status
static void handle_status(AsyncWebServerRequest* request) {
    JsonDocument doc;

    const GlucoseReading& r = http_get_reading();
    doc["glucose"] = r.valid ? r.glucose : 0;
    doc["trend"] = r.valid ? TREND_NAMES[r.trend] : "Unknown";
    doc["valid"] = r.valid;
    doc["data_age_sec"] = r.valid ? (millis() - r.received_at_ms) / 1000 : -1;
    doc["state"] = engine_state_name(engine_get_state());
    doc["glucose_display_locked"] = engine_low_glucose_lock_active();
    doc["wifi_connected"] = wifi_is_connected();
    doc["wifi_ip"] = wifi_get_ip();
    doc["wifi_rssi"] = wifi_get_rssi();
    doc["uptime_sec"] = time_get_uptime_sec();
    doc["failure_count"] = http_get_failure_count();
    doc["brightness"] = display_get_brightness();
    doc["message"] = r.message;
    OtaStatusSnapshot ota;
    ota_get_status(ota);
    doc["firmware_version"] = PUMPKIN_VERSION;
    doc["hardware"] = PUMPKIN_HARDWARE_ID;
    doc["running_partition"] = ota.running_partition;
    doc["boot_partition"] = ota.boot_partition;

    // Delta
    doc["delta"] = http_get_delta();

    // Glucose color info
    AppConfig& cfg = config_get();
    doc["use_mmol"] = cfg.use_mmol;
    unsigned long age = http_time_since_last_reading();
    unsigned long stale_ms = (unsigned long)cfg.stale_timeout_min * 60UL * 1000UL;
    int failures = http_get_failure_count();
    bool is_stale = (cfg.data_source != 2) && (age >= stale_ms || failures >= 5);

    if (r.valid) {
        if (is_stale) {
            doc["color"] = "gray";
        } else if (r.glucose < cfg.thresh_urgent_low || r.glucose > cfg.thresh_urgent_high) {
            doc["color"] = "red";
        } else if (r.glucose < cfg.thresh_low || r.glucose > cfg.thresh_high) {
            doc["color"] = "orange";
        } else {
            doc["color"] = "green";
        }
    } else {
        doc["color"] = "gray";
    }

    // Thresholds (for web UI graph)
    JsonObject thresholds = doc["thresholds"].to<JsonObject>();
    thresholds["urgent_low"] = cfg.thresh_urgent_low;
    thresholds["low"] = cfg.thresh_low;
    thresholds["high"] = cfg.thresh_high;
    thresholds["urgent_high"] = cfg.thresh_urgent_high;

    // Timer status
    doc["timer_state"] = (int)timer_get_state();
    doc["timer_remaining"] = timer_get_remaining_sec();
    doc["timer_session"] = timer_get_session();

    // Stopwatch status
    doc["stopwatch_state"] = (int)stopwatch_get_state();
    doc["stopwatch_elapsed"] = stopwatch_get_elapsed_sec();

    // Countdown
    if (cfg.countdown_enabled && countdown_is_configured()) {
        doc["countdown_remaining"] = countdown_get_remaining_sec();
        doc["countdown_name"] = cfg.countdown_name;
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

static void add_libre_people(JsonDocument& doc) {
    LibreConfigLock lock(config_libre_mutex());
    const AppConfig& cfg = config_get();
    doc["libre_patient_id"] = String(cfg.libre_patient_id);
    doc["libre_patient_name"] = String(cfg.libre_patient_name);
    LibrePatients snapshot = libre_get_patients();
    JsonArray list = doc["libre_patients"].to<JsonArray>();
    if (!snapshot) return;
    for (const LibrePatient& entry : snapshot->people) {
        JsonObject person = list.add<JsonObject>();
        // Explicit string copies: JSON serialization outlives this snapshot.
        person["id"] = String(entry.id);
        person["name"] = String(entry.name);
    }
}

// GET /api/config
// The settings the web UI reads; also the source for copying settings to
// other clocks (filtered there). Secrets are never part of it.
static void fill_config_doc(JsonDocument& doc) {
    LibreConfigLock lock(config_libre_mutex());
    AppConfig& cfg = config_get();

    // Secrets are never echoed back. This endpoint is reachable over plain HTTP,
    // including from an open setup AP, so the UI is told only whether a value is
    // set. POST treats an empty string as "leave unchanged", which is what makes
    // a round-trip through the form safe.
    doc["wifi_ssid"] = cfg.wifi_ssid;
    doc["has_wifi_password"] = strlen(cfg.wifi_password) > 0;
    doc["wifi_security"] = cfg.wifi_security;
    doc["wifi_eap_method"] = cfg.wifi_eap_method;
    doc["wifi_identity"] = cfg.wifi_identity;
    doc["has_wifi_eap_password"] = strlen(cfg.wifi_eap_password) > 0;
    doc["wifi_anon_identity"] = cfg.wifi_anon_identity;
    doc["wifi_validate_ca"] = cfg.wifi_validate_ca;
    doc["has_wifi_ca"] = config_ca_exists();
    doc["data_source"] = cfg.data_source;
    doc["server_url"] = cfg.server_url;
    doc["has_auth_token"] = strlen(cfg.auth_token) > 0;
    doc["dexcom_username"] = cfg.dexcom_username;
    doc["has_dexcom_password"] = strlen(cfg.dexcom_password) > 0;
    doc["dexcom_us"] = cfg.dexcom_us;
    doc["libre_email"] = cfg.libre_email;
    doc["has_libre_password"] = strlen(cfg.libre_password) > 0;
    doc["libre_region"] = cfg.libre_region;
    add_libre_people(doc);
    doc["poll_interval"] = cfg.poll_interval_sec;
    doc["brightness"] = cfg.brightness;
    doc["auto_brightness"] = cfg.auto_brightness;
    doc["glucose_only_when_low"] = cfg.glucose_only_when_low;
    doc["show_delta"] = cfg.show_delta;
    doc["use_mmol"] = cfg.use_mmol;
    doc["thresh_urgent_low"] = cfg.thresh_urgent_low;
    doc["thresh_low"] = cfg.thresh_low;
    doc["thresh_high"] = cfg.thresh_high;
    doc["thresh_urgent_high"] = cfg.thresh_urgent_high;
    doc["timezone"] = cfg.timezone;
    doc["use_24h"] = cfg.use_24h;
    doc["time_display_enabled"] = cfg.time_display_enabled;
    doc["trend_display_enabled"] = cfg.trend_display_enabled;
    doc["tir_display_enabled"] = cfg.tir_display_enabled;
    doc["iob_display_enabled"] = cfg.iob_display_enabled;
    doc["alerts_display_enabled"] = cfg.alerts_display_enabled;
    doc["alerts_replace_pet"] = cfg.alerts_replace_pet;
    doc["default_mode"] = cfg.default_mode;
    doc["ambient_enabled"] = cfg.ambient_enabled;
    doc["ambient_creature"] = cfg.ambient_character == COMPANION_GHOST ? 1 : 0;
    doc["ambient_character"] = cfg.ambient_character;
    doc["ambient_style"] = cfg.ambient_style;
    doc["ambient_use_glucose_colors"] = cfg.ambient_use_glucose_colors;
    doc["ambient_show_tir"] = cfg.ambient_show_tir;
    doc["ambient_seasonal"] = cfg.ambient_seasonal;

    // Alerts
    doc["alert_enabled"] = cfg.alert_enabled;
    doc["alert_low"] = cfg.alert_low;
    doc["alert_high"] = cfg.alert_high;
    doc["alert_snooze_min"] = cfg.alert_snooze_min;

    // Theme colors
    doc["color_urgent_low"] = color_to_hex(cfg.color_urgent_low);
    doc["color_low"] = color_to_hex(cfg.color_low);
    doc["color_in_range"] = color_to_hex(cfg.color_in_range);
    doc["color_high"] = color_to_hex(cfg.color_high);
    doc["color_urgent_high"] = color_to_hex(cfg.color_urgent_high);
    doc["color_stale"] = color_to_hex(cfg.color_stale);

    // Clock color
    doc["color_clock"] = color_to_hex(cfg.color_clock);

    // Night mode
    doc["night_mode_enabled"] = cfg.night_mode_enabled;
    doc["night_start_hour"] = cfg.night_start_hour;
    doc["night_end_hour"] = cfg.night_end_hour;
    doc["night_brightness"] = cfg.night_brightness;
    doc["night_full_mute"] = cfg.night_full_mute;

    // Stale timeout
    doc["stale_timeout_min"] = cfg.stale_timeout_min;

    // Date
    doc["date_on_time_screen"] = cfg.date_on_time_screen;
    doc["date_format"] = cfg.date_format;

    // Timer
    doc["timer_enabled"] = cfg.timer_enabled;
    doc["timer_work_min"] = cfg.timer_work_min;
    doc["timer_break_min"] = cfg.timer_break_min;
    doc["timer_long_break_min"] = cfg.timer_long_break_min;
    doc["timer_sessions"] = cfg.timer_sessions;
    doc["timer_buzzer"] = cfg.timer_buzzer;

    // Stopwatch
    doc["stopwatch_enabled"] = cfg.stopwatch_enabled;

    // Countdown
    doc["countdown_enabled"] = cfg.countdown_enabled;
    doc["countdown_name"] = cfg.countdown_name;
    doc["countdown_target"] = cfg.countdown_target;

    // Auto-cycle
    doc["auto_cycle_enabled"] = cfg.auto_cycle_enabled;
    doc["auto_cycle_sec"] = cfg.auto_cycle_sec;


    // This clock on the local network
    doc["clock_name"] = cfg.clock_name;
    doc["clock_display_name"] = clock_net_name();
    doc["clock_host"] = clock_net_local_host();
    doc["clock_id"] = clock_net_id();
    doc["has_web_password"] = cfg.web_password[0] != '\0';
}

static void handle_get_config(AsyncWebServerRequest* request) {
    JsonDocument doc;
    fill_config_doc(doc);
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/config (JSON body) — accumulate chunks before parsing
static char config_body[4096];

static void handle_post_config(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (!web_auth_ok(request)) return;   // answered with 401 by the middleware
    if (total > sizeof(config_body) - 1) {
        request->send(413, "application/json", "{\"error\":\"Body too large\"}");
        return;
    }
    memcpy(config_body + index, data, len);
    if (index + len < total) return; // wait for all chunks
    config_body[total] = '\0';

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, config_body, total);
    if (err) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    if (doc.as<JsonObjectConst>().containsKey("ambient_character") &&
        (!doc["ambient_character"].is<int>() || !companion_valid(doc["ambient_character"].as<int>()))) {
        request->send(400, "application/json", "{\"error\":\"Invalid companion selection\"}");
        return;
    }

    if (doc.as<JsonObjectConst>().containsKey("ambient_style") &&
        (!doc["ambient_style"].is<int>() || !companion_style_valid(doc["ambient_style"].as<int>()))) {
        request->send(400, "application/json", "{\"error\":\"Invalid companion display style\"}");
        return;
    }

    if (doc["web_password"].is<const char*>()) {
        const char* pw = doc["web_password"].as<const char*>();
        const size_t n = strlen(pw);
        bool printable = true;
        for (size_t i = 0; i < n; ++i) printable &= (unsigned char)pw[i] >= 0x20 && pw[i] != 0x7F;
        if (n > 0 && (n < 4 || n >= sizeof(config_get().web_password) || !printable)) {
            request->send(400, "application/json",
                          "{\"error\":\"Password: 4-64 characters, no control characters\"}");
            return;
        }
    }

    if (doc.as<JsonObjectConst>().containsKey("clock_name")) {
        // "" goes back to the default name; anything else must make a usable address
        const char* name = doc["clock_name"].is<const char*>() ? doc["clock_name"].as<const char*>() : nullptr;
        if (!name || (name[0] && !clock_name_valid(name))) {
            request->send(400, "application/json",
                          "{\"error\":\"Clock name: up to 32 characters, with at least one letter or digit\"}");
            return;
        }
    }

    if (doc.as<JsonObjectConst>().containsKey("ambient_use_glucose_colors") &&
        !doc["ambient_use_glucose_colors"].is<bool>()) {
        request->send(400, "application/json", "{\"error\":\"Invalid companion color setting\"}");
        return;
    }

    AppConfig& cfg = config_get();
    std::unique_lock<std::recursive_mutex> lock(config_libre_mutex());
    const char* requested_person = doc["libre_patient_id"] | "";
    const char* requested_email = doc["libre_email"] | cfg.libre_email;
    LibrePatient selected_person = {};
    // Validate before applying any settings. Never accept an arbitrary ID or
    // carry an old account's selection into a newly supplied account.
    if (strcmp(requested_email, cfg.libre_email) == 0 && requested_person[0] &&
        strcmp(requested_person, cfg.libre_patient_id) != 0) {
        LibrePatients snapshot = libre_get_patients();
        int index = snapshot ? libre_patient_index(snapshot->people.data(), snapshot->people.size(), requested_person)
                             : LIBRE_PATIENT_MISSING;
        if (index < 0) {
            request->send(400, "application/json", "{\"error\":\"Test the Libre connection, then select a person from the list\"}");
            return;
        }
        selected_person = snapshot->people[index];
    }

    if (doc["wifi_ssid"].is<const char*>()) {
        strncpy(cfg.wifi_ssid, doc["wifi_ssid"] | "", sizeof(cfg.wifi_ssid) - 1);
    }
    // Secret fields: an empty value means the form was rendered without it (see
    // the GET handler), so keep whatever is already stored.
    if (doc["wifi_password"].is<const char*>() && strlen(doc["wifi_password"] | "") > 0) {
        strncpy(cfg.wifi_password, doc["wifi_password"] | "", sizeof(cfg.wifi_password) - 1);
    }
    if (doc["wifi_security"].is<int>()) {
        cfg.wifi_security = constrain(doc["wifi_security"].as<int>(), 0, 1);
        if (cfg.wifi_security == 0) {
            cfg.wifi_eap_method = 0;
            cfg.wifi_identity[0] = '\0';
            cfg.wifi_eap_password[0] = '\0';
            cfg.wifi_anon_identity[0] = '\0';
            cfg.wifi_validate_ca = false;
        }
    }
    if (doc["wifi_eap_method"].is<int>()) {
        cfg.wifi_eap_method = constrain(doc["wifi_eap_method"].as<int>(), 0, 1);
    }
    if (doc["wifi_identity"].is<const char*>()) {
        strncpy(cfg.wifi_identity, doc["wifi_identity"] | "", sizeof(cfg.wifi_identity) - 1);
        cfg.wifi_identity[sizeof(cfg.wifi_identity) - 1] = '\0';
    }
    if (doc["wifi_eap_password"].is<const char*>() && strlen(doc["wifi_eap_password"] | "") > 0) {
        strncpy(cfg.wifi_eap_password, doc["wifi_eap_password"] | "", sizeof(cfg.wifi_eap_password) - 1);
        cfg.wifi_eap_password[sizeof(cfg.wifi_eap_password) - 1] = '\0';
    }
    if (doc["wifi_anon_identity"].is<const char*>()) {
        strncpy(cfg.wifi_anon_identity, doc["wifi_anon_identity"] | "", sizeof(cfg.wifi_anon_identity) - 1);
        cfg.wifi_anon_identity[sizeof(cfg.wifi_anon_identity) - 1] = '\0';
    }
    if (doc["data_source"].is<int>()) {
        cfg.data_source = doc["data_source"].as<int>();
    }
    if (doc["server_url"].is<const char*>()) {
        strncpy(cfg.server_url, doc["server_url"] | "", sizeof(cfg.server_url) - 1);
    }
    if (doc["auth_token"].is<const char*>() && strlen(doc["auth_token"] | "") > 0) {
        strncpy(cfg.auth_token, doc["auth_token"] | "", sizeof(cfg.auth_token) - 1);
    }
    if (doc["dexcom_username"].is<const char*>()) {
        strncpy(cfg.dexcom_username, doc["dexcom_username"] | "", sizeof(cfg.dexcom_username) - 1);
    }
    if (doc["dexcom_password"].is<const char*>() && strlen(doc["dexcom_password"] | "") > 0) {
        strncpy(cfg.dexcom_password, doc["dexcom_password"] | "", sizeof(cfg.dexcom_password) - 1);
    }
    if (doc["dexcom_us"].is<bool>()) {
        cfg.dexcom_us = doc["dexcom_us"].as<bool>();
    }
    char previous_email[sizeof(cfg.libre_email)];
    strcpy(previous_email, cfg.libre_email);
    bool libre_changed = config_update_libre_credentials(
        cfg, doc["libre_email"] | (const char*)nullptr, doc["libre_password"] | (const char*)nullptr);
    bool libre_person_changed = strcmp(previous_email, cfg.libre_email) != 0;
    if (!libre_person_changed && selected_person.id[0]) {
        strcpy(cfg.libre_patient_id, selected_person.id);
        strcpy(cfg.libre_patient_name, selected_person.name);
        libre_person_changed = true;
    }
    if (doc["poll_interval"].is<int>()) {
        cfg.poll_interval_sec = max(15, doc["poll_interval"].as<int>());
    }
    if (doc["brightness"].is<int>()) {
        cfg.brightness = constrain(doc["brightness"].as<int>(), 1, 255);
    }
    if (doc["auto_brightness"].is<bool>()) {
        cfg.auto_brightness = doc["auto_brightness"].as<bool>();
    }
    if (doc["glucose_only_when_low"].is<bool>()) {
        cfg.glucose_only_when_low = doc["glucose_only_when_low"].as<bool>();
    }
    if (doc["show_delta"].is<bool>()) {
        cfg.show_delta = doc["show_delta"].as<bool>();
    }
    if (doc["use_mmol"].is<bool>()) {
        cfg.use_mmol = doc["use_mmol"].as<bool>();
    }
    if (doc["thresh_urgent_low"].is<int>()) {
        cfg.thresh_urgent_low = doc["thresh_urgent_low"].as<int>();
    }
    if (doc["thresh_low"].is<int>()) {
        cfg.thresh_low = doc["thresh_low"].as<int>();
    }
    if (doc["thresh_high"].is<int>()) {
        cfg.thresh_high = doc["thresh_high"].as<int>();
    }
    if (doc["thresh_urgent_high"].is<int>()) {
        cfg.thresh_urgent_high = doc["thresh_urgent_high"].as<int>();
    }
    if (doc["timezone"].is<const char*>()) {
        strncpy(cfg.timezone, doc["timezone"] | "", sizeof(cfg.timezone) - 1);
    }
    if (doc["use_24h"].is<bool>()) {
        cfg.use_24h = doc["use_24h"].as<bool>();
    }
    if (doc["time_display_enabled"].is<bool>()) {
        cfg.time_display_enabled = doc["time_display_enabled"].as<bool>();
    }
    if (doc["trend_display_enabled"].is<bool>()) {
        cfg.trend_display_enabled = doc["trend_display_enabled"].as<bool>();
    }
    if (doc["tir_display_enabled"].is<bool>()) {
        cfg.tir_display_enabled = doc["tir_display_enabled"].as<bool>();
    }
    if (doc["iob_display_enabled"].is<bool>()) {
        cfg.iob_display_enabled = doc["iob_display_enabled"].as<bool>();
    }
    if (doc["alerts_display_enabled"].is<bool>()) {
        cfg.alerts_display_enabled = doc["alerts_display_enabled"].as<bool>();
    }
    if (doc["alerts_replace_pet"].is<bool>()) {
        cfg.alerts_replace_pet = doc["alerts_replace_pet"].as<bool>();
    }
    if (doc["default_mode"].is<int>()) {
        cfg.default_mode = constrain(doc["default_mode"].as<int>(), 0, 3);
    }
    if (doc["ambient_enabled"].is<bool>()) {
        cfg.ambient_enabled = doc["ambient_enabled"].as<bool>();
    }
    if (doc["ambient_use_glucose_colors"].is<bool>()) {
        cfg.ambient_use_glucose_colors = doc["ambient_use_glucose_colors"].as<bool>();
    }
    if (doc["ambient_show_tir"].is<bool>()) {
        cfg.ambient_show_tir = doc["ambient_show_tir"].as<bool>();
    }
    if (doc["ambient_style"].is<int>()) {
        cfg.ambient_style = doc["ambient_style"].as<int>();
    }
    if (doc["ambient_creature"].is<int>() && !doc.as<JsonObjectConst>().containsKey("ambient_character")) {
        cfg.ambient_character = constrain(doc["ambient_creature"].as<int>(), 0, 1);
    }
    if (doc["ambient_character"].is<int>()) {
        cfg.ambient_character = doc["ambient_character"].as<int>();
    }
    if (doc["ambient_seasonal"].is<bool>()) {
        cfg.ambient_seasonal = doc["ambient_seasonal"].as<bool>();
    }
    if (cfg.default_mode == 2) cfg.default_mode = 0;   // 2 was weather (removed in 0.10)
    if (!cfg.time_display_enabled && cfg.default_mode == 1) {
        cfg.default_mode = 0;
    }
    if (!cfg.ambient_enabled && cfg.default_mode == 3) {
        cfg.default_mode = 0;
    }

    // Alerts
    if (doc["alert_enabled"].is<bool>()) {
        cfg.alert_enabled = doc["alert_enabled"].as<bool>();
    }
    if (doc["alert_low"].is<int>()) {
        cfg.alert_low = doc["alert_low"].as<int>();
    }
    if (doc["alert_high"].is<int>()) {
        cfg.alert_high = doc["alert_high"].as<int>();
    }
    if (doc["alert_snooze_min"].is<int>()) {
        cfg.alert_snooze_min = constrain(doc["alert_snooze_min"].as<int>(), 1, 120);
    }

    // Theme colors
    if (doc["color_urgent_low"].is<const char*>()) {
        cfg.color_urgent_low = hex_to_color(doc["color_urgent_low"] | "#ea4335");
    }
    if (doc["color_low"].is<const char*>()) {
        cfg.color_low = hex_to_color(doc["color_low"] | "#fbbc04");
    }
    if (doc["color_in_range"].is<const char*>()) {
        cfg.color_in_range = hex_to_color(doc["color_in_range"] | "#34a853");
    }
    if (doc["color_high"].is<const char*>()) {
        cfg.color_high = hex_to_color(doc["color_high"] | "#fbbc04");
    }
    if (doc["color_urgent_high"].is<const char*>()) {
        cfg.color_urgent_high = hex_to_color(doc["color_urgent_high"] | "#ea4335");
    }
    if (doc["color_stale"].is<const char*>()) {
        cfg.color_stale = hex_to_color(doc["color_stale"] | "#808080");
    }

    // Clock color
    if (doc["color_clock"].is<const char*>()) {
        cfg.color_clock = hex_to_color(doc["color_clock"] | "#ffffff");
    }

    // Night mode
    if (doc["night_mode_enabled"].is<bool>()) {
        cfg.night_mode_enabled = doc["night_mode_enabled"].as<bool>();
    }
    if (doc["night_start_hour"].is<int>()) {
        cfg.night_start_hour = constrain(doc["night_start_hour"].as<int>(), 0, 23);
    }
    if (doc["night_end_hour"].is<int>()) {
        cfg.night_end_hour = constrain(doc["night_end_hour"].as<int>(), 0, 23);
    }
    if (doc["night_brightness"].is<int>()) {
        cfg.night_brightness = constrain(doc["night_brightness"].as<int>(), 1, 255);
    }
    if (doc["night_full_mute"].is<bool>()) {
        cfg.night_full_mute = doc["night_full_mute"].as<bool>();
    }
    // Web page password: a new one replaces the old; "web_password_clear"
    // removes it. An empty field leaves it as it is.
    if (doc["web_password"].is<const char*>() && strlen(doc["web_password"] | "") > 0) {
        strncpy(cfg.web_password, doc["web_password"] | "", sizeof(cfg.web_password) - 1);
        cfg.web_password[sizeof(cfg.web_password) - 1] = '\0';
    } else if (doc["web_password_clear"] | false) {
        cfg.web_password[0] = '\0';
    }

    bool clock_name_changed = false;
    if (doc["clock_name"].is<const char*>()) {
        const char* name = doc["clock_name"] | "";   // validated above
        if (strcmp(name, cfg.clock_name) != 0) {
            strncpy(cfg.clock_name, name, sizeof(cfg.clock_name) - 1);
            cfg.clock_name[sizeof(cfg.clock_name) - 1] = '\0';
            clock_name_changed = true;
        }
    }

    // Stale timeout
    if (doc["stale_timeout_min"].is<int>()) {
        cfg.stale_timeout_min = constrain(doc["stale_timeout_min"].as<int>(), 5, 60);
    }

    // Date
    if (doc["date_on_time_screen"].is<bool>()) {
        cfg.date_on_time_screen = doc["date_on_time_screen"].as<bool>();
    }
    if (doc["date_format"].is<int>()) {
        cfg.date_format = constrain(doc["date_format"].as<int>(), 0, 2);
    }

    // Timer
    if (doc["timer_enabled"].is<bool>()) {
        cfg.timer_enabled = doc["timer_enabled"].as<bool>();
    }
    if (doc["timer_work_min"].is<int>()) {
        cfg.timer_work_min = constrain(doc["timer_work_min"].as<int>(), 1, 120);
    }
    if (doc["timer_break_min"].is<int>()) {
        cfg.timer_break_min = constrain(doc["timer_break_min"].as<int>(), 1, 60);
    }
    if (doc["timer_long_break_min"].is<int>()) {
        cfg.timer_long_break_min = constrain(doc["timer_long_break_min"].as<int>(), 1, 60);
    }
    if (doc["timer_sessions"].is<int>()) {
        cfg.timer_sessions = constrain(doc["timer_sessions"].as<int>(), 1, 12);
    }
    if (doc["timer_buzzer"].is<bool>()) {
        cfg.timer_buzzer = doc["timer_buzzer"].as<bool>();
    }

    // Stopwatch
    if (doc["stopwatch_enabled"].is<bool>()) {
        cfg.stopwatch_enabled = doc["stopwatch_enabled"].as<bool>();
    }

    // Countdown
    if (doc["countdown_enabled"].is<bool>()) {
        cfg.countdown_enabled = doc["countdown_enabled"].as<bool>();
    }
    if (doc["countdown_name"].is<const char*>()) {
        strncpy(cfg.countdown_name, doc["countdown_name"] | "", sizeof(cfg.countdown_name) - 1);
        cfg.countdown_name[sizeof(cfg.countdown_name) - 1] = '\0';
    }
    if (doc["countdown_target"].is<unsigned long>()) {
        cfg.countdown_target = doc["countdown_target"].as<unsigned long>();
    }

    // Auto-cycle
    if (doc["auto_cycle_enabled"].is<bool>()) {
        cfg.auto_cycle_enabled = doc["auto_cycle_enabled"].as<bool>();
    }
    if (doc["auto_cycle_sec"].is<int>()) {
        cfg.auto_cycle_sec = constrain(doc["auto_cycle_sec"].as<int>(), 3, 300);
    }

    config_save();
    engine_rebuild_toggle_order();
    if (clock_name_changed) clock_net_name_changed();
    if (libre_changed) libre_reset_session();
    if (libre_person_changed && cfg.data_source == 3) http_clear_readings();
    lock.unlock();

    // Apply brightness immediately
    if (!cfg.auto_brightness) {
        display_set_brightness(cfg.brightness);
    }

    // If WiFi credentials were just saved from the setup portal while the STA is
    // still offline, reboot to pick them up. When the trial flow already got us
    // online there is nothing to restart for.
    if (wifi_is_ap_mode() && !wifi_is_connected() && config_has_wifi()) {
        request->send(200, "application/json", "{\"status\":\"ok\",\"reboot\":true}");
        delay(1000);
        ESP.restart();
        return;
    }

    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

static String frame_etag(uint32_t epoch, uint32_t sequence) {
    char value[48];
    snprintf(value, sizeof(value), "\"frame-%08lx-%lu\"", (unsigned long)epoch, (unsigned long)sequence);
    return String(value);
}

// Own pixels inside the response object: no growable 768-byte String and no
// reference to a render buffer that might be reused before TCP finishes sending.
class DisplayFrameResponse final : public AsyncAbstractResponse {
    DisplayFrame frame_;
    size_t offset_ = 0;
public:
    explicit DisplayFrameResponse(uint32_t epoch) {
        display_copy_frame(frame_);
        _code = 200;
        _contentType = "application/octet-stream";
        _contentLength = sizeof(frame_.rgb);
        addHeader("ETag", frame_etag(epoch, frame_.sequence));
        addHeader("X-Display-Sequence", String(frame_.sequence));
    }
    bool _sourceValid() const override { return true; }
    size_t _fillBuffer(uint8_t* buffer, size_t max_len) override {
        const size_t remaining = sizeof(frame_.rgb) - offset_;
        const size_t count = max_len < remaining ? max_len : remaining;
        memcpy(buffer, frame_.rgb + offset_, count);
        offset_ += count;
        return count;
    }
};

// GET /api/display/frame: renew demand before checking the conditional ETag.
static void handle_display_frame(AsyncWebServerRequest* request) {
    const DisplayFrameStatus frame = display_request_frame();
    AsyncWebServerResponse* response;
    if (!frame.ready) {
        response = request->beginResponse(204); // Render loop will capture a fresh frame shortly.
    } else if (request->header("If-None-Match") == frame_etag(frame.epoch, frame.sequence)) {
        response = request->beginResponse(304);
        response->addHeader("ETag", frame_etag(frame.epoch, frame.sequence));
        response->addHeader("X-Display-Sequence", String(frame.sequence));
    } else {
        response = new DisplayFrameResponse(frame.epoch);
    }
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Display-Mode", engine_state_name(engine_get_state()));
    request->send(response);
}

// GET /api/debug
static void handle_debug(AsyncWebServerRequest* request) {
    JsonDocument doc;

    doc["last_http_code"] = http_get_last_response_code();
    doc["last_http_body"] = http_get_last_response_body();
    doc["failure_count"] = http_get_failure_count();
    doc["ever_received"] = http_has_ever_received();
    doc["wifi_rssi"] = wifi_get_rssi();
    doc["wifi_status"] = wifi_get_status();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["min_free_heap"] = ESP.getMinFreeHeap();
    doc["largest_free_block"] = ESP.getMaxAllocHeap();
    doc["uptime_sec"] = time_get_uptime_sec();
    doc["display_state"] = engine_state_name(engine_get_state());
    doc["ldr_raw"] = sensors_get_ldr();
    doc["auto_brightness_val"] = sensors_get_auto_brightness();
    doc["battery_voltage"] = sensors_get_battery_voltage();
    doc["battery_percent"] = sensors_get_battery_percent();

    // Button GPIO states (LOW = pressed on active-LOW buttons)
    doc["btn_left_raw"] = digitalRead(PIN_BUTTON_LEFT);
    doc["btn_middle_raw"] = digitalRead(PIN_BUTTON_MIDDLE);
    doc["btn_right_raw"] = digitalRead(PIN_BUTTON_RIGHT);
    doc["user_mode"] = engine_state_name(engine_get_user_mode());

    // MAC address
    doc["mac"] = WiFi.macAddress();

    // LittleFS info
    doc["fs_used"] = LittleFS.usedBytes();
    doc["fs_total"] = LittleFS.totalBytes();

    unsigned long age = http_time_since_last_reading();
    doc["data_age_ms"] = (age == ULONG_MAX) ? -1 : (long)age;

    const GlucoseReading& r = http_get_reading();
    if (r.valid) {
        doc["raw_glucose"] = r.glucose;
        doc["raw_trend"] = TREND_NAMES[r.trend];
        doc["raw_message"] = r.message;
        doc["raw_force_mode"] = r.force_mode;
        doc["raw_delta"] = http_get_delta();
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// GET /api/history
static void handle_history(AsyncWebServerRequest* request) {
    JsonDocument doc;

    GlucoseHistoryEntry entries[GLUCOSE_HISTORY_SIZE];
    int count = http_get_history(entries, GLUCOSE_HISTORY_SIZE);

    JsonArray readings = doc["readings"].to<JsonArray>();
    for (int i = 0; i < count; i++) {
        JsonObject entry = readings.add<JsonObject>();
        entry["glucose"] = entries[i].glucose;
        entry["delta"] = entries[i].delta;
        entry["ts"] = entries[i].timestamp;
    }
    doc["count"] = count;

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// GET /api/timer
static void handle_timer_status(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["timer_state"] = (int)timer_get_state();
    doc["timer_remaining"] = timer_get_remaining_sec();
    doc["timer_session"] = timer_get_session();
    doc["timer_total_sessions"] = timer_get_total_sessions();
    doc["stopwatch_state"] = (int)stopwatch_get_state();
    doc["stopwatch_elapsed"] = stopwatch_get_elapsed_sec();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// GET /api/update/status
static void handle_update_status(AsyncWebServerRequest* request) {
    OtaStatusSnapshot snapshot;
    ota_get_status(snapshot);
    JsonDocument doc;
    doc["current_version"] = snapshot.current_version;
    doc["state"] = ota_state_name(snapshot.state);
    doc["progress"] = snapshot.progress;
    doc["last_error"] = snapshot.last_error;
    doc["running_partition"] = snapshot.running_partition;
    doc["boot_partition"] = snapshot.boot_partition;
    doc["pending_verification"] = snapshot.pending_verification;
    doc["rolled_back"] = snapshot.rolled_back;
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/update?size=<bytes> (multipart, one file): install a firmware.bin.
// The data arrives before the password middleware runs, so the upload callback
// checks the password itself and ignores the data when it's wrong.
static AsyncWebServerRequest* update_owner = nullptr;

static void handle_update_upload(AsyncWebServerRequest* request, const String& filename, size_t index,
                                 uint8_t* data, size_t len, bool final) {
    if (!web_auth_ok(request)) return;   // the middleware answers 401 afterwards
    if (index == 0) {
        if (update_owner) return;        // another upload is running; answered with 409
        size_t size = 0;
        if (request->hasParam("size")) {
            size = strtoul(request->getParam("size")->value().c_str(), nullptr, 10);
        }
        if (!ota_upload_begin(size)) return;
        update_owner = request;
        request->onDisconnect([]() {
            // The browser went away mid-upload: drop the partial image.
            OtaStatusSnapshot snapshot;
            ota_get_status(snapshot);
            if (snapshot.state == OTA_RECEIVING) ota_upload_abort("upload_interrupted");
            update_owner = nullptr;
        });
        Serial.printf("[WEB] Firmware upload: %s\n", filename.c_str());
    }
    if (request != update_owner) return;
    if (len && !ota_upload_write(data, len)) return;
    if (final) ota_upload_end();
}

static void handle_update_done(AsyncWebServerRequest* request) {
    OtaStatusSnapshot snapshot;
    ota_get_status(snapshot);
    if (request == update_owner) {
        update_owner = nullptr;
        if (snapshot.state == OTA_PENDING_REBOOT) {
            request->send(200, "application/json", "{\"status\":\"ok\",\"restarting\":true}");
            return;
        }
        if (snapshot.state == OTA_RECEIVING) ota_upload_abort("upload_incomplete");
        ota_get_status(snapshot);
    } else if (ota_is_busy()) {
        request->send(409, "application/json", "{\"error\":\"update_already_running\"}");
        return;
    }
    JsonDocument doc;
    doc["error"] = snapshot.last_error[0] ? snapshot.last_error : "no_firmware_received";
    String output;
    serializeJson(doc, output);
    request->send(400, "application/json", output);
}

// POST /api/test/alert {"id": "suspended"|"warning"|"battery"|"reservoir"|"bg"|"sensor"|"set"|"all"}
// Plays the pump alert(s) once on the display, then returns to the previous screen.
static void handle_test_alert(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (!web_auth_ok(request)) return;   // answered with 401 by the middleware
    if (index != 0) return;

    JsonDocument doc;
    if (deserializeJson(doc, data, len)) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    const char* id = doc["id"] | "";
    uint8_t items[BRIDGE_MAX_WARNINGS];
    uint8_t count = 0;
    if (strcmp(id, "all") == 0) {
        static const uint8_t ALL[] = {WARN_SUSPENDED, WARN_ALERT, WARN_BATTERY, WARN_RESERVOIR,
                                      WARN_BG, WARN_SENSOR, WARN_SET};
        static_assert(sizeof(ALL) <= BRIDGE_MAX_WARNINGS, "preview list size");
        memcpy(items, ALL, sizeof(ALL));
        count = sizeof(ALL);
    } else {
        uint8_t w = bridge_warning_from_id(id);
        if (w == WARN_NONE) {
            request->send(400, "application/json", "{\"error\":\"Unknown alert id\"}");
            return;
        }
        items[count++] = w;
    }
    if (!engine_preview_alerts(items, count)) {
        request->send(409, "application/json", "{\"error\":\"Low glucose is being shown\"}");
        return;
    }
    JsonDocument resp;
    resp["status"] = "ok";
    resp["id"] = id;
    resp["count"] = count;
    String output;
    serializeJson(resp, output);
    request->send(200, "application/json", output);
}

// GET /api/clocks: this clock and the other Pumpkin clocks it sees on the network.
static void handle_get_clocks(AsyncWebServerRequest* request) {
    JsonDocument doc;
    JsonObject self = doc["self"].to<JsonObject>();
    self["name"] = clock_net_name();
    self["host"] = clock_net_local_host();
    self["ip"] = wifi_get_ip();
    self["id"] = clock_net_id();
    self["version"] = PUMPKIN_VERSION;
    doc["scanning"] = clock_net_scanning();
    const unsigned long age = clock_net_last_scan_age_ms();
    if (age == ULONG_MAX) doc["scan_age_s"] = nullptr;
    else doc["scan_age_s"] = age / 1000UL;
    JsonArray clocks = doc["clocks"].to<JsonArray>();
    ClockPeer peers[CLOCK_NET_MAX_PEERS];
    const int n = clock_net_peers(peers, CLOCK_NET_MAX_PEERS);
    for (int i = 0; i < n; ++i) {
        JsonObject c = clocks.add<JsonObject>();
        c["name"] = peers[i].name;
        c["host"] = peers[i].host;
        c["ip"] = peers[i].ip;
        c["version"] = peers[i].version;
        c["id"] = peers[i].id;
    }
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/clocks/copy {"targets": ["192.168.0.12", ...], "night": false, "source": false}
// Sends this clock's display and alert settings to the chosen clocks; night
// mode / Full Mute hours and the data source (URL + token) only when asked.
// Runs on the clock-network task; poll GET /api/clocks/copy for the result.
static void handle_copy_clocks(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (!web_auth_ok(request)) return;   // answered with 401 by the middleware
    if (index != 0 || len != total) {
        if (index == 0) request->send(413, "application/json", "{\"error\":\"Request too large\"}");
        return;
    }
    JsonDocument req;
    if (deserializeJson(req, data, len)) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    const char* targets[CLOCK_NET_MAX_PEERS];
    int count = 0;
    for (JsonVariantConst t : req["targets"].as<JsonArrayConst>()) {
        if (count >= CLOCK_NET_MAX_PEERS || !t.is<const char*>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid target list\"}");
            return;
        }
        targets[count++] = t.as<const char*>();
    }
    if (count == 0) {
        request->send(400, "application/json", "{\"error\":\"Choose at least one clock\"}");
        return;
    }
    const bool night = req["night"] | false;
    const bool source = req["source"] | false;
    const AppConfig& cfg = config_get();
    if (source && cfg.data_source != 0) {
        request->send(400, "application/json",
                      "{\"error\":\"Only a Nightscout / custom URL data source can be copied\"}");
        return;
    }

    JsonDocument all;
    fill_config_doc(all);
    JsonDocument out;
    for (JsonPairConst kv : all.as<JsonObjectConst>()) {
        const uint8_t group = clock_copy_group(kv.key().c_str());
        if (group == CLOCK_COPY_DISPLAY || (night && group == CLOCK_COPY_NIGHT) ||
            (source && group == CLOCK_COPY_SOURCE)) {
            out[kv.key().c_str()] = kv.value();
        }
    }
    if (source && cfg.auth_token[0]) out["auth_token"] = cfg.auth_token;
    String body;
    serializeJson(out, body);

    if (!clock_net_copy_start(targets, count, body.c_str())) {
        request->send(409, "application/json",
                      "{\"error\":\"A copy is still running, or a clock is no longer on the list. Refresh and try again\"}");
        return;
    }
    request->send(202, "application/json", "{\"status\":\"pending\"}");
}

// GET /api/clocks/copy: {"running": bool, "results": [{"ip","name","status","code"}]}
static void handle_copy_clocks_status(AsyncWebServerRequest* request) {
    ClockCopyResult results[CLOCK_NET_MAX_PEERS];
    bool running = false;
    const int n = clock_net_copy_results(results, CLOCK_NET_MAX_PEERS, &running);
    JsonDocument doc;
    doc["running"] = running;
    JsonArray arr = doc["results"].to<JsonArray>();
    for (int i = 0; i < n; ++i) {
        JsonObject r = arr.add<JsonObject>();
        r["ip"] = results[i].ip;
        r["name"] = results[i].name;
        r["status"] = results[i].state == CLOCK_COPY_OK ? "ok"
                    : results[i].state == CLOCK_COPY_FAILED ? "failed" : "pending";
        r["code"] = results[i].http_code;
    }
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/alerts/clear: ask the bridge to clear its remembered pump reminders
// (for a reminder that didn't clear by itself). Runs on the HTTP loop; poll
// GET /api/alerts/clear for the result.
static void handle_clear_alerts(AsyncWebServerRequest* request) {
    if (!http_request_clear_reminders()) {
        request->send(400, "application/json",
                      "{\"error\":\"The data source is not a bridge /clock URL\"}");
        return;
    }
    request->send(202, "application/json", "{\"status\":\"pending\"}");
}

// GET /api/alerts/clear: {"status":"idle"|"pending"|"cleared"|"nothing"|"error","code":<http>}
static void handle_clear_alerts_status(AsyncWebServerRequest* request) {
    const int code = http_clear_reminders_status();
    JsonDocument resp;
    resp["status"] = code == 0 ? "idle" : code == 1 ? "pending" : code == 200 ? "cleared"
                   : code == 404 ? "nothing" : "error";
    resp["code"] = code;
    String output;
    serializeJson(resp, output);
    request->send(200, "application/json", output);
}

// POST /api/test/glucose
static void handle_test_glucose(AsyncWebServerRequest* request) {
    AppConfig& cfg = config_get();

    // Demo mode: no network involved, just return a synthetic reading.
    if (cfg.data_source == 2) {
        http_force_fetch();
        const GlucoseReading& r = http_get_reading();
        JsonDocument doc;
        doc["ok"] = true;
        doc["http_code"] = 200;
        doc["glucose"] = r.glucose;
        doc["trend"] = TREND_NAMES[r.trend];
        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
        return;
    }

    if (!wifi_is_connected()) {
        request->send(503, "application/json", "{\"ok\":false,\"error\":\"WiFi not connected\"}");
        return;
    }
    if (!config_has_server()) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"No data source configured\"}");
        return;
    }

    bool ok = http_force_fetch();
    JsonDocument doc;
    doc["ok"] = ok;
    if (cfg.data_source == 3) add_libre_people(doc);
    doc["http_code"] = http_get_last_response_code();

    if (ok) {
        const GlucoseReading& r = http_get_reading();
        doc["glucose"] = r.glucose;
        doc["trend"] = TREND_NAMES[r.trend];
    } else {
        int code = http_get_last_response_code();
        const char* body = http_get_last_response_body();
        if (code > 0 && strlen(body) > 0) {
            char err[384];
            snprintf(err, sizeof(err), "HTTP %d: %s", code, body);
            doc["error"] = err;
        } else if (code > 0) {
            char err[32];
            snprintf(err, sizeof(err), "HTTP %d", code);
            doc["error"] = err;
        } else if (strlen(body) > 0) {
            doc["error"] = body;
        } else {
            doc["error"] = "Connection failed";
        }
    }

    String output;
    serializeJson(doc, output);
    request->send(ok ? 200 : (http_get_last_response_code() == 429 ? 429 : 502), "application/json", output);
}

// POST /api/display/next or /prev. Report the result of the navigation attempt,
// so a blocked command cannot be mistaken for a successful screen change.
static void handle_display_navigation(AsyncWebServerRequest* request, bool forward) {
    const bool changed = forward ? engine_toggle_mode() : engine_toggle_mode_prev();
    JsonDocument doc;
    doc["status"] = changed ? "ok" : "locked";
    doc["locked"] = !changed;
    doc["mode"] = engine_state_name(engine_get_user_mode());
    if (!changed) {
        doc["error"] = "Blood sugar display is locked while glucose is low.";
        doc["reason"] = "low_glucose";
    }
    String output;
    serializeJson(doc, output);
    request->send(changed ? 200 : 409, "application/json", output);
}

static void handle_display_next(AsyncWebServerRequest* request) {
    handle_display_navigation(request, true);
}

static void handle_display_prev(AsyncWebServerRequest* request) {
    handle_display_navigation(request, false);
}

// POST /api/restart
static void handle_restart(AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"status\":\"restarting\"}");
    delay(500);
    ESP.restart();
}

// POST /api/factory-reset
static void handle_factory_reset(AsyncWebServerRequest* request) {
    config_reset();
    request->send(200, "application/json", "{\"status\":\"factory reset, restarting\"}");
    delay(500);
    ESP.restart();
}


// ---------------------------------------------------------------------------
// WiFi setup portal API
//
// ESPAsyncWebServer runs handlers on the async TCP task; blocking there stalls
// every other connection and can trip the watchdog. Nothing below waits on the
// radio — scans and connection attempts are queued and driven from wifi_loop().

static const char* wifi_state_name(WifiState st) {
    switch (st) {
        case WIFI_ST_CONNECTING: return "connecting";
        case WIFI_ST_CONNECTED:  return "connected";
        case WIFI_ST_RETRY_WAIT: return "retry_wait";
        case WIFI_ST_SETUP_AP:   return "setup_ap";
        default:                 return "idle";
    }
}

static const char* netcheck_name(NetCheckResult r) {
    switch (r) {
        case NC_OK:   return "ok";
        case NC_FAIL: return "fail";
        default:      return "unknown";
    }
}

// GET /api/wifi/scan — cached results only; scanning is never background-polled
static void handle_wifi_scan(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["scanning"] = wifi_scan_in_progress();
    doc["age_ms"] = wifi_scan_age_ms();

    JsonArray arr = doc["networks"].to<JsonArray>();
    for (int i = 0; i < wifi_scan_count(); i++) {
        const WifiScanEntry* e = wifi_scan_get(i);
        if (!e) continue;
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = e->ssid;
        o["rssi"] = e->rssi;
        o["channel"] = e->channel;
        o["enc"] = e->enc;
        o["enterprise"] = e->enterprise;
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/wifi/scan/refresh — kicks off an async scan and returns immediately
static void handle_wifi_scan_refresh(AsyncWebServerRequest* request) {
    bool ok = wifi_scan_start();
    JsonDocument doc;
    doc["started"] = ok;
    doc["scanning"] = wifi_scan_in_progress();
    if (!ok) doc["error"] = "A scan is already running";
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// GET /api/wifi/status — poll target for the portal page
static void handle_wifi_status(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["state"] = wifi_state_name(wifi_get_state());
    doc["trial"] = wifi_trial_status_str();
    doc["trial_ssid"] = wifi_trial_ssid();
    doc["detail"] = wifi_trial_detail();
    doc["connected"] = wifi_is_connected();
    doc["ip"] = wifi_get_ip();
    doc["rssi"] = wifi_get_rssi();
    doc["ssid"] = config_get().wifi_ssid;
    doc["enterprise"] = config_has_enterprise();
    doc["ap_active"] = wifi_is_ap_mode();
    doc["ap_ssid"] = wifi_get_ap_ssid();
    doc["ap_ip"] = wifi_get_ap_ip();
    doc["ap_stations"] = wifi_ap_station_count();

    JsonObject checks = doc["checks"].to<JsonObject>();
    checks["dns"] = netcheck_name(netcheck_dns());
    checks["data"] = netcheck_name(netcheck_data());
    checks["ntp"] = netcheck_name(netcheck_ntp());
    checks["host"] = netcheck_data_host();
    checks["summary"] = netcheck_summary();
    checks["running"] = netcheck_running();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

// POST /api/wifi/connect — start a trial. Credentials are not persisted here;
// wifi_loop() writes them to NVS only once the join actually succeeds.
static char wifi_body[1024];

static void handle_wifi_connect(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (!web_auth_ok(request)) return;   // answered with 401 by the middleware
    if (total > sizeof(wifi_body) - 1) {
        request->send(413, "application/json", "{\"error\":\"Body too large\"}");
        return;
    }
    memcpy(wifi_body + index, data, len);
    if (index + len < total) return;
    wifi_body[total] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, wifi_body, total)) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    WifiTrialParams p;
    memset(&p, 0, sizeof(p));
    strncpy(p.ssid, doc["ssid"] | "", sizeof(p.ssid) - 1);
    p.security = doc["security"] | 0;
    p.eap_method = doc["eap_method"] | 0;
    strncpy(p.identity, doc["identity"] | "", sizeof(p.identity) - 1);
    strncpy(p.password, doc["password"] | "", sizeof(p.password) - 1);
    strncpy(p.anon_identity, doc["anon_identity"] | "", sizeof(p.anon_identity) - 1);
    p.validate_ca = doc["validate_ca"] | false;

    if (p.ssid[0] == '\0') {
        request->send(400, "application/json", "{\"error\":\"ssid is required\"}");
        return;
    }
    if (strlen(p.ssid) > 32) {
        request->send(400, "application/json", "{\"error\":\"ssid must be 32 characters or fewer\"}");
        return;
    }
    if (p.security != 0 && p.security != 1) {
        request->send(400, "application/json", "{\"error\":\"unsupported security type\"}");
        return;
    }
    if (p.eap_method != 0 && p.eap_method != 1) {
        request->send(400, "application/json", "{\"error\":\"unsupported EAP method\"}");
        return;
    }
    if (p.security == 1 && p.identity[0] == '\0') {
        request->send(400, "application/json", "{\"error\":\"identity is required for enterprise networks\"}");
        return;
    }

    if (p.validate_ca && !config_ca_exists()) {
        request->send(400, "application/json", "{\"error\":\"upload a CA certificate before enabling validation\"}");
        return;
    }

    if (!wifi_trial_start(p)) {
        request->send(409, "application/json", "{\"error\":\"A connection attempt is already running\"}");
        return;
    }

    request->send(202, "application/json", "{\"status\":\"started\"}");
}

// POST /api/wifi/ca — raw PEM body
static char ca_body[4096];

static void handle_wifi_ca_upload(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (!web_auth_ok(request)) return;   // answered with 401 by the middleware
    if (total > sizeof(ca_body) - 1) {
        request->send(413, "application/json", "{\"error\":\"Certificate too large (4 KB max)\"}");
        return;
    }
    memcpy(ca_body + index, data, len);
    if (index + len < total) return;
    ca_body[total] = '\0';

    if (!strstr(ca_body, "-----BEGIN CERTIFICATE-----") ||
        !strstr(ca_body, "-----END CERTIFICATE-----")) {
        request->send(400, "application/json", "{\"error\":\"Expected a PEM certificate\"}");
        return;
    }
    if (!config_ca_write(ca_body, total)) {
        request->send(500, "application/json", "{\"error\":\"Failed to store certificate\"}");
        return;
    }
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// DELETE /api/wifi/ca
static void handle_wifi_ca_delete(AsyncWebServerRequest* request) {
    if (!config_ca_delete()) {
        request->send(500, "application/json", "{\"error\":\"Failed to remove certificate\"}");
        return;
    }
    AppConfig& cfg = config_get();
    cfg.wifi_validate_ca = false;
    config_save();
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void webserver_init() {
    server.addMiddleware([](AsyncWebServerRequest* request, ArMiddlewareNext next) {
        if (web_auth_ok(request) || captive_portal_is_probe(request->url().c_str())) return next();
        request->requestAuthentication(AsyncAuthType::AUTH_BASIC, "Pumpkin");
    });

    // LittleFS is only an optional config/certificate overlay. Embedded web
    // assets keep the UI and firmware atomic and remain available if it fails.
    if (!LittleFS.begin(false)) {
        Serial.println("[WEB] LittleFS mount failed; continuing with embedded UI");
    } else {
        Serial.println("[WEB] LittleFS mounted for config overlay");
    }

    for (size_t i = 0; i < get_web_assets_count(); ++i) {
        const WebAsset* asset = get_web_asset_at(i);
        server.on(asset->path, HTTP_GET, [asset](AsyncWebServerRequest* request) {
            AsyncWebServerResponse* response = request->beginResponse(
                200, asset->mime_type, asset->data, asset->size);
            if (asset->content_encoding) response->addHeader("Content-Encoding", asset->content_encoding);
            response->addHeader("Cache-Control", "no-cache");
            response->addHeader("ETag", asset->etag);
            request->send(response);
        });
    }

    // API routes
    server.on("/api/status", HTTP_GET, handle_status);
    server.on("/api/display/frame", HTTP_GET, handle_display_frame);
    server.on("/api/config", HTTP_GET, handle_get_config);
    server.on("/api/debug", HTTP_GET, handle_debug);
    server.on("/api/history", HTTP_GET, handle_history);
    server.on("/api/timer", HTTP_GET, handle_timer_status);
    server.on("/api/update/status", HTTP_GET, handle_update_status);
    server.on("/api/update", HTTP_POST, handle_update_done, handle_update_upload);
    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* r) { handle_restart(r); });
    server.on("/api/factory-reset", HTTP_POST, [](AsyncWebServerRequest* r) { handle_factory_reset(r); });
    server.on("/api/test/glucose", HTTP_POST, [](AsyncWebServerRequest* r) { handle_test_glucose(r); });

    // Specific paths first: a handler for "/api/clocks" also matches "/api/clocks/…".
    server.on("/api/clocks/scan", HTTP_POST, [](AsyncWebServerRequest* r) {
        clock_net_rescan();
        r->send(202, "application/json", "{\"status\":\"scanning\"}");
    });
    server.on("/api/clocks/copy", HTTP_GET, [](AsyncWebServerRequest* r) { handle_copy_clocks_status(r); });
    server.on("/api/clocks/copy", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handle_copy_clocks
    );
    server.on("/api/clocks", HTTP_GET, [](AsyncWebServerRequest* r) { handle_get_clocks(r); });
    server.on("/api/alerts/clear", HTTP_POST, [](AsyncWebServerRequest* r) { handle_clear_alerts(r); });
    server.on("/api/alerts/clear", HTTP_GET, [](AsyncWebServerRequest* r) { handle_clear_alerts_status(r); });

    // POST /api/test/alert with body
    server.on("/api/test/alert", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handle_test_alert
    );

    // WiFi setup portal
    server.on("/api/wifi/scan", HTTP_GET, handle_wifi_scan);
    server.on("/api/wifi/status", HTTP_GET, handle_wifi_status);
    server.on("/api/wifi/scan/refresh", HTTP_POST, [](AsyncWebServerRequest* r) { handle_wifi_scan_refresh(r); });
    server.on("/api/wifi/ca", HTTP_DELETE, [](AsyncWebServerRequest* r) { handle_wifi_ca_delete(r); });
    server.on("/api/wifi/connect", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handle_wifi_connect
    );
    server.on("/api/wifi/ca", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handle_wifi_ca_upload
    );

    server.on("/api/display/next", HTTP_POST, [](AsyncWebServerRequest* r) { handle_display_next(r); });
    server.on("/api/display/prev", HTTP_POST, [](AsyncWebServerRequest* r) { handle_display_prev(r); });

    // POST /api/config with body
    server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        NULL,
        handle_post_config
    );

    // Same-origin production UI needs no wildcard CORS. It can be enabled for
    // local development only with -DPUMPKIN_DEV_CORS.
#ifdef PUMPKIN_DEV_CORS
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
#endif

    // Registered last: this installs the onNotFound catch-all, which must not
    // shadow any real route.
    captive_portal_register_routes(server);

    Serial.println("[WEB] Routes registered");
}

void webserver_start() {
    if (started) return;
    server.begin();
    started = true;
    Serial.printf("[WEB] Server started at http://%s/\n", wifi_get_ip());
}
