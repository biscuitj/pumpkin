// Clock naming on the local network: default names, setup SSID, host labels,
// collision suffixes and which settings "copy to other clocks" sends.
#include "../src/clock_identity.cpp"
#include <cassert>
#include <cstdio>
#include <string>

static std::string slug(const char* name, size_t size = CLOCK_SLUG_MAX + 1) {
    char out[64];
    bool ok = clock_name_slug(name, out, size < sizeof(out) ? size : sizeof(out));
    assert(ok == (out[0] != '\0'));
    return out;
}

static std::string suffixed(const char* s, int n) {
    char out[CLOCK_SLUG_MAX + 1];
    clock_slug_with_suffix(s, n, out, sizeof(out));
    return out;
}

int main() {
    const uint8_t mac[6] = {0xA4, 0xCF, 0x12, 0x9B, 0xE5, 0xF6};
    char buf[64];
    clock_id_from_mac(mac, buf, sizeof(buf));      assert(std::string(buf) == "a4cf129be5f6");
    clock_default_name(mac, buf, sizeof(buf));     assert(std::string(buf) == "pumpkin-e5f6");
    clock_setup_ssid(mac, buf, sizeof(buf));       assert(std::string(buf) == "Pumpkin-Setup-E5F6");
    assert(slug("pumpkin-e5f6") == "pumpkin-e5f6");                 // default name is its own slug

    // Names -> host labels
    assert(slug("Keuken") == "keuken");
    assert(slug("Slaapkamer Emilie") == "slaapkamer-emilie");
    assert(slug("Slaapkamer \xC3\x89milie") == "slaapkamer-emilie");      // É
    assert(slug("Caf\xC3\xA9 & Co.") == "cafe-co");                       // é, punctuation collapses
    assert(slug("Stra\xC3\x9F" "e") == "strasse");                          // ß
    assert(slug("  --Living  room-- ") == "living-room");
    assert(slug("Kl\xC3\xA1ss 1B") == "klass-1b");
    assert(slug("\xE2\x9D\xA4 Lisa \xF0\x9F\x8E\x89") == "lisa");          // emoji dropped
    assert(slug("\xF0\x9F\x8E\x89").empty());                              // nothing usable
    assert(slug("!!!").empty() && slug("").empty() && slug(nullptr).empty());
    assert(slug("abcdefghijklmnopqrstuvwxyz0123456789").size() == CLOCK_SLUG_MAX);
    assert(slug("Keuken", 4) == "kek" || slug("Keuken", 4) == "keu");      // small buffers stay safe
    assert(slug("a b", 3) == "a");                                         // never ends in '-'

    // What the web UI accepts
    assert(clock_name_valid("Keuken") && clock_name_valid("Slaapkamer \xC3\x89milie"));
    assert(!clock_name_valid("") && !clock_name_valid("!!!") && !clock_name_valid(nullptr));
    assert(!clock_name_valid("bad\nname"));
    assert(clock_name_valid("12345678901234567890123456789012"));          // 32 bytes
    assert(!clock_name_valid("123456789012345678901234567890123"));        // 33 bytes

    // Collision suffixes
    assert(suffixed("keuken", 1) == "keuken" && suffixed("keuken", 0) == "keuken");
    assert(suffixed("keuken", 2) == "keuken-2");
    assert(suffixed("abcdefghijklmnopqrstuvwxyz012345", 2) == "abcdefghijklmnopqrstuvwxyz0123-2");
    assert(suffixed("abcdefghijklmnopqrstuvwxyz01234-", 3).size() <= CLOCK_SLUG_MAX);
    assert(suffixed("abcdefghijklmnopqrstuvwxyz0123-x", 2) == "abcdefghijklmnopqrstuvwxyz0123-2"); // no "--2"

    // Settings copy groups
    assert(clock_copy_group("thresh_low") == CLOCK_COPY_DISPLAY);
    assert(clock_copy_group("alerts_replace_pet") == CLOCK_COPY_DISPLAY);
    assert(clock_copy_group("night_full_mute") == CLOCK_COPY_NIGHT);
    assert(clock_copy_group("auth_token") == CLOCK_COPY_SOURCE);
    const char* const never[] = {"wifi_ssid", "wifi_password", "clock_name", "brightness", "auto_brightness",
                                 "weather_city", "dexcom_username", "libre_email", "countdown_name", nullptr};
    for (const char* key : never) assert(clock_copy_group(key) == 0);
    puts("clock identity: ok");
    return 0;
}
