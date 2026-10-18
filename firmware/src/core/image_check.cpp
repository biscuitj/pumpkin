#include "image_check.h"

#include <string.h>

bool ota_image_check_begin(OtaImageCheck& check, const char* marker, size_t prefix_len) {
    memset(&check, 0, sizeof(check));
    check.header_verdict = OTA_IMAGE_PENDING;
    const size_t len = marker ? strlen(marker) : 0;
    if (len == 0 || len >= OTA_IMAGE_MARKER_MAX || prefix_len == 0 || prefix_len > len) return false;
    memcpy(check.marker, marker, len);
    check.marker_len = len;
    check.prefix_len = prefix_len;
    // failure[i]: length of the longest proper prefix of marker[0..i] that is
    // also its suffix, so a partial match never has to look back at old bytes.
    size_t k = 0;
    check.failure[0] = 0;
    for (size_t i = 1; i < len; ++i) {
        while (k > 0 && marker[i] != marker[k]) k = check.failure[k - 1];
        if (marker[i] == marker[k]) ++k;
        check.failure[i] = static_cast<uint8_t>(k);
    }
    return true;
}

static OtaImageVerdict check_header(const uint8_t* header) {
    if (header[0] != OTA_IMAGE_MAGIC) return OTA_IMAGE_NOT_FIRMWARE;
    const uint16_t chip_id = static_cast<uint16_t>(header[12] | (header[13] << 8));
    if (chip_id != OTA_IMAGE_CHIP_ID_ESP32) return OTA_IMAGE_WRONG_CHIP;
    return OTA_IMAGE_PENDING;
}

OtaImageVerdict ota_image_check_feed(OtaImageCheck& check, const uint8_t* data, size_t len) {
    if (check.marker_len == 0) return OTA_IMAGE_NOT_PUMPKIN;   // begin() failed
    if (check.header_verdict != OTA_IMAGE_PENDING) return check.header_verdict;
    for (size_t i = 0; i < len; ++i) {
        const uint8_t byte = data[i];
        if (check.received < OTA_IMAGE_HEADER_LEN) {
            check.header[check.received] = byte;
            if (check.received + 1 == OTA_IMAGE_HEADER_LEN) {
                check.header_verdict = check_header(check.header);
                if (check.header_verdict != OTA_IMAGE_PENDING) {
                    ++check.received;
                    return check.header_verdict;
                }
            }
        }
        ++check.received;
        if (check.found_marker) continue;
        size_t k = check.matched;
        while (k > 0 && byte != static_cast<uint8_t>(check.marker[k])) k = check.failure[k - 1];
        if (byte == static_cast<uint8_t>(check.marker[k])) ++k;
        if (k >= check.prefix_len) check.found_prefix = true;
        if (k == check.marker_len) {
            check.found_marker = true;
            k = 0;
        }
        check.matched = k;
    }
    return OTA_IMAGE_PENDING;
}

OtaImageVerdict ota_image_check_finish(const OtaImageCheck& check) {
    if (check.marker_len == 0) return OTA_IMAGE_NOT_PUMPKIN;
    if (check.header_verdict != OTA_IMAGE_PENDING) return check.header_verdict;
    if (check.received < OTA_IMAGE_HEADER_LEN) return OTA_IMAGE_TRUNCATED;
    if (check.found_marker) return OTA_IMAGE_OK;
    if (check.found_prefix) return OTA_IMAGE_WRONG_HARDWARE;
    return OTA_IMAGE_NOT_PUMPKIN;
}

const char* ota_image_verdict_error(OtaImageVerdict verdict) {
    switch (verdict) {
        case OTA_IMAGE_PENDING: return "";
        case OTA_IMAGE_OK: return "";
        case OTA_IMAGE_NOT_FIRMWARE: return "not_firmware";
        case OTA_IMAGE_WRONG_CHIP: return "wrong_chip";
        case OTA_IMAGE_NOT_PUMPKIN: return "not_pumpkin";
        case OTA_IMAGE_WRONG_HARDWARE: return "wrong_hardware";
        case OTA_IMAGE_TRUNCATED: return "truncated";
    }
    return "not_firmware";
}
