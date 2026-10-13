#ifndef OTA_IMAGE_CHECK_H
#define OTA_IMAGE_CHECK_H

// Checks a firmware upload while it streams in (no Arduino/ESP calls,
// unit-tested on the host). A .bin is only installed when it is an ESP32 app
// image that carries this build's Pumpkin marker, so a .bin from another
// project, another chip or another Pumpkin hardware variant is refused and
// the clock keeps running what it has.

#include <stddef.h>
#include <stdint.h>

// The hardware variant this build is for. Single source; scripts/post_build.py
// reads it from here to check the built image.
#ifndef PUMPKIN_HARDWARE_ID
#define PUMPKIN_HARDWARE_ID "ulanzi-tc001-esp32-4mb"
#endif

// Every Pumpkin image contains its marker once, as a plain string:
// "PUMPKIN-IMAGE|ulanzi-tc001-esp32-4mb|". ota_manager passes it to the check
// at runtime, which also keeps it in the image. The USB guard and the web page
// look for the same bytes.
#define PUMPKIN_IMAGE_MARKER_PREFIX "PUMPKIN-IMAGE|"
#define PUMPKIN_IMAGE_MARKER PUMPKIN_IMAGE_MARKER_PREFIX PUMPKIN_HARDWARE_ID "|"

#define OTA_IMAGE_MARKER_MAX 64
#define OTA_IMAGE_HEADER_LEN 24        // esp_image_header_t
#define OTA_IMAGE_MAGIC 0xE9
#define OTA_IMAGE_CHIP_ID_ESP32 0x0000

enum OtaImageVerdict : uint8_t {
    OTA_IMAGE_PENDING,         // nothing wrong yet; call finish() at the end
    OTA_IMAGE_OK,
    OTA_IMAGE_NOT_FIRMWARE,    // doesn't start like an ESP32 app image
    OTA_IMAGE_WRONG_CHIP,      // an app image for another chip (S2, S3, C3 …)
    OTA_IMAGE_NOT_PUMPKIN,     // ESP32 image without any Pumpkin marker
    OTA_IMAGE_WRONG_HARDWARE,  // Pumpkin, built for another hardware ID
    OTA_IMAGE_TRUNCATED,       // ended inside the header
};

struct OtaImageCheck {
    char marker[OTA_IMAGE_MARKER_MAX];
    size_t marker_len;
    size_t prefix_len;
    uint8_t failure[OTA_IMAGE_MARKER_MAX];   // KMP failure table for marker
    size_t matched;                          // marker bytes matched so far
    uint8_t header[OTA_IMAGE_HEADER_LEN];
    size_t received;
    bool found_marker;
    bool found_prefix;
    OtaImageVerdict header_verdict;
};

// marker: the full expected marker; prefix_len: how much of it is the
// project prefix (seeing only that means "Pumpkin, other hardware").
// False when the marker is empty, too long or prefix_len doesn't fit.
bool ota_image_check_begin(OtaImageCheck& check, const char* marker, size_t prefix_len);

// Feed the next bytes in order. Returns OTA_IMAGE_PENDING until the header
// shows the file is not an ESP32 app image; then that verdict, which sticks.
OtaImageVerdict ota_image_check_feed(OtaImageCheck& check, const uint8_t* data, size_t len);

// The verdict for the whole file once every byte has been fed.
OtaImageVerdict ota_image_check_finish(const OtaImageCheck& check);

// "not_a_pumpkin_firmware" etc., the last_error the web page shows.
const char* ota_image_verdict_error(OtaImageVerdict verdict);

#endif
