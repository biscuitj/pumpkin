#include "ota.h"

#include <Arduino.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>

#include "core/image_check.h"
#include "core/sync.h"
#include "core/types.h"

#ifndef PUMPKIN_VERSION
#error "PUMPKIN_VERSION comes from the VERSION file (scripts/pre_build.py)"
#endif

// Arduino accepts a pending image before setup() unless this says otherwise.
// Returning true leaves it pending so otaLoop() can decide after its checks; a
// crash or restart before then makes the bootloader roll back.
extern "C" bool verifyRollbackLater() { return true; }

namespace hal {

namespace {

using pk::Locked;

const char* const kNs = "sugarota";
constexpr uint32_t kWatchMs = 15000;      // minimum time a new firmware has to run
constexpr uint32_t kHeapGraceMs = 60000;  // heap may recover from a dip until then
constexpr uint32_t kMinHeap = 55000;

pk::Mutex lock;
pk::UpdateView view;
size_t total = 0;
OtaImageCheck check;

bool pendingVerify = false;
uint32_t verifyStart = 0, lastAttempt = 0;
uint32_t loops = 0;
bool rolledBack = false;
char rolledBackFrom[24] = "";

void fail(const char* code) {
    Locked l(lock);
    view.state = pk::UpdateView::Failed;
    pk::copyStr(view.error, code);
}

} // namespace

void otaBegin() {
    view = pk::UpdateView{};
    Serial.printf("[ota] %s, image %s\n", PUMPKIN_VERSION, PUMPKIN_IMAGE_MARKER);
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (running && esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        pendingVerify = true;
        verifyStart = millis();
    }

    Preferences p;
    if (!p.begin(kNs, false)) return;
    if (pendingVerify) {
        // Tell the previous firmware which version it would be rolling back from.
        p.putString("next", PUMPKIN_VERSION);
        Serial.println("[ota] New firmware: checking it before keeping it");
    }
    const bool uploaded = p.getBool("pending", false);
    const String previous = p.getString("previous", "");
    String error = p.getString("last_error", "");
    // Back on the firmware that did the upload while it's still marked pending:
    // the new one never passed its checks.
    if (uploaded && !pendingVerify && previous == PUMPKIN_VERSION) {
        error = "rollback_detected";
        p.putString("last_error", error);
        p.remove("pending");
        p.remove("previous");
    }
    if (error == "rollback_detected") {
        rolledBack = true;
        pk::copyStr(rolledBackFrom, p.getString("next", "a newer version").c_str());
        Serial.printf("[ota] %s didn't pass its first start; back on %s\n", rolledBackFrom, PUMPKIN_VERSION);
    }
    p.end();
}

void otaLoop(bool healthy) {
    if (!pendingVerify) return;
    if (loops != UINT32_MAX) ++loops;
    const uint32_t now = millis();
    const uint32_t elapsed = now - verifyStart;
    if (elapsed < kWatchMs) return;
    if (lastAttempt && now - lastAttempt < 5000) return;
    const uint32_t heap = ESP.getFreeHeap();
    const bool ok = healthy && loops > 100;
    if (ok && heap < kMinHeap && elapsed < kHeapGraceMs) return;
    lastAttempt = now;

    Preferences p;
    if (ok && heap >= kMinHeap) {
        if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) return;  // retried in 5 s
        pendingVerify = false;
        if (p.begin(kNs, false)) {
            p.remove("pending");
            p.remove("previous");
            p.remove("last_error");
            p.remove("next");
            p.end();
        }
        rolledBack = false;
        Serial.printf("[ota] %s kept\n", PUMPKIN_VERSION);
        return;
    }
    Serial.printf("[ota] First-start check failed (healthy %d, loops %u, heap %u): rolling back\n", healthy,
                  (unsigned)loops, (unsigned)heap);
    if (p.begin(kNs, false)) {
        p.putString("last_error", "rollback_detected");
        p.end();
    }
    esp_ota_mark_app_invalid_rollback_and_reboot();  // returns only when there's nothing to go back to
}

bool otaUploadBegin(size_t size) {
    {
        Locked l(lock);
        if (view.state == pk::UpdateView::Receiving || view.state == pk::UpdateView::Restarting) return false;
        view = pk::UpdateView{};
        view.state = pk::UpdateView::Receiving;
    }
    total = size;
    // A second image before this one is kept could leave nothing to go back to.
    if (pendingVerify) return fail("busy"), false;
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (!target || (size && size > target->size)) return fail("write_failed"), false;
    ota_image_check_begin(check, PUMPKIN_IMAGE_MARKER, sizeof(PUMPKIN_IMAGE_MARKER_PREFIX) - 1);
    if (!Update.begin(size ? size : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        Serial.printf("[ota] %s\n", Update.errorString());
        return fail("write_failed"), false;
    }
    Serial.printf("[ota] Receiving %u bytes into %s\n", (unsigned)size, target->label);
    return true;
}

bool otaUploadWrite(const uint8_t* data, size_t len) {
    {
        Locked l(lock);
        if (view.state != pk::UpdateView::Receiving) return false;
    }
    const OtaImageVerdict v = ota_image_check_feed(check, data, len);
    if (v != OTA_IMAGE_PENDING) {
        // Not an ESP32 app image: stop before any more of it reaches flash.
        Update.abort();
        return fail(ota_image_verdict_error(v)), false;
    }
    if (Update.write(const_cast<uint8_t*>(data), len) != len) {
        Serial.printf("[ota] Write failed: %s\n", Update.errorString());
        Update.abort();
        return fail("write_failed"), false;
    }
    Locked l(lock);
    view.received += len;
    return true;
}

bool otaUploadEnd() {
    {
        Locked l(lock);
        if (view.state != pk::UpdateView::Receiving) return false;
    }
    const OtaImageVerdict v = ota_image_check_finish(check);
    if (v != OTA_IMAGE_OK) {
        // Written to the other slot but never selected.
        Update.abort();
        return fail(ota_image_verdict_error(v)), false;
    }
    if (!Update.end(true)) {  // verifies the image and selects it
        Serial.printf("[ota] Image refused: %s\n", Update.errorString());
        return fail("write_failed"), false;
    }
    Preferences p;
    if (p.begin(kNs, false)) {
        p.putBool("pending", true);
        p.putString("previous", PUMPKIN_VERSION);
        p.remove("last_error");
        p.remove("next");
        p.end();
    }
    Locked l(lock);
    view.state = pk::UpdateView::Restarting;
    Serial.println("[ota] Update received");
    return true;
}

void otaUploadAbort() {
    bool receiving;
    {
        Locked l(lock);
        receiving = view.state == pk::UpdateView::Receiving;
    }
    if (!receiving) return;
    Update.abort();
    fail("truncated");
}

void otaView(pk::UpdateView& out) {
    Locked l(lock);
    out = view;
}

int otaProgress() {
    Locked l(lock);
    if (view.state != pk::UpdateView::Receiving || !total) return -1;
    return (int)((uint64_t)view.received * 100 / total);
}

bool otaRolledBack(char* from, size_t size) {
    if (!rolledBack) return false;
    pk::copyStr(from, size, rolledBackFrom);
    return true;
}

} // namespace hal
