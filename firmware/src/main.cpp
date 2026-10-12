#include <Arduino.h>
#include <esp_task_wdt.h>
#include "hardware_pins.h"
#include "config_manager.h"
#include "display.h"
#include "glucose_engine.h"
#include "buttons.h"
#include "wifi_manager.h"
#include "time_engine.h"
#include "sensors.h"
#include "http_client.h"
#include "web_server.h"
#include "buzzer.h"
#include "timer_engine.h"
#include "countdown_engine.h"
#include "improv_serial.h"
#include "net_check.h"
#include "ota_manager.h"
#include "clock_net.h"

#ifndef PUMPKIN_VERSION
#error "PUMPKIN_VERSION must be injected from the root VERSION file"
#endif
#define WDT_TIMEOUT_SEC  30

// Performance tracking
static unsigned long loop_count = 0;
static unsigned long loop_time_sum = 0;
static unsigned long loop_time_max = 0;
static unsigned long last_diag_ms = 0;
#define DIAG_INTERVAL_MS 60000  // log diagnostics every 60s

static bool webserver_started = false;

// 8x8 pixel pumpkin shown while the clock starts: stem, body, two ridges.
static void draw_boot_pumpkin() {
    static const char* const ROWS[8] = {
        "...gg...",
        "..oOOo..",
        ".oOooOo.",
        "ooOooOoo",
        "ooOooOoo",
        "ooOooOoo",
        ".oOooOo.",
        "..oooo..",
    };
    const uint16_t orange = display_color(255, 117, 24);
    const uint16_t ridge = display_color(200, 70, 0);
    const uint16_t stem = display_color(60, 160, 40);
    const int x0 = (MATRIX_WIDTH - 8) / 2;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const char c = ROWS[y][x];
            if (c == '.') continue;
            display_draw_pixel(x0 + x, y, c == 'g' ? stem : c == 'O' ? ridge : orange);
        }
    }
}

void setup() {
    // 1. Serial init
    Serial.begin(115200);
    delay(100);

    // 2. Initialize buzzer (silences immediately)
    buzzer_init();

    // Log boot reason
    esp_reset_reason_t reason = esp_reset_reason();
    Serial.println();
    Serial.println("================================");
    Serial.printf("Pumpkin v%s\n", PUMPKIN_VERSION);
    Serial.printf("Reset reason: %d\n", reason);
    if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT) {
        Serial.println("WARNING: Previous watchdog reset!");
    }
    Serial.println("================================");

    // 3. Load configuration
    config_init();

    // 4. Initialize display + show boot screen
    display_init();
    display_clear();
    draw_boot_pumpkin();
    display_show();

    // 5. Init buttons
    buttons_init();

    // 6. Init WiFi (clock identity first: it names the setup network and the
    //    DHCP hostname, both of which must be set before the radio starts)
    clock_net_init();
    wifi_init();
    clock_net_start();  // mDNS name + other clocks, on its own task once online

    // 7. Init time engine
    time_init();

    // 8. Init sensors
    sensors_init();

    // 9. Init HTTP client
    http_init();

    // 11. Init web server routes (doesn't start serving yet)
    webserver_init();

    // 12. Init new feature engines
    timer_init();
    countdown_init();

    // 13. Init Improv Wi-Fi serial handler
    improv_init();

    // 13b. Init post-connection reachability checks
    netcheck_init();

    // 14. Init glucose engine (state machine)
    engine_init();

    // Firmware updates (web upload) and first-boot validation of a new image.
    // Initialized only after configuration, display, web assets and the main
    // engines are ready, because the validation checks those.
    ota_init();

    // 14. Enable watchdog timer
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL); // add current task

    Serial.println("[BOOT] Setup complete");
    Serial.printf("[BOOT] Free heap: %d bytes\n", ESP.getFreeHeap());
}

void loop() {
    unsigned long loop_start = millis();

    // Reset watchdog
    esp_task_wdt_reset();

    // 1. WiFi management
    wifi_loop();

    // 1a. Right after a setup (new Wi-Fi joined from the setup page), scroll
    //     where the clock can now be reached, once: "visit keuken.local or <ip>".
    static bool address_announced = false;
    if (!address_announced && wifi_trial_get_state() == WIFI_TRIAL_CONNECTED &&
        wifi_is_connected() && !wifi_is_ap_mode() && clock_net_local_host()[0]) {
        address_announced = true;
        engine_show_connection_info();
    }

    // 1b. Improv Wi-Fi serial (for ESP Web Tools credential input)
    improv_loop();

    // 1c. Reachability probes (DNS / data source / NTP) once online
    netcheck_loop();

    // Start web server once WiFi connects or in AP mode (one-time)
    if ((wifi_is_connected() || wifi_is_ap_mode()) && !webserver_started) {
        webserver_start();
        webserver_started = true;
    }

    // 2. HTTP polling
    http_loop();

    // 3. Time management
    time_loop();

    // 4. Button input
    buttons_loop();
    ButtonEvent evt = buttons_get_event();
    if (evt != BTN_NONE) {
        // While the connection address is visible, any completed button
        // gesture dismisses it without also changing another setting.
        if (engine_get_state() == STATE_CONNECTION_INFO_DISPLAY) {
            engine_dismiss_connection_info();
            Serial.println("[BTN] Connection info dismissed");
        } else switch (evt) {
            case BTN_LEFT_SHORT:
                engine_toggle_mode();
                break;
            case BTN_MIDDLE_SHORT: {
                // Cycle brightness: 10 -> 40 -> 100 -> 200 -> 10
                AppConfig& cfg = config_get();
                if (cfg.brightness < 20) cfg.brightness = 40;
                else if (cfg.brightness < 60) cfg.brightness = 100;
                else if (cfg.brightness < 150) cfg.brightness = 200;
                else cfg.brightness = 10;
                cfg.auto_brightness = false;
                display_set_brightness(cfg.brightness);
                config_save();
                Serial.printf("[BTN] Brightness: %d\n", cfg.brightness);
                break;
            }
            case BTN_MIDDLE_DOUBLE:
                engine_show_connection_info();
                Serial.println("[BTN] Showing connection info");
                break;
            case BTN_MIDDLE_LONG:
                // Snooze buzzer alerts
                engine_snooze_alerts();
                Serial.println("[BTN] Alerts snoozed");
                break;
            case BTN_RIGHT_SHORT:
                // Context-sensitive right button
                engine_right_button_action();
                break;
            case BTN_LEFT_LONG:
                engine_clear_force();
                engine_set_default_mode(STATE_GLUCOSE_DISPLAY);
                Serial.println("[BTN] Overrides cleared");
                break;
            case BTN_RIGHT_LONG:
                // Context-sensitive right long press
                engine_right_long_action();
                break;
            default:
                break;
        }
    }

    // 5. Sensor readings
    sensors_loop();

    // 6. Feature engine loops
    buzzer_loop();
    timer_loop();
    countdown_loop();

    // 7. Engine state machine + rendering
    engine_loop();

    // 8. Firmware update progress, restart after an upload, and first-boot
    // validation. Runs after the renderer so the progress screen stays visible.
    ota_loop();

    // Performance tracking
    unsigned long loop_time = millis() - loop_start;
    loop_count++;
    loop_time_sum += loop_time;
    if (loop_time > loop_time_max) loop_time_max = loop_time;

    // Periodic diagnostic logging
    if (millis() - last_diag_ms > DIAG_INTERVAL_MS) {
        last_diag_ms = millis();
        unsigned long avg = (loop_count > 0) ? (loop_time_sum / loop_count) : 0;
        Serial.printf("[DIAG] Heap: %d/%d, Loop avg: %lums, max: %lums, state: %s\n",
                      ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                      avg, loop_time_max,
                      engine_state_name(engine_get_state()));
        loop_count = 0;
        loop_time_sum = 0;
        loop_time_max = 0;
    }

    // No delay() - all subsystems use millis()-based timing
}
