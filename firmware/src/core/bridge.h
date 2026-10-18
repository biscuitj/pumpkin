// The Pumpkin bridge's GET /clock document (docs/DATA-SCHEMA.md §3), what one
// fetch means for the dashboard (SourceState), and demo data.
#pragma once

#include "types.h"

namespace pk {

constexpr int kMaxWarnings = 7;

struct BridgeData {
    bool valid;           // a reading is present
    int mgdl;
    Trend trend;
    uint32_t timestamp;   // unix seconds of the reading, 0 = unknown
    int tir;              // % in range over 24 h, -1 = unknown
    bool iobValid;
    float iob;
    bool tempTarget;      // sport mode
    Warning warnings[kMaxWarnings];
    uint8_t warningCount;
    char message[96];     // free text to scroll, "" = none
    char bridgeAlert[48]; // "CARELINK LOGIN NEEDED" / "CARELINK ERROR" (or Dutch), "" = none
    char upstream[12];    // "carelink" | "nightscout" | ""
    uint16_t nextPollSec; // bridge's hint, 0 = none

    void clear() {
        memset(this, 0, sizeof(*this));
        tir = -1;
        trend = Trend::Unknown;
    }
};

enum class ParseResult : uint8_t { Ok, NotJson, NoReading };
ParseResult parseClock(const char* body, size_t len, BridgeData& out);

Warning warningFromId(const char* id);
const char* warningId(Warning w);

// "https://x.workers.dev/clock" -> "https://x.workers.dev/reminders/clear?id=all".
bool remindersClearUrl(const char* clockUrl, char* out, size_t size);

// ---------------------------------------------------------------- source state

enum class SourceState : uint8_t {
    NotConfigured, Waiting, Ok, Stale, LoginNeeded, CarelinkError, Unauthorized, Unreachable, Demo
};
const char* sourceStateName(SourceState s);

// Result of one GET /clock.
struct FetchOutcome {
    bool transportFailed;  // DNS, TLS, timeout: no HTTP answer at all
    int http;              // status code when there was an answer
    bool parsed;           // 200 with a usable document
    char error[64];        // transport or parse detail
    uint32_t ms;           // round trip
};

SourceState classify(const FetchOutcome& f, const BridgeData& d, uint32_t readingAgeSec, uint8_t staleAfterMin);

// ---------------------------------------------------------------- demo

// Made-up data that walks through lows, highs, sport mode, insulin and every
// pump alert within a few minutes, so every screen can be tried without a pump.
// A new reading every 5 s.
void demoData(uint32_t uptimeMs, BridgeData& out, uint32_t& readingAgeSec);

} // namespace pk
