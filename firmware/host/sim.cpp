// Pumpkin simulator: the firmware core (API, engine, network jobs) with fake
// Wi-Fi, a fake bridge and the computer's clock, behind HTTP on port 8080.
//
//   make -C host run-sim
//   cd dashboard && PUMPKIN_HOST=localhost:8080 npm run dev
//
// Options: --port N, --www DIR (also serve the built dashboard, e.g. ../dashboard/dist),
// --setup (start in the setup hotspot).
//
// The fake bridge follows the same inputs as the dashboard's mock: an address
// containing "down", "login", "stale" or "ns", or the token "bad", gives that
// failure. Wi-Fi password "wrong" fails to join, network "nope" doesn't exist,
// "Schoolnet" blocks NTP.
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#include "core/api.h"
#include "core/glucose.h"
#include "core/identity.h"
#include "core/image_check.h"
#include "core/net_jobs.h"
#include "core/runner.h"
#include "http_server.h"

using namespace pk;

namespace {

uint32_t nowMs() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - start).count() + 1;
}

const char* kVersionBase = "1.0.0";

// ---------------------------------------------------------------- platform

struct SimPlatform : Platform {
    std::mutex m;
    std::string version = kVersionBase;
    char rolledBackFrom[24] = "";
    uint32_t offlineUntil = 0;
    std::atomic<bool> rebootPending{false};
    std::atomic<bool> resetPending{false};
    NetJobs* jobs = nullptr;
    App* app = nullptr;

    // Wi-Fi
    bool setupAp = false;
    std::string ssid = "Thuis";
    WifiSecurity security = WifiSecurity::Personal;
    bool ca = false;
    struct Trial {
        bool active = false;
        WifiJoinRequest req{};
        uint32_t startedAt = 0;
        TrialState outcome = TrialState::Idle;
        TrialState shown = TrialState::Idle;
    } trial;
    uint32_t scanStartedAt = 0, scanDoneAt = 0;
    bool ntpBlocked = false;
    uint32_t checksAt = 1;

    UpdateView update;

    uint32_t millis() override { return nowMs(); }

    TrialState trialState() {
        if (!trial.active) return trial.shown;
        const uint32_t t = nowMs() - trial.startedAt;
        const uint32_t auth = trial.req.security == WifiSecurity::Enterprise ? 3000 : 1500;
        if (t < 1200) return TrialState::Associating;
        if (t < 1200 + auth) return TrialState::Authenticating;
        trial.active = false;
        trial.shown = trial.outcome;
        if (trial.outcome == TrialState::Connected) {
            ssid = trial.req.ssid;
            security = trial.req.security;
            setupAp = false;
            ntpBlocked = ssid == "Schoolnet";
            checksAt = nowMs();
            // Like the clock: credentials are saved only once the join works.
            Locked l(app->mutex);
            WifiSettings& w = app->settings.wifi;
            copyStr(w.ssid, trial.req.ssid);
            w.security = trial.req.security;
            w.eap = trial.req.eap;
            copyStr(w.password, trial.req.password);
            copyStr(w.identity, trial.req.identity);
            copyStr(w.anonIdentity, trial.req.anonIdentity);
            w.validateCa = trial.req.validateCa;
            app->markDirty(kGroupWifi);
        }
        return trial.shown;
    }

    void wifiView(WifiView& out) override {
        std::lock_guard<std::mutex> l(m);
        out = WifiView{};
        const TrialState t = trialState();
        const bool joining = t == TrialState::Associating || t == TrialState::Authenticating;
        out.trial = t;
        copyStr(out.trialSsid, trial.req.ssid);
        if (setupAp && !joining) {
            out.mode = WifiMode::SetupAp;
            out.ap = true;
            copyStr(out.apSsid, "Pumpkin-Setup-E5F6");
            copyStr(out.apIp, "192.168.4.1");
            out.apStations = 1;
        } else {
            out.mode = joining ? WifiMode::Connecting : WifiMode::Connected;
            copyStr(out.ssid, joining ? trial.req.ssid : ssid.c_str());
            out.hasSecurity = true;
            out.security = security;
            out.hasRssi = true;
            out.rssi = -52 + (int)((nowMs() / 3000) % 5) - 2;
            copyStr(out.ip, "192.168.1.50");
            char slug[CLOCK_SLUG_MAX + 1];
            {
                Locked al(app->mutex);
                clockSlug(slug, sizeof(slug));
            }
            if (out.mode == WifiMode::Connected) snprintf(out.localName, sizeof(out.localName), "%s.local", slug);
        }
        out.caInstalled = ca;
        if (t == TrialState::FailedAuth) copyStr(out.trialDetail, "4-way handshake failed (reason 15)");
    }

    void clockSlug(char* out, size_t size);

    int wifiScan(ScanEntry* out, int max, bool& scanning, int32_t& ageSec) override {
        std::lock_guard<std::mutex> l(m);
        if (scanStartedAt && nowMs() - scanStartedAt > 2500) {
            scanStartedAt = 0;
            scanDoneAt = nowMs();
        }
        scanning = scanStartedAt != 0;
        ageSec = scanDoneAt ? (int32_t)((nowMs() - scanDoneAt) / 1000) : -1;
        static const ScanEntry kNets[] = {
            {"Thuis", -52, true, false}, {"Schoolnet", -67, true, true}, {"telenet-2C4F1", -74, true, false}, {"Gasten", -81, false, false}};
        int n = 0;
        for (const auto& e : kNets)
            if (n < max) out[n++] = e;
        return scanDoneAt ? n : 0;
    }

    bool wifiStartScan() override {
        std::lock_guard<std::mutex> l(m);
        if (scanStartedAt) return false;
        scanStartedAt = nowMs();
        return true;
    }

    bool wifiJoin(const WifiJoinRequest& r) override {
        std::lock_guard<std::mutex> l(m);
        const TrialState t = trialState();
        if (t == TrialState::Associating || t == TrialState::Authenticating) return false;
        trial.active = true;
        trial.req = r;
        trial.startedAt = nowMs();
        trial.outcome = !strcmp(r.ssid, "nope") ? TrialState::FailedNoAp
                      : !strcmp(r.password, "wrong") ? TrialState::FailedAuth
                                                     : TrialState::Connected;
        return true;
    }

    bool caWrite(const char*, size_t) override { return ca = true; }
    bool caDelete() override { return !(ca = false); }
    bool caInstalled() override { return ca; }

    void netRecheck() override { checksAt = nowMs(); }
    void wakeNetwork() override {
        if (jobs) jobs->fetchSoon();
    }

    void localTime(LocalTime& out, TimeSource& src) override {
        time_t t = time(nullptr);
        struct tm tm;
        localtime_r(&t, &tm);
        out = LocalTime{true, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (tm.tm_wday + 6) % 7};
        src = TimeSource::Ntp;
    }
    void applyTimezone(const char* posix) override {
        setenv("TZ", posix, 1);
        tzset();
    }

    const char* defaultName() override { return "pumpkin-e5f6"; }
    void nameChanged() override {}
    void diagnostics(Diagnostics& d) override {
        const uint32_t up = nowMs() / 1000;
        d = Diagnostics{up, "power_on", 142000u + (up % 7) * 900u, 118400, 81908, (uint16_t)(1800 + (up % 40) * 20), 42, false,
                        (up % 6) == 0, false};
    }
    void restartSoon() override { rebootPending = true; }
    void factoryResetSoon() override {
        resetPending = true;
        rebootPending = true;
    }
    const char* firmwareVersion() override { return version.c_str(); }
    const char* hardware() override { return "Ulanzi TC001 (simulator)"; }
    bool rolledBack(char* from, size_t size) override {
        if (!rolledBackFrom[0]) return false;
        copyStr(from, size, rolledBackFrom);
        return true;
    }
    void updateView(UpdateView& out) override { out = update; }

    bool offline() { return (int32_t)(offlineUntil - nowMs()) > 0; }
};

SimPlatform* gPlatform = nullptr;

void SimPlatform::clockSlug(char* out, size_t size) {
    if (!clock_name_slug(app->name(), out, size)) copyStr(out, size, "pumpkin");
}

// ---------------------------------------------------------------- fake bridge

struct FakeBridge : HttpTransport {
    void get(const char* url, const char* token, HttpResult& out) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        out.ms = 150;
        if (strstr(url, "down")) {
            out.transportFailed = true;
            copyStr(out.error, "Connection timed out");
            return;
        }
        if (!strcmp(token, "bad")) {
            out.status = 401;
            out.body = R"({"error":"unauthorized"})";
            return;
        }
        if (strstr(url, "stale")) {
            out.status = 503;
            out.body = R"({"error":"stale","age_min":25})";
            return;
        }
        // Real-looking data from the demo generator, dated now.
        BridgeData d;
        uint32_t age;
        demoData(nowMs() * 3, d, age);  // a bit faster than real time
        JsonDocument doc;
        doc["glucose"] = d.mgdl;
        doc["trend"] = trendName(d.trend);
        doc["timestamp"] = (uint32_t)time(nullptr) - 90;
        doc["tir"] = d.tir;
        doc["iob"] = d.iob;
        doc["temp_target"] = d.tempTarget;
        JsonArray w = doc["warnings"].to<JsonArray>();
        for (int i = 0; i < d.warningCount; ++i) w.add(warningId(d.warnings[i]));
        doc["source"] = strstr(url, "ns") ? "nightscout" : "carelink";
        doc["bridge_alert"] = strstr(url, "login") ? "CARELINK LOGIN NEEDED" : "";
        doc["next_poll_sec"] = 30;
        doc["message"] = "";
        out.status = 200;
        serializeJson(doc, out.body);
    }
    void post(const char* url, const char* token, HttpResult& out) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        out.status = strcmp(token, "bad") ? 200 : 401;
        out.body = "{}";
    }
};

// ---------------------------------------------------------------- update upload

void handleUpdate(App& app, SimPlatform& p, const SimRequest& req, SimResponse& res) {
    {
        if (p.update.state == UpdateView::Receiving) {
            HttpResponse r;
            apiError(r, 409, "An update is already running.", "busy");
            res.status = r.status;
            res.body = r.body;
            return;
        }
        p.update = UpdateView{};
        p.update.state = UpdateView::Receiving;
    }
    OtaImageCheck check;
    ota_image_check_begin(check, PUMPKIN_IMAGE_MARKER, sizeof(PUMPKIN_IMAGE_MARKER_PREFIX) - 1);
    OtaImageVerdict v = OTA_IMAGE_PENDING;
    for (size_t i = 0; i < req.body.size() && v == OTA_IMAGE_PENDING; i += 4096) {
        v = ota_image_check_feed(check, (const uint8_t*)req.body.data() + i, std::min<size_t>(4096, req.body.size() - i));
        p.update.received = (uint32_t)std::min(req.body.size(), i + 4096);
    }
    if (v == OTA_IMAGE_PENDING) v = ota_image_check_finish(check);
    if (v != OTA_IMAGE_OK) {
        p.update.state = UpdateView::Failed;
        copyStr(p.update.error, ota_image_verdict_error(v));
        static const char* const kMessages[] = {"", "", "That file isn't clock firmware.", "That firmware is for a different chip.",
                                                "That's ESP32 firmware, but not Pumpkin.", "That Pumpkin firmware is for different hardware.",
                                                "The file is incomplete."};
        HttpResponse r;
        apiError(r, 400, kMessages[(int)v], ota_image_verdict_error(v));
        res.status = r.status;
        res.body = r.body;
        return;
    }
    p.update.state = UpdateView::Restarting;
    // The simulated new version: patch number + 1, or a failed first boot when
    // the image contains "ROLLBACK".
    const bool rollback = req.body.find("ROLLBACK") != std::string::npos;
    int a = 1, b = 0, c = 0;
    sscanf(p.version.c_str(), "%d.%d.%d", &a, &b, &c);
    char next[24];
    snprintf(next, sizeof(next), "%d.%d.%d", a, b, c + 1);
    if (rollback) copyStr(p.rolledBackFrom, next);
    else {
        p.version = next;
        p.rolledBackFrom[0] = '\0';
    }
    p.rebootPending = true;
    res.status = 202;
    res.body.clear();
}

// ---------------------------------------------------------------- static files

bool serveFile(const std::string& root, const std::string& path, SimResponse& res) {
    if (root.empty() || path.find("..") != std::string::npos) return false;
    std::string file = root + (path == "/" ? "/index.html" : path);
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    res.status = 200;
    res.body = ss.str();
    res.contentType = file.size() > 5 && file.substr(file.size() - 5) == ".html" ? "text/html; charset=utf-8" : "application/octet-stream";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    int port = 8080;
    std::string www;
    bool setup = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--www") && i + 1 < argc) www = argv[++i];
        else if (!strcmp(argv[i], "--setup")) setup = true;
    }

    static SimPlatform platform;
    static App app(platform);
    static FakeBridge bridge;
    static NetJobs jobs(app, bridge);
    static Runner runner(app);
    platform.app = &app;
    platform.jobs = &jobs;
    platform.setupAp = setup;
    gPlatform = &platform;
    platform.applyTimezone(app.settings.device.posix);
    if (!setup) copyStr(app.settings.wifi.ssid, "Thuis");
    runner.begin();

    // Network task
    std::thread([&] {
        for (;;) {
            if (platform.offline()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            // Net checks: pending for 2 s after a (re)connect or recheck.
            {
                Locked l(app.mutex);
                const bool running = nowMs() - platform.checksAt < 2000;
                app.net.running = running;
                app.net.dns = running ? CheckResult::Pending : CheckResult::Ok;
                app.net.data = running ? CheckResult::Pending
                             : strstr(app.settings.source.bridgeUrl, "down") ? CheckResult::Fail
                                                                               : CheckResult::Ok;
                app.net.ntp = running ? CheckResult::Pending : platform.ntpBlocked ? CheckResult::Fail : CheckResult::Ok;
                hostOfUrl(app.settings.source.bridgeUrl, app.net.dataHost, sizeof(app.net.dataHost));
            }
            const uint32_t wait = jobs.step(!platform.setupAp, (uint32_t)time(nullptr));
            std::this_thread::sleep_for(std::chrono::milliseconds(wait < 50 ? 50 : wait > 200 ? 200 : wait));
        }
    }).detach();

    // Render loop: 20 fps, like the clock.
    std::thread([&] {
        for (;;) {
            if (platform.rebootPending) {
                std::this_thread::sleep_for(std::chrono::milliseconds(300));  // let the answer go out
                platform.offlineUntil = nowMs() + 4000;
                if (platform.resetPending) {
                    Locked l(app.mutex);
                    setDefaults(app.settings);
                    platform.setupAp = true;
                    platform.resetPending = false;
                }
                platform.rebootPending = false;
                {
                    std::lock_guard<std::mutex> wl(platform.m);
                    platform.trial = SimPlatform::Trial{};  // a restart forgets the last join
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(4000));
                platform.update = UpdateView{};
                {
                    Locked l(app.mutex);
                    app.resetSource();
                }
                runner.begin();
                jobs.fetchSoon();
            }
            HardwareInputs hw;
            hw.light = 2048;
            hw.updating = platform.update.state == UpdateView::Receiving;
            runner.frame(hw);
            {
                Locked l(app.mutex);
                app.dirty = 0;  // nothing to persist in the simulator
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }).detach();

    printf("Pumpkin simulator on http://localhost:%d%s\n", port, www.empty() ? "" : (" (serving " + www + ")").c_str());
    fflush(stdout);
    serveHttp(port, [&](const SimRequest& req, SimResponse& res) {
        if (platform.offline()) {
            res.drop = true;  // a restarting clock doesn't answer
            return;
        }
        if (req.method == "POST" && req.path == "/api/update") return handleUpdate(app, platform, req, res);
        HttpRequest r;
        r.method = req.method.c_str();
        r.path = req.path.c_str();
        r.body = req.body.data();
        r.bodyLen = req.body.size();
        const std::string inm = req.header("if-none-match");
        r.ifNoneMatch = inm.c_str();
        HttpResponse out;
        if (handleApi(app, r, out)) {
            res.status = out.status;
            res.contentType = out.contentType;
            res.body = std::move(out.body);
            if (!out.etag.empty()) res.headers["ETag"] = out.etag;
            if (out.screen) res.headers["X-Screen"] = out.screen;
            res.headers["Cache-Control"] = "no-store";
            return;
        }
        if (req.method == "GET") {
            const std::string page = platform.setupAp && req.path == "/" ? "/setup.html" : req.path;
            if (serveFile(www, page, res)) return;
        }
        res.status = 404;
        res.contentType = "text/plain";
        res.body = "Not found";
    });
}
