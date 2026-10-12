#ifndef CLOCK_IDENTITY_H
#define CLOCK_IDENTITY_H

// Pure helpers for naming a clock on the local network (no Arduino/ESP calls,
// unit-tested on the host). The network side lives in clock_net.cpp.

#include <stddef.h>
#include <stdint.h>

#define CLOCK_NAME_MAX 32        // display name, UTF-8 bytes
#define CLOCK_SLUG_MAX 32        // mDNS host label, without ".local"

// "a1b2c3d4e5f6": the chip's base MAC, lowercase hex (out >= 13 bytes).
void clock_id_from_mac(const uint8_t mac[6], char* out, size_t out_size);

// "pumpkin-e5f6": default display name before the user picks one.
void clock_default_name(const uint8_t mac[6], char* out, size_t out_size);

// "Pumpkin-Setup-E5F6": setup Wi-Fi network, unique per clock so two new
// clocks set up side by side are never mixed up.
void clock_setup_ssid(const uint8_t mac[6], char* out, size_t out_size);

// Display name -> host label: lowercase a-z0-9, common accented letters
// folded (é -> e, ß -> ss), everything else becomes one '-', no leading or
// trailing '-'. "Slaapkamer Élise" -> "slaapkamer-elise". False (out = "")
// when nothing usable is left.
bool clock_name_slug(const char* name, char* out, size_t out_size);

// A name the web UI may save: 1..CLOCK_NAME_MAX bytes, no control characters,
// and a usable slug.
bool clock_name_valid(const char* name);

// Host label with a collision suffix: ("keuken", 2) -> "keuken-2"; n <= 1
// leaves the label as is. The result never exceeds CLOCK_SLUG_MAX.
void clock_slug_with_suffix(const char* slug, int n, char* out, size_t out_size);

// Settings that "copy settings to other clocks" sends. Display and alert
// settings always; night/mute hours and the data source only when asked.
// Wi-Fi, the clock name, brightness, weather and other per-room or secret
// settings are never copied.
enum ClockCopyGroup : uint8_t {
    CLOCK_COPY_DISPLAY = 1,
    CLOCK_COPY_NIGHT = 2,
    CLOCK_COPY_SOURCE = 4,   // data source, URL, token, poll interval
};

// Group a config key belongs to, 0 when it is never copied.
uint8_t clock_copy_group(const char* key);

#endif // CLOCK_IDENTITY_H
