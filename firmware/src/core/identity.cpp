#include "identity.h"

#include <stdio.h>
#include <string.h>

void clock_id_from_mac(const uint8_t mac[6], char* out, size_t out_size) {
    snprintf(out, out_size, "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void clock_default_name(const uint8_t mac[6], char* out, size_t out_size) {
    snprintf(out, out_size, "pumpkin-%02x%02x", mac[4], mac[5]);
}

void clock_setup_ssid(const uint8_t mac[6], char* out, size_t out_size) {
    snprintf(out, out_size, "Pumpkin-Setup-%02X%02X", mac[4], mac[5]);
}

namespace {

// Latin-1 Supplement letters (U+00C0..U+00FF) folded to ASCII; "" = drop.
// Index = code point - 0xC0.
static const char* const LATIN1_FOLD[64] = {
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",   // C0-CF
    "d", "n", "o", "o", "o", "o", "o", "",  "o", "u", "u", "u", "u", "y", "th", "ss", // D0-DF
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",   // E0-EF
    "d", "n", "o", "o", "o", "o", "o", "",  "o", "u", "u", "u", "u", "y", "th", "y",  // F0-FF
};

struct SlugWriter {
    char* out;
    size_t cap;      // usable chars (out_size - 1, capped at CLOCK_SLUG_MAX)
    size_t len;
    bool pending_dash;
    bool full;       // out of room: stop rather than glue words together

    SlugWriter(char* o, size_t c) : out(o), cap(c), len(0), pending_dash(false), full(false) {}

    void letter(const char* s) {
        for (; *s && !full; ++s) {
            if (pending_dash && len > 0) {
                if (len + 2 > cap) { full = true; break; }   // need room for '-' + letter
                out[len++] = '-';
            }
            pending_dash = false;
            if (len >= cap) { full = true; break; }
            out[len++] = *s;
        }
    }
    void separator() { pending_dash = true; }
};

} // namespace

bool clock_name_slug(const char* name, char* out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    if (!name) return false;
    SlugWriter w(out, out_size - 1 < CLOCK_SLUG_MAX ? out_size - 1 : CLOCK_SLUG_MAX);
    const unsigned char* p = reinterpret_cast<const unsigned char*>(name);
    while (*p) {
        const unsigned char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            const char s[2] = {(char)c, 0};
            w.letter(s);
            ++p;
        } else if (c >= 'A' && c <= 'Z') {
            const char s[2] = {(char)(c - 'A' + 'a'), 0};
            w.letter(s);
            ++p;
        } else if (c == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
            // UTF-8 for U+00C0..U+00FF
            const char* fold = LATIN1_FOLD[p[1] - 0x80];
            if (fold[0]) w.letter(fold); else w.separator();
            p += 2;
        } else if (c >= 0x80) {
            // Any other multi-byte character: skip the whole sequence.
            ++p;
            while ((*p & 0xC0) == 0x80) ++p;
            w.separator();
        } else {
            w.separator();
            ++p;
        }
    }
    while (w.len > 0 && out[w.len - 1] == '-') --w.len;
    out[w.len] = '\0';
    return w.len > 0;
}

bool clock_name_valid(const char* name) {
    if (!name) return false;
    const size_t n = strlen(name);
    if (n == 0 || n > CLOCK_NAME_MAX) return false;
    for (size_t i = 0; i < n; ++i) {
        if ((unsigned char)name[i] < 0x20 || name[i] == 0x7F) return false;
    }
    char slug[CLOCK_SLUG_MAX + 1];
    return clock_name_slug(name, slug, sizeof(slug));
}

void clock_slug_with_suffix(const char* slug, int n, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (n <= 1) {
        snprintf(out, out_size, "%.*s", CLOCK_SLUG_MAX, slug ? slug : "");
        return;
    }
    char suffix[12];
    const int suffix_len = snprintf(suffix, sizeof(suffix), "-%d", n);
    int keep = CLOCK_SLUG_MAX - suffix_len;
    const int slug_len = slug ? (int)strlen(slug) : 0;
    if (keep > slug_len) keep = slug_len;
    while (keep > 0 && slug[keep - 1] == '-') --keep;   // no "--2"
    snprintf(out, out_size, "%.*s%s", keep, slug ? slug : "", suffix);
}
