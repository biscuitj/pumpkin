#include "ota_manager.h"
#include "ota_boot_validation.h"

#include "config_manager.h"
#include "display.h"
#include "web_assets.h"

#include <Arduino.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <string.h>

#ifndef PUMPKIN_VERSION
#error "PUMPKIN_VERSION must be injected from VERSION"
#endif

// NVS namespace kept from SugarClock so existing clocks keep their update state.
static const char* OTA_NVS_NAMESPACE = "sugarota";
static const uint32_t REBOOT_DELAY_MS = 1500;

static portMUX_TYPE status_mux = portMUX_INITIALIZER_UNLOCKED;
static OtaStatusSnapshot status_snapshot;
static bool validation_pending = false;
static OtaBootValidationState boot_validation = {};
static uint32_t last_render_ms = 0;
static uint32_t reboot_at_ms = 0;
static size_t upload_total = 0;
static size_t upload_received = 0;

static void copy_text(char* dst, size_t size, const char* src) {
    strncpy(dst, src ? src : "", size - 1);
    dst[size - 1] = '\0';
}

static void set_state(OtaState state, const char* error = nullptr) {
    portENTER_CRITICAL(&status_mux);
    status_snapshot.state = state;
    if (state == OTA_RECEIVING) status_snapshot.last_error[0] = '\0';
    if (error) copy_text(status_snapshot.last_error, sizeof(status_snapshot.last_error), error);
    portEXIT_CRITICAL(&status_mux);
}

static void set_progress(int progress) {
    portENTER_CRITICAL(&status_mux);
    status_snapshot.progress = progress;
    portEXIT_CRITICAL(&status_mux);
}

static OtaState current_state() {
    portENTER_CRITICAL(&status_mux);
    const OtaState state = status_snapshot.state;
    portEXIT_CRITICAL(&status_mux);
    return state;
}

const char* ota_state_name(OtaState state) {
    switch (state) {
        case OTA_IDLE: return "idle";
        case OTA_RECEIVING: return "receiving";
        case OTA_PENDING_REBOOT: return "pending_reboot";
        case OTA_ERROR: return "error";
    }
    return "unknown";
}

bool ota_is_busy() {
    const OtaState state = current_state();
    return state == OTA_RECEIVING || state == OTA_PENDING_REBOOT;
}

void ota_get_status(OtaStatusSnapshot& output) {
    portENTER_CRITICAL(&status_mux);
    output = status_snapshot;
    portEXIT_CRITICAL(&status_mux);
}

// ---------------------------------------------------------------------------
// Upload (called from the web server's upload callback)

bool ota_upload_begin(size_t total_size) {
    if (ota_is_busy()) return false;
    if (validation_pending) {
        // A second image before this one is accepted could leave no valid fallback.
        set_state(OTA_ERROR, "previous_update_still_validating");
        return false;
    }
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (!target) {
        set_state(OTA_ERROR, "no_inactive_partition");
        return false;
    }
    if (total_size && total_size > target->size) {
        set_state(OTA_ERROR, "firmware_too_large");
        return false;
    }
    if (!Update.begin(total_size ? total_size : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        set_state(OTA_ERROR, Update.errorString());
        return false;
    }
    upload_total = total_size;
    upload_received = 0;
    set_progress(total_size ? 0 : -1);
    set_state(OTA_RECEIVING);
    Serial.printf("[OTA] Receiving firmware upload (%u bytes) into %s\n",
                  static_cast<unsigned>(total_size), target->label);
    return true;
}

bool ota_upload_write(const uint8_t* data, size_t len) {
    if (current_state() != OTA_RECEIVING) return false;
    if (Update.write(const_cast<uint8_t*>(data), len) != len) {
        Serial.printf("[OTA] Write failed: %s\n", Update.errorString());
        set_state(OTA_ERROR, Update.errorString());
        Update.abort();
        return false;
    }
    upload_received += len;
    if (upload_total) set_progress(static_cast<int>((upload_received * 100ULL) / upload_total));
    return true;
}

bool ota_upload_end() {
    if (current_state() != OTA_RECEIVING) return false;
    // end(true) verifies the image and selects it for the next boot.
    if (!Update.end(true)) {
        Serial.printf("[OTA] Image rejected: %s\n", Update.errorString());
        set_state(OTA_ERROR, Update.errorString());
        return false;
    }
    Preferences prefs;
    if (prefs.begin(OTA_NVS_NAMESPACE, false)) {
        prefs.putBool("pending", true);
        prefs.putString("previous", PUMPKIN_VERSION);
        prefs.remove("last_error");
        prefs.end();
    }
    set_progress(100);
    set_state(OTA_PENDING_REBOOT);
    reboot_at_ms = millis() + REBOOT_DELAY_MS;
    if (reboot_at_ms == 0) reboot_at_ms = 1;
    Serial.printf("[OTA] Upload complete (%u bytes); restarting into the new firmware\n",
                  static_cast<unsigned>(upload_received));
    return true;
}

void ota_upload_abort(const char* reason) {
    if (current_state() == OTA_RECEIVING) Update.abort();
    set_state(OTA_ERROR, reason ? reason : "aborted");
}

// ---------------------------------------------------------------------------
// Boot: first-boot validation of a freshly uploaded image, rollback reporting

static void clear_pending_metadata() {
    Preferences prefs;
    if (!prefs.begin(OTA_NVS_NAMESPACE, false)) return;
    prefs.remove("pending");
    prefs.remove("previous");
    prefs.end();
}

static void inspect_boot_state() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    copy_text(status_snapshot.running_partition, sizeof(status_snapshot.running_partition), running ? running->label : "unknown");
    copy_text(status_snapshot.boot_partition, sizeof(status_snapshot.boot_partition), boot ? boot->label : "unknown");
    Serial.printf("[OTA] Running partition: %s; boot partition: %s\n",
                  status_snapshot.running_partition, status_snapshot.boot_partition);

    esp_ota_img_states_t image_state = ESP_OTA_IMG_UNDEFINED;
    if (running && esp_ota_get_state_partition(running, &image_state) == ESP_OK &&
        image_state == ESP_OTA_IMG_PENDING_VERIFY) {
        validation_pending = true;
        ota_boot_validation_begin(boot_validation, millis());
        status_snapshot.pending_verification = true;
        Serial.printf("[OTA] v%s pending local first-boot validation\n", PUMPKIN_VERSION);
    }

    Preferences prefs;
    if (!prefs.begin(OTA_NVS_NAMESPACE, false)) return;
    const bool pending_record = prefs.getBool("pending", false);
    const String previous = prefs.getString("previous", "");
    String saved_error = prefs.getString("last_error", "");
    // Back on the firmware that did the upload while the upload is still
    // marked pending: the new image never passed its checks (it failed them,
    // or restarted first) and the bootloader returned here.
    if (pending_record && !validation_pending && previous == PUMPKIN_VERSION) {
        saved_error = "rollback_detected";
        prefs.putString("last_error", saved_error);
    }
    prefs.end();
    copy_text(status_snapshot.last_error, sizeof(status_snapshot.last_error), saved_error.c_str());
    // Shown until the next successful update.
    status_snapshot.rolled_back = saved_error == "rollback_detected";
    if (status_snapshot.rolled_back) {
        Serial.println("[OTA] The last uploaded firmware failed its checks; this version was restored");
    }
    if (pending_record && !validation_pending) clear_pending_metadata();
}

void ota_init() {
    memset(&status_snapshot, 0, sizeof(status_snapshot));
    status_snapshot.state = OTA_IDLE;
    copy_text(status_snapshot.current_version, sizeof(status_snapshot.current_version), PUMPKIN_VERSION);
    inspect_boot_state();
}

static void validate_pending_image() {
    if (!validation_pending) return;
    const bool config_loaded = config_is_loaded();
    const uint32_t assets = get_web_assets_count();
    const uint32_t heap = ESP.getFreeHeap();
    const OtaBootValidationAction action = ota_boot_validation_next(
        boot_validation, millis(), config_loaded, assets, heap);
    if (action == OTA_BOOT_WAIT) return;

    if (action == OTA_BOOT_ACCEPT) {
        const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
        if (result == ESP_OK) {
            validation_pending = false;
            clear_pending_metadata();
            Preferences prefs;
            if (prefs.begin(OTA_NVS_NAMESPACE, false)) {
                prefs.remove("last_error");
                prefs.end();
            }
            portENTER_CRITICAL(&status_mux);
            status_snapshot.pending_verification = false;
            status_snapshot.rolled_back = false;
            status_snapshot.last_error[0] = '\0';
            portEXIT_CRITICAL(&status_mux);
            Serial.printf("[OTA] Local validation passed; v%s marked valid\n", PUMPKIN_VERSION);
        } else {
            Serial.printf("[OTA] Accepting image failed (%d); retrying in 5 seconds\n", result);
        }
    } else {
        Serial.printf("[OTA] Local validation failed (config=%d assets=%u loops=%lu heap=%u); rolling back\n",
                      config_loaded, static_cast<unsigned>(assets),
                      static_cast<unsigned long>(boot_validation.loop_count), heap);
        Preferences prefs;
        if (prefs.begin(OTA_NVS_NAMESPACE, false)) {
            prefs.putString("last_error", "rollback_detected");
            prefs.end();
        }
        const esp_err_t result = esp_ota_mark_app_invalid_rollback_and_reboot();
        // Successful rollback reboots. If it returns (e.g. no valid fallback),
        // keep the image pending and avoid retrying flash writes on every loop.
        Serial.printf("[OTA] Rollback returned (%d); retrying in 5 seconds\n", result);
    }
}

static void render_update_status() {
    OtaStatusSnapshot snapshot;
    ota_get_status(snapshot);
    if (snapshot.state != OTA_RECEIVING && snapshot.state != OTA_PENDING_REBOOT) return;
    if (millis() - last_render_ms < 200) return;
    last_render_ms = millis();
    char text[8];
    if (snapshot.state == OTA_RECEIVING) {
        if (snapshot.progress >= 0) snprintf(text, sizeof(text), "%d%%", snapshot.progress);
        else snprintf(text, sizeof(text), "UPDATE");
    } else {
        snprintf(text, sizeof(text), "REBOOT");
    }
    display_set_brightness(40);
    display_clear();
    display_draw_text(text, 1, 0, display_color(0, 200, 200));
    display_show();
}

void ota_loop() {
    validate_pending_image();
    render_update_status();
    if (reboot_at_ms && static_cast<int32_t>(millis() - reboot_at_ms) >= 0) {
        reboot_at_ms = 0;
        Serial.println("[OTA] Restarting");
        Serial.flush();
        ESP.restart();
    }
}
