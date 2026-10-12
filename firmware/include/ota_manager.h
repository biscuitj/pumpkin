#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

// Firmware updates for Pumpkin: a .bin uploaded from the clock's own web page
// (Device → Update firmware). The image goes to the inactive OTA slot; on the
// next boot it stays "pending" until local health checks pass
// (ota_boot_validation), otherwise the bootloader returns to the previous
// firmware. No update server, no automatic installs.

#include <stddef.h>
#include <stdint.h>

enum OtaState : uint8_t {
    OTA_IDLE,
    OTA_RECEIVING,        // upload in progress
    OTA_PENDING_REBOOT,   // image written and verified; restarting shortly
    OTA_ERROR
};

struct OtaStatusSnapshot {
    OtaState state;
    int progress;               // 0..100 while receiving (-1 when the size is unknown)
    char current_version[24];
    char last_error[64];
    char running_partition[16];
    char boot_partition[16];
    bool pending_verification;  // this firmware hasn't passed its first-boot checks yet
    bool rolled_back;           // the last uploaded firmware failed and this one was restored
};

void ota_init();
void ota_loop();

// Upload, called from the web server's upload callback (async task).
// begin() must succeed before write(); end() verifies the image and selects it
// for the next boot. abort() drops a partial upload. All return false with
// ota status last_error set on failure.
bool ota_upload_begin(size_t total_size);
bool ota_upload_write(const uint8_t* data, size_t len);
bool ota_upload_end();
void ota_upload_abort(const char* reason);
bool ota_is_busy();

const char* ota_state_name(OtaState state);
void ota_get_status(OtaStatusSnapshot& output);

#endif
