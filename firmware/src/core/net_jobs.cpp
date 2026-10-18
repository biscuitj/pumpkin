#include "net_jobs.h"

#include <stdio.h>

namespace pk {

namespace {
constexpr uint32_t kDefaultPollMs = 60000;
constexpr uint32_t kFirstRetryMs = 30000;
constexpr uint32_t kMaxRetryMs = 300000;
} // namespace

bool NetJobs::fetch(const char* url, const char* token, uint32_t unixNow, FetchOutcome& outcome, BridgeData& data,
                    uint32_t& ageSec) {
    HttpResult r;
    http_.get(url, token, r);
    outcome = FetchOutcome{};
    outcome.transportFailed = r.transportFailed;
    outcome.http = r.status;
    outcome.ms = r.ms;
    copyStr(outcome.error, r.error);
    data.clear();
    ageSec = UINT32_MAX;
    if (r.transportFailed || r.status != 200) {
        if (!r.transportFailed && !outcome.error[0]) snprintf(outcome.error, sizeof(outcome.error), "HTTP %d", r.status);
        return false;
    }
    const ParseResult p = parseClock(r.body.data(), r.body.size(), data);
    outcome.parsed = p == ParseResult::Ok;
    if (!outcome.parsed) {
        copyStr(outcome.error, p == ParseResult::NotJson ? "The bridge sent something that isn't JSON" : "No reading in the bridge's answer");
        return false;
    }
    // Age from the reading's own timestamp once the clock has the time;
    // until then, count it as new.
    ageSec = (unixNow > 1600000000u && data.timestamp > 0 && unixNow >= data.timestamp) ? unixNow - data.timestamp : 0;
    return true;
}

uint32_t NetJobs::step(bool online, uint32_t unixNow) {
    const uint32_t now = app_.platform.millis();

    // Copy what this step needs, then let go of the lock while the network runs.
    SourceKind kind;
    char url[kUrlMax + 1], token[kTokenMax + 1];
    uint8_t staleAfter;
    SourceTest::State testState;
    AlertsClear::State clearState;
    {
        Locked l(app_.mutex);
        kind = app_.settings.source.kind;
        copyStr(url, app_.settings.source.bridgeUrl);
        copyStr(token, app_.settings.source.token);
        staleAfter = app_.settings.display.glucose.staleAfterMin;
        testState = app_.test.state;
        clearState = app_.clear.state;
        if (testState == SourceTest::Requested) app_.test.state = SourceTest::Running;
        if (clearState == AlertsClear::Requested) app_.clear.state = AlertsClear::Running;
    }

    // A changed address starts over: no fetch counts as made yet.
    if (strcmp(url, lastUrl_) != 0) {
        copyStr(lastUrl_, url);
        fetchAt_ = 0;
    }

    // 1. Source test from the dashboard (address and token as typed there).
    if (testState == SourceTest::Requested) {
        char tUrl[kUrlMax + 1], tToken[kTokenMax + 1];
        {
            Locked l(app_.mutex);
            copyStr(tUrl, app_.test.url);
            copyStr(tToken, app_.test.token);
        }
        FetchOutcome o{};
        BridgeData d;
        uint32_t age = UINT32_MAX;
        if (online) {
            fetch(tUrl, tToken, unixNow, o, d, age);
        } else {
            d.clear();
            o.transportFailed = true;
            copyStr(o.error, "The clock is not on Wi-Fi");
        }
        Locked l(app_.mutex);
        app_.test.outcome = o;
        app_.test.data = d;
        app_.test.readingAgeSec = age;
        app_.test.result = classify(o, d, age, staleAfter);
        app_.test.state = SourceTest::Done;
        return 10;
    }

    // 2. Clear remembered alerts on the bridge.
    if (clearState == AlertsClear::Requested) {
        char clearUrl[kUrlMax + 40];
        HttpResult r;
        const bool haveUrl = kind == SourceKind::Bridge && remindersClearUrl(url, clearUrl, sizeof(clearUrl));
        if (haveUrl && online) http_.post(clearUrl, token, r);
        Locked l(app_.mutex);
        if (kind == SourceKind::Demo) {
            app_.clear.state = AlertsClear::Done;  // nothing to clear
        } else if (!haveUrl) {
            app_.clear.state = AlertsClear::Failed;
            copyStr(app_.clear.error, "The bridge address doesn't end in /clock");
        } else if (!online) {
            app_.clear.state = AlertsClear::Failed;
            copyStr(app_.clear.error, "The clock is not on Wi-Fi");
        } else if (r.transportFailed || r.status < 200 || r.status >= 300) {
            app_.clear.state = AlertsClear::Failed;
            if (r.transportFailed) copyStr(app_.clear.error, r.error[0] ? r.error : "The bridge didn't answer");
            else snprintf(app_.clear.error, sizeof(app_.clear.error), "The bridge answered %d", r.status);
        } else {
            app_.clear.state = AlertsClear::Done;
            app_.clear.error[0] = '\0';
            fetchAt_ = 0;  // show the result straight away
        }
        return 10;
    }

    // 3. Demo data needs no network.
    if (kind == SourceKind::Demo) {
        BridgeData d;
        uint32_t age;
        demoData(now, d, age);
        Locked l(app_.mutex);
        app_.source.haveData = true;
        app_.source.data = d;
        app_.source.readingAtMs = now - age * 1000u;
        app_.source.fetched = true;
        app_.source.failures = 0;
        app_.source.lastFetchAtMs = now;
        app_.source.state = SourceState::Demo;
        return 500;
    }

    // 4. The regular fetch.
    if (!url[0] || !token[0] || !online) return 1000;
    if (fetchAt_ != 0 && (int32_t)(now - fetchAt_) < 0) {
        const uint32_t wait = fetchAt_ - now;
        return wait < 1000 ? wait : 1000;
    }

    FetchOutcome o{};
    BridgeData d;
    uint32_t age = UINT32_MAX;
    const bool ok = fetch(url, token, unixNow, o, d, age);
    const uint32_t done = app_.platform.millis();

    Locked l(app_.mutex);
    // The settings may have changed while the request was out: drop the result.
    if (strcmp(app_.settings.source.bridgeUrl, url) != 0 || app_.settings.source.kind != SourceKind::Bridge) return 10;
    SourceRuntime& s = app_.source;
    s.fetched = true;
    s.lastFetchAtMs = done;
    s.last = o;
    if (ok) {
        s.haveData = true;
        s.data = d;
        s.readingAtMs = done - age * 1000u;
        s.failures = 0;
        fetchAt_ = done + (d.nextPollSec ? d.nextPollSec * 1000u : kDefaultPollMs);
    } else {
        if (s.failures < 255) ++s.failures;
        uint32_t backoff = kFirstRetryMs;
        for (int i = 1; i < s.failures && backoff < kMaxRetryMs; ++i) backoff *= 2;
        fetchAt_ = done + (backoff < kMaxRetryMs ? backoff : kMaxRetryMs);
    }
    // A 200 with an alert (e.g. login needed) keeps its data; other failures
    // keep the last reading, which goes stale by itself.
    s.state = classify(o, ok ? d : s.data, ok ? age : app_.readingAgeSec(done), staleAfter);
    if (s.state == SourceState::Ok && !ok) s.state = SourceState::Unreachable;
    return 10;
}

} // namespace pk
