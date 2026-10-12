#ifndef CLOCK_NET_H
#define CLOCK_NET_H

// Clocks on the local network: this clock's name and .local address (mDNS),
// the other SugarClocks it can see, and copying settings to them.
//
// All mDNS and clock-to-clock HTTP runs on a small background task, so the
// display loop and the async web server never block on the network.

#include <stdint.h>

#define CLOCK_NET_MAX_PEERS 8

struct ClockPeer {
    char name[33];      // display name (TXT "name"), UTF-8
    char host[40];      // "keuken.local"
    char ip[16];
    char version[16];   // firmware version (TXT "ver")
    char id[13];        // base MAC, hex (TXT "id")
};

enum ClockCopyState : uint8_t {
    CLOCK_COPY_IDLE = 0,
    CLOCK_COPY_PENDING,
    CLOCK_COPY_OK,
    CLOCK_COPY_FAILED,
};

struct ClockCopyResult {
    char ip[16];
    char name[33];
    uint8_t state;      // ClockCopyState
    int http_code;      // target's answer (or a negative HTTPClient error)
};

// Before wifi_init(): reads the chip ID, so the setup network name and the DHCP
// hostname are known before the radio starts.
void clock_net_init();

// After wifi_init(): starts the background task (mDNS + discovery + copies).
void clock_net_start();

const char* clock_net_id();          // "a1b2c3d4e5f6"
const char* clock_net_name();        // display name (configured or default)
const char* clock_net_setup_ssid();  // "SugarClock-Setup-E5F6"
const char* clock_net_slug();        // host label this clock asks for ("keuken")

// "keuken.local" once mDNS is running (with a -2 suffix if the name was
// taken), "" otherwise. Safe from any task.
const char* clock_net_local_host();

// The clock name setting changed: re-announce under the new name.
void clock_net_name_changed();

// Copy of the clocks seen on the last scan (this clock excluded). Scans run
// about once a minute; clock_net_rescan() asks for one now.
int clock_net_peers(ClockPeer* out, int max);
bool clock_net_scanning();
unsigned long clock_net_last_scan_age_ms();   // ULONG_MAX before the first scan
void clock_net_rescan();

// Queue a settings copy: POST `body` (JSON) to http://<ip>/api/config on each
// target. Targets must be clocks from the current peer list. False when a copy
// is still running, the list is empty or a target isn't a known clock.
bool clock_net_copy_start(const char* const* target_ips, int count, const char* body);
int clock_net_copy_results(ClockCopyResult* out, int max, bool* running);

#endif // CLOCK_NET_H
