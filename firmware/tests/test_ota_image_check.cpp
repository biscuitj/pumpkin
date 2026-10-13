// Firmware upload check: ESP32 header, Pumpkin marker across chunk borders,
// other projects, other chips and other Pumpkin hardware.
#include "../src/ota_image_check.cpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

static const char* MARKER = PUMPKIN_IMAGE_MARKER;
static const size_t PREFIX = sizeof(PUMPKIN_IMAGE_MARKER_PREFIX) - 1;

static std::vector<uint8_t> image(const std::string& body, uint8_t magic = 0xE9, uint16_t chip = 0) {
    std::vector<uint8_t> out(OTA_IMAGE_HEADER_LEN, 0);
    out[0] = magic;
    out[1] = 5;                       // segment count
    out[12] = chip & 0xFF;
    out[13] = chip >> 8;
    // Filler that looks like code, with bytes of the marker scattered around.
    std::string filler;
    for (int i = 0; i < 3000; ++i) filler += static_cast<char>((i * 37) & 0xFF);
    filler += "PUMPKIN-IMAG";        // near miss
    filler += "PUMPKIN-PUMPKIN-IMAGE-";   // partial restart, no "|"
    out.insert(out.end(), filler.begin(), filler.end());
    out.insert(out.end(), body.begin(), body.end());
    for (int i = 0; i < 500; ++i) out.push_back(static_cast<uint8_t>(i));
    return out;
}

static OtaImageVerdict run(const std::vector<uint8_t>& data, size_t chunk) {
    OtaImageCheck check;
    assert(ota_image_check_begin(check, MARKER, PREFIX));
    for (size_t at = 0; at < data.size(); at += chunk) {
        const size_t n = data.size() - at < chunk ? data.size() - at : chunk;
        const OtaImageVerdict v = ota_image_check_feed(check, data.data() + at, n);
        if (v != OTA_IMAGE_PENDING) {
            assert(ota_image_check_finish(check) == v);   // sticks
            return v;
        }
    }
    return ota_image_check_finish(check);
}

static void expect(const std::vector<uint8_t>& data, OtaImageVerdict want) {
    // Every chunking, including one byte at a time, gives the same answer.
    const size_t chunks[] = {1, 2, 3, 7, 13, 23, 24, 25, 64, 1436, 100000};
    for (size_t c : chunks) {
        const OtaImageVerdict got = run(data, c);
        if (got != want) {
            std::printf("chunk %zu: got %d want %d\n", c, got, want); std::fflush(stdout);
            assert(false);
        }
    }
}

int main() {
    assert(std::string(MARKER) == "PUMPKIN-IMAGE|ulanzi-tc001-esp32-4mb|");

    // A Pumpkin image for this hardware
    expect(image(std::string("\0", 1) + MARKER + std::string("\0", 1)), OTA_IMAGE_OK);
    // The marker split by every possible chunk border is covered by expect().

    // Another ESP32 project (no marker at all)
    expect(image("esphome 2026.9 ota firmware"), OTA_IMAGE_NOT_PUMPKIN);
    // Pumpkin built for another hardware ID
    expect(image("PUMPKIN-IMAGE|ulanzi-tc001-esp32-8mb|"), OTA_IMAGE_WRONG_HARDWARE);
    // The marker with something extra in front still counts
    expect(image("xxPUMPKIN-IMAGE|ulanzi-tc001-esp32-4mb|"), OTA_IMAGE_OK);
    // Not an app image: wrong magic (a zip, a text file, a merged factory bin
    // starting with the bootloader at 0x1000 padding)
    expect(image(MARKER, 0x50), OTA_IMAGE_NOT_FIRMWARE);
    expect(image(MARKER, 0xFF), OTA_IMAGE_NOT_FIRMWARE);
    // ESP32-S3 image (chip id 9) and ESP32-C3 (5), even with a matching marker
    expect(image(MARKER, 0xE9, 9), OTA_IMAGE_WRONG_CHIP);
    expect(image(MARKER, 0xE9, 5), OTA_IMAGE_WRONG_CHIP);

    // Header verdicts come before the end of the file
    {
        OtaImageCheck check;
        assert(ota_image_check_begin(check, MARKER, PREFIX));
        std::vector<uint8_t> bad = image(MARKER, 0x00);
        assert(ota_image_check_feed(check, bad.data(), OTA_IMAGE_HEADER_LEN) == OTA_IMAGE_NOT_FIRMWARE);
        assert(ota_image_check_feed(check, bad.data(), 10) == OTA_IMAGE_NOT_FIRMWARE);
    }
    // A file shorter than the header
    {
        OtaImageCheck check;
        assert(ota_image_check_begin(check, MARKER, PREFIX));
        const uint8_t tiny[] = {0xE9, 1, 2};
        assert(ota_image_check_feed(check, tiny, sizeof(tiny)) == OTA_IMAGE_PENDING);
        assert(ota_image_check_finish(check) == OTA_IMAGE_TRUNCATED);
        OtaImageCheck empty;
        assert(ota_image_check_begin(empty, MARKER, PREFIX));
        assert(ota_image_check_finish(empty) == OTA_IMAGE_TRUNCATED);
    }
    // Self-overlapping markers (KMP fallback) are found
    {
        OtaImageCheck check;
        assert(ota_image_check_begin(check, "aab|", 2));
        std::vector<uint8_t> data(OTA_IMAGE_HEADER_LEN, 0);
        data[0] = 0xE9;
        const std::string body = "aaaab|";
        data.insert(data.end(), body.begin(), body.end());
        for (uint8_t b : data) ota_image_check_feed(check, &b, 1);
        assert(ota_image_check_finish(check) == OTA_IMAGE_OK);
    }
    // Bad markers are refused, and a failed begin() never says OK
    {
        OtaImageCheck check;
        assert(!ota_image_check_begin(check, "", 0));
        assert(!ota_image_check_begin(check, nullptr, 1));
        assert(!ota_image_check_begin(check, "abc", 4));
        std::string long_marker(OTA_IMAGE_MARKER_MAX, 'x');
        assert(!ota_image_check_begin(check, long_marker.c_str(), 1));
        const std::vector<uint8_t> ok = image(MARKER);
        assert(ota_image_check_feed(check, ok.data(), ok.size()) == OTA_IMAGE_NOT_PUMPKIN);
        assert(ota_image_check_finish(check) == OTA_IMAGE_NOT_PUMPKIN);
    }

    assert(std::string(ota_image_verdict_error(OTA_IMAGE_NOT_PUMPKIN)) == "not_a_pumpkin_firmware");
    assert(std::string(ota_image_verdict_error(OTA_IMAGE_OK)).empty());
    std::puts("ota image check: ok");
    return 0;
}
