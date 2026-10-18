// The network task's work: fetch /clock when it's due, run source tests and
// "clear remembered alerts" when the dashboard asks. Blocking calls are fine
// here: this runs on its own task, never on the render loop or the web server.
#pragma once

#include <string>

#include "app.h"

namespace pk {

struct HttpResult {
    bool transportFailed = false;
    int status = 0;
    char error[64] = "";
    uint32_t ms = 0;
    std::string body;
};

class HttpTransport {
public:
    virtual ~HttpTransport() {}
    // GET/POST with "Authorization: Bearer <token>" when token is non-empty.
    virtual void get(const char* url, const char* token, HttpResult& out) = 0;
    virtual void post(const char* url, const char* token, HttpResult& out) = 0;
};

class NetJobs {
public:
    NetJobs(App& app, HttpTransport& http) : app_(app), http_(http) {}

    // Do at most one job. `unixNow` is 0 while the clock has no time.
    // Returns how long the caller may sleep before the next call.
    uint32_t step(bool online, uint32_t unixNow);

    // Settings changed or the dashboard wants fresh data.
    void fetchSoon() { fetchAt_ = 0; }

private:
    bool fetch(const char* url, const char* token, uint32_t unixNow, FetchOutcome& outcome, BridgeData& data,
               uint32_t& ageSec);

    App& app_;
    HttpTransport& http_;
    uint32_t fetchAt_ = 0;
    char lastUrl_[kUrlMax + 1] = "";
};

} // namespace pk
