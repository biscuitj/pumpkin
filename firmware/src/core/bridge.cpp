#include "bridge.h"

#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>

#include "glucose.h"

namespace pk {

namespace {
const char* const kWarningIds[] = {"", "suspended", "warning", "battery", "reservoir", "bg", "sensor", "set"};
static_assert(sizeof(kWarningIds) / sizeof(kWarningIds[0]) == (size_t)Warning::Count, "warning ids");
} // namespace

Warning warningFromId(const char* id) {
    if (!id) return Warning::None;
    for (uint8_t i = 1; i < (uint8_t)Warning::Count; ++i)
        if (strcmp(id, kWarningIds[i]) == 0) return (Warning)i;
    return Warning::None;
}

const char* warningId(Warning w) { return (uint8_t)w < (uint8_t)Warning::Count ? kWarningIds[(uint8_t)w] : ""; }

ParseResult parseClock(const char* body, size_t len, BridgeData& out) {
    out.clear();
    JsonDocument doc;
    if (deserializeJson(doc, body, len) || !doc.is<JsonObjectConst>()) return ParseResult::NotJson;
    JsonObjectConst src = doc.as<JsonObjectConst>();

    out.mgdl = src["glucose"] | 0;
    out.timestamp = src["timestamp"] | 0UL;
    out.trend = trendFromName(src["trend"] | "NONE");
    out.valid = out.mgdl > 0;

    int tir = src["tir"] | -1;
    out.tir = (tir >= 0 && tir <= 100) ? tir : -1;

    JsonVariantConst iob = src["iob"];
    if (iob.is<float>()) {
        float u = iob.as<float>();
        if (u >= 0.0f && u < 100.0f) {
            out.iob = u;
            out.iobValid = true;
        }
    }
    out.tempTarget = src["temp_target"] | false;

    // Unknown ids are skipped, so a newer bridge never breaks this firmware.
    for (JsonVariantConst w : src["warnings"].as<JsonArrayConst>()) {
        Warning id = warningFromId(w.as<const char*>());
        if (id == Warning::None || out.warningCount >= kMaxWarnings) continue;
        bool seen = false;
        for (uint8_t i = 0; i < out.warningCount; ++i) seen |= out.warnings[i] == id;
        if (!seen) out.warnings[out.warningCount++] = id;
    }

    copyStr(out.message, src["message"] | "");
    copyStr(out.bridgeAlert, src["bridge_alert"] | "");
    copyStr(out.upstream, src["source"] | "");

    int next = src["next_poll_sec"] | 0;
    if (next > 0) out.nextPollSec = (uint16_t)(next < 10 ? 10 : next > 300 ? 300 : next);

    return out.valid ? ParseResult::Ok : ParseResult::NoReading;
}

bool remindersClearUrl(const char* clockUrl, char* out, size_t size) {
    if (!out || size == 0) return false;
    out[0] = '\0';
    if (!clockUrl) return false;
    const char* q = strchr(clockUrl, '?');
    const size_t pathLen = q ? (size_t)(q - clockUrl) : strlen(clockUrl);
    static const char kSuffix[] = "/clock";
    const size_t n = sizeof(kSuffix) - 1;
    if (pathLen <= n || strncmp(clockUrl + pathLen - n, kSuffix, n) != 0) return false;
    const int written = snprintf(out, size, "%.*s/reminders/clear?id=all", (int)(pathLen - n), clockUrl);
    if (written < 0 || (size_t)written >= size) {
        out[0] = '\0';
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- state

const char* sourceStateName(SourceState s) {
    switch (s) {
        case SourceState::NotConfigured: return "not_configured";
        case SourceState::Waiting: return "waiting";
        case SourceState::Ok: return "ok";
        case SourceState::Stale: return "stale";
        case SourceState::LoginNeeded: return "login_needed";
        case SourceState::CarelinkError: return "carelink_error";
        case SourceState::Unauthorized: return "unauthorized";
        case SourceState::Unreachable: return "unreachable";
        case SourceState::Demo: return "demo";
    }
    return "waiting";
}

SourceState classify(const FetchOutcome& f, const BridgeData& d, uint32_t ageSec, uint8_t staleAfterMin) {
    if (f.transportFailed) return SourceState::Unreachable;
    if (f.http == 401 || f.http == 403) return SourceState::Unauthorized;
    if (f.http == 503) return SourceState::Stale;  // the bridge's "no fresh reading anywhere"
    if (f.http != 200 || !f.parsed) return SourceState::Unreachable;
    if (d.bridgeAlert[0]) {
        // English "CARELINK LOGIN NEEDED", Dutch "CARELINK LOGIN NODIG".
        return strstr(d.bridgeAlert, "LOGIN") ? SourceState::LoginNeeded : SourceState::CarelinkError;
    }
    if (ageSec != UINT32_MAX && ageSec >= (uint32_t)staleAfterMin * 60u) return SourceState::Stale;
    return SourceState::Ok;
}

// ---------------------------------------------------------------- demo

namespace {
int demoGlucose(uint32_t n) {
    // One loop through the whole range every 36 readings (3 minutes).
    return (int)lroundf(150.0f + 110.0f * sinf(6.2831853f * (float)n / 36.0f));
}
} // namespace

void demoData(uint32_t uptimeMs, BridgeData& out, uint32_t& readingAgeSec) {
    out.clear();
    const uint32_t n = uptimeMs / 5000u + 1;  // reading number
    out.valid = true;
    out.mgdl = demoGlucose(n);
    const int delta = out.mgdl - demoGlucose(n - 1);
    out.trend = delta > 15  ? Trend::DoubleUp
              : delta > 8   ? Trend::SingleUp
              : delta > 3   ? Trend::FortyFiveUp
              : delta >= -3 ? Trend::Flat
              : delta >= -8 ? Trend::FortyFiveDown
              : delta >= -15 ? Trend::SingleDown
                             : Trend::DoubleDown;
    out.timestamp = 1700000000u + n * 5u;  // synthetic, unique per reading
    out.tir = 78;
    out.iobValid = true;
    out.iob = 0.2f + 1.5f * (0.5f + 0.5f * sinf((float)n / 20.0f));
    out.tempTarget = ((n / 36) % 3) == 1;
    // Every two minutes the next alert type, with quiet stretches between.
    const uint32_t k = (n / 24) % 9;
    if (k >= 2) {
        out.warnings[0] = (Warning)(k - 1);
        out.warningCount = 1;
    }
    copyStr(out.upstream, "demo");
    readingAgeSec = (uptimeMs % 5000u) / 1000u;
}

} // namespace pk
