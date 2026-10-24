#include "api.h"

#include <stdio.h>

#include "glucose.h"
#include "identity.h"
#include "pets.h"
#include "schedule.h"

namespace pk {

namespace {

// ---------------------------------------------------------------- helpers

void sendJson(HttpResponse& res, int status, const JsonDocument& doc) {
    res.status = status;
    res.contentType = "application/json";
    res.body.clear();
    serializeJson(doc, res.body);
}

void noContent(HttpResponse& res, int status = 204) {
    res.status = status;
    res.body.clear();
}

void accepted(HttpResponse& res) { noContent(res, 202); }

bool parseBody(const HttpRequest& req, JsonDocument& doc, HttpResponse& res) {
    if (req.bodyLen == 0 || deserializeJson(doc, req.body, req.bodyLen) || !doc.is<JsonObject>()) {
        apiError(res, 400, "Invalid JSON");
        return false;
    }
    return true;
}

void setOrNull(JsonObject o, const char* key, const char* s) {
    if (s && s[0]) o[key] = s;
    else o[key] = nullptr;
}

const char* checkName(CheckResult c) {
    switch (c) {
        case CheckResult::Pending: return "pending";
        case CheckResult::Ok: return "ok";
        case CheckResult::Fail: return "fail";
        default: return "skipped";
    }
}

const char* trialName(TrialState t) {
    switch (t) {
        case TrialState::Associating: return "associating";
        case TrialState::Authenticating: return "authenticating";
        case TrialState::Connected: return "connected";
        case TrialState::FailedAuth: return "failed_auth";
        case TrialState::FailedNoAp: return "failed_no_ap";
        case TrialState::FailedTimeout: return "failed_timeout";
        default: return "idle";
    }
}

const char* wifiModeName(WifiMode m) {
    switch (m) {
        case WifiMode::Connected: return "connected";
        case WifiMode::Connecting: return "connecting";
        case WifiMode::SetupAp: return "setup_ap";
        default: return "disconnected";
    }
}

bool is(const HttpRequest& r, const char* method, const char* path) {
    return strcmp(r.method, method) == 0 && strcmp(r.path, path) == 0;
}

// ---------------------------------------------------------------- status

void getStatus(App& app, HttpResponse& res) {
    JsonDocument doc;
    doc["firmware_version"] = app.platform.firmwareVersion();
    doc["hardware"] = app.platform.hardware();
    doc["use_mmol"] = app.settings.display.glucose.unit == Unit::Mmol;
    doc["uptime_sec"] = app.platform.millis() / 1000u;
    sendJson(res, 200, doc);
}

// ---------------------------------------------------------------- Wi-Fi

void getWifi(App& app, HttpResponse& res) {
    WifiView w;
    app.platform.wifiView(w);
    JsonDocument doc;
    doc["mode"] = wifiModeName(w.mode);
    setOrNull(doc.as<JsonObject>(), "ssid", w.mode == WifiMode::Connected || w.mode == WifiMode::Connecting ? w.ssid : "");
    if (w.mode == WifiMode::Connected && w.hasSecurity) doc["security"] = enumToName(kSecurityNames, w.security);
    else doc["security"] = nullptr;
    if (w.mode == WifiMode::Connected && w.hasRssi) doc["rssi"] = w.rssi;
    else doc["rssi"] = nullptr;
    setOrNull(doc.as<JsonObject>(), "ip", w.mode == WifiMode::Connected ? w.ip : "");
    setOrNull(doc.as<JsonObject>(), "local_name", w.localName);
    if (w.ap) {
        JsonObject ap = doc["ap"].to<JsonObject>();
        ap["ssid"] = w.apSsid;
        ap["ip"] = w.apIp;
        ap["stations"] = w.apStations;
    } else {
        doc["ap"] = nullptr;
    }
    JsonObject ca = doc["ca"].to<JsonObject>();
    ca["installed"] = w.caInstalled;
    ca["validate"] = app.settings.wifi.validateCa;

    JsonObject c = doc["checks"].to<JsonObject>();
    const NetChecks& n = app.net;
    const bool online = w.mode == WifiMode::Connected;
    c["running"] = online && n.running;
    c["dns"] = checkName(online ? n.dns : CheckResult::Skipped);
    c["data"] = checkName(online ? n.data : CheckResult::Skipped);
    c["ntp"] = checkName(online ? n.ntp : CheckResult::Skipped);
    setOrNull(c, "data_host", n.dataHost);

    JsonObject t = doc["trial"].to<JsonObject>();
    t["state"] = trialName(w.trial);
    setOrNull(t, "ssid", w.trial == TrialState::Idle ? "" : w.trialSsid);
    setOrNull(t, "detail", w.trialDetail);
    sendJson(res, 200, doc);
}

void getNetworks(App& app, HttpResponse& res) {
    static ScanEntry entries[24];
    bool scanning = false;
    int32_t age = -1;
    const int n = app.platform.wifiScan(entries, 24, scanning, age);
    JsonDocument doc;
    doc["scanning"] = scanning;
    if (age >= 0) doc["age_sec"] = age;
    else doc["age_sec"] = nullptr;
    JsonArray list = doc["networks"].to<JsonArray>();
    for (int i = 0; i < n; ++i) {
        JsonObject e = list.add<JsonObject>();
        e["ssid"] = entries[i].ssid;
        e["rssi"] = entries[i].rssi;
        e["secure"] = entries[i].secure;
        e["enterprise"] = entries[i].enterprise;
    }
    sendJson(res, 200, doc);
}

void postJoin(App& app, const HttpRequest& req, HttpResponse& res) {
    JsonDocument doc;
    if (!parseBody(req, doc, res)) return;
    WifiJoinRequest j{};
    const char* ssid = doc["ssid"] | "";
    const size_t ssidLen = strlen(ssid);
    if (ssidLen == 0) return apiError(res, 400, "Enter the network name.", "ssid");
    if (ssidLen > 32) return apiError(res, 400, "Network names are 32 bytes at most.", "ssid");
    copyStr(j.ssid, ssid);
    if (!enumFromName(kSecurityNames, doc["security"] | "", j.security))
        return apiError(res, 400, "Unknown security type.", "security");
    const char* password = doc["password"] | "";
    if (j.security == WifiSecurity::Personal) {
        const size_t n = strlen(password);
        if (n && (n < 8 || n > 63)) return apiError(res, 400, "Wi-Fi passwords are 8 to 63 characters.", "password");
        copyStr(j.password, password);
    } else {
        if (!enumFromName(kEapNames, doc["eap"] | "peap", j.eap)) return apiError(res, 400, "Unknown method.", "eap");
        const char* identity = doc["identity"] | "";
        if (!identity[0]) return apiError(res, 400, "Enter the username.", "identity");
        if (strlen(identity) > 127) return apiError(res, 400, "The username is too long.", "identity");
        if (!password[0]) return apiError(res, 400, "Enter the password.", "password");
        if (strlen(password) > 127) return apiError(res, 400, "The password is too long.", "password");
        const char* anon = doc["anon_identity"] | "";
        if (strlen(anon) > 127) return apiError(res, 400, "The anonymous identity is too long.", "anon_identity");
        copyStr(j.identity, identity);
        copyStr(j.password, password);
        copyStr(j.anonIdentity, anon);
        j.validateCa = doc["validate_ca"] | false;
        if (j.validateCa && !app.platform.caInstalled())
            return apiError(res, 400, "Upload a certificate first.", "validate_ca");
    }
    if (!app.platform.wifiJoin(j)) return apiError(res, 409, "The clock is already trying a network.");
    accepted(res);
}

void putCa(App& app, const HttpRequest& req, HttpResponse& res) {
    if (req.bodyLen > 4096) return apiError(res, 413, "Certificates are 4 KB at most.", "ca");
    std::string pem(req.body, req.bodyLen);
    if (pem.find("-----BEGIN CERTIFICATE-----") == std::string::npos ||
        pem.find("-----END CERTIFICATE-----") == std::string::npos)
        return apiError(res, 400, "That is not a PEM certificate.", "ca");
    if (!app.platform.caWrite(pem.data(), pem.size())) return apiError(res, 500, "Couldn't store the certificate.", "ca");
    noContent(res);
}

void deleteCa(App& app, HttpResponse& res) {
    if (!app.platform.caDelete()) return apiError(res, 500, "Couldn't remove the certificate.", "ca");
    if (app.settings.wifi.validateCa) {
        app.settings.wifi.validateCa = false;
        app.markDirty(kGroupWifi);
    }
    noContent(res);
}

// ---------------------------------------------------------------- source

// SourceReport fields shared by GET /api/source and the test result.
void reportJson(JsonObject o, SourceState state, const FetchOutcome* f, const BridgeData* d, uint32_t ageSec) {
    o["state"] = sourceStateName(state);
    const bool answered = f && !f->transportFailed && f->http > 0;
    if (answered) o["http_status"] = f->http;
    else o["http_status"] = nullptr;
    const bool reading = d && d->valid && ageSec != UINT32_MAX;
    if (reading) {
        o["reading_age_sec"] = ageSec;
        o["glucose_mgdl"] = d->mgdl;
    } else {
        o["reading_age_sec"] = nullptr;
        o["glucose_mgdl"] = nullptr;
    }
    const bool upstream = d && (!strcmp(d->upstream, "carelink") || !strcmp(d->upstream, "nightscout"));
    setOrNull(o, "upstream", upstream ? d->upstream : "");
    setOrNull(o, "bridge_alert", d ? d->bridgeAlert : "");
    setOrNull(o, "error", f && state != SourceState::Ok && state != SourceState::Demo ? f->error : "");
}

void getSource(App& app, HttpResponse& res) {
    const uint32_t now = app.platform.millis();
    const SourceRuntime& s = app.source;
    JsonDocument doc;
    JsonObject cfg = doc["config"].to<JsonObject>();
    cfg["kind"] = enumToName(kSourceKindNames, app.settings.source.kind);
    cfg["bridge_url"] = app.settings.source.bridgeUrl;
    cfg["token_set"] = app.settings.source.token[0] != '\0';
    if (s.fetched) doc["last_fetch_age_sec"] = (now - s.lastFetchAtMs) / 1000u;
    else doc["last_fetch_age_sec"] = nullptr;
    const SourceState state = app.sourceState();
    const bool demo = state == SourceState::Demo;
    reportJson(doc.as<JsonObject>(), state, s.fetched && !demo ? &s.last : nullptr, s.haveData ? &s.data : nullptr,
               app.readingAgeSec(now));
    sendJson(res, 200, doc);
}

void putSource(App& app, const HttpRequest& req, HttpResponse& res) {
    JsonDocument doc;
    if (!parseBody(req, doc, res)) return;
    SourceSettings next = app.settings.source;
    if (!enumFromName(kSourceKindNames, doc["kind"] | "", next.kind)) return apiError(res, 400, "Unknown source.", "kind");
    if (next.kind == SourceKind::Bridge) {
        const char* url = doc["bridge_url"] | "";
        if (!url[0]) return apiError(res, 400, "Enter the bridge address.", "bridge_url");
        if (strlen(url) > kUrlMax) return apiError(res, 400, "The address is too long (255 characters max).", "bridge_url");
        if (!isHttpsUrl(url)) return apiError(res, 400, "Use the https:// address of the bridge.", "bridge_url");
        copyStr(next.bridgeUrl, url);
        JsonVariantConst token = doc["token"];
        if (token.is<const char*>() && token.as<const char*>()[0]) {
            if (strlen(token.as<const char*>()) > kTokenMax) return apiError(res, 400, "The token is too long.", "token");
            copyStr(next.token, token.as<const char*>());
        }
        if (!next.token[0]) return apiError(res, 400, "Enter the clock token.", "token");
    }
    const bool changed = next.kind != app.settings.source.kind || strcmp(next.bridgeUrl, app.settings.source.bridgeUrl) ||
                         strcmp(next.token, app.settings.source.token);
    app.settings.source = next;
    if (changed) {
        app.resetSource();
        app.markDirty(kGroupSource);
        app.platform.wakeNetwork();
    }
    getSource(app, res);
}

void postSourceTest(App& app, const HttpRequest& req, HttpResponse& res) {
    JsonDocument doc;
    if (!parseBody(req, doc, res)) return;
    const char* url = doc["bridge_url"] | "";
    if (!url[0]) return apiError(res, 400, "Enter the bridge address.", "bridge_url");
    if (strlen(url) > kUrlMax || !isHttpsUrl(url)) return apiError(res, 400, "Use the https:// address of the bridge.", "bridge_url");
    const char* token = doc["token"] | app.settings.source.token;
    if (!token[0]) return apiError(res, 400, "Enter the clock token.", "token");
    if (strlen(token) > kTokenMax) return apiError(res, 400, "The token is too long.", "token");
    if (app.test.state == SourceTest::Requested || app.test.state == SourceTest::Running)
        return apiError(res, 409, "A test is already running.");
    copyStr(app.test.url, url);
    copyStr(app.test.token, token);
    app.test.state = SourceTest::Requested;
    app.platform.wakeNetwork();
    accepted(res);
}

void getSourceTest(App& app, HttpResponse& res) {
    JsonDocument doc;
    const SourceTest& t = app.test;
    doc["state"] = t.state == SourceTest::Idle ? "idle" : t.state == SourceTest::Done ? "done" : "running";
    if (t.state == SourceTest::Done) {
        JsonObject r = doc["result"].to<JsonObject>();
        reportJson(r, t.result, &t.outcome, &t.data, t.readingAgeSec);
        if (t.outcome.transportFailed) r["ms"] = nullptr;
        else r["ms"] = t.outcome.ms;
    } else {
        doc["result"] = nullptr;
    }
    sendJson(res, 200, doc);
}

// ---------------------------------------------------------------- display

void getDisplay(App& app, HttpResponse& res) {
    JsonDocument doc;
    toJson(app.settings.display, doc.to<JsonObject>());
    sendJson(res, 200, doc);
}

void patchDisplay(App& app, const HttpRequest& req, HttpResponse& res) {
    JsonDocument doc;
    if (!parseBody(req, doc, res)) return;
    FieldError err;
    if (!patch(app.settings.display, doc.as<JsonObjectConst>(), err)) return apiError(res, 400, err.message, err.field);
    app.markDirty(kGroupDisplay);
    getDisplay(app, res);
}

void getFrame(App& app, const HttpRequest& req, HttpResponse& res) {
    Mirror& m = app.mirror;
    m.requestedAt = app.platform.millis();
    if (m.requestedAt == 0) m.requestedAt = 1;
    res.screen = screenName(m.screen);
    if (m.lowLocked) res.hold = "low";
    if (!m.ready) return noContent(res, 204);
    char etag[24];
    snprintf(etag, sizeof(etag), "\"%lu\"", (unsigned long)m.sequence);
    res.etag = etag;
    if (req.ifNoneMatch && strcmp(req.ifNoneMatch, etag) == 0) return noContent(res, 304);
    res.status = 200;
    res.contentType = "application/octet-stream";
    res.body.assign(reinterpret_cast<const char*>(m.frame.rgb), sizeof(m.frame.rgb));
}

// What each pet looks like, for the dashboard's picker: the in-range idle pose
// (two frames), drawn by the same code as the matrix and cropped to the pet.
void getPets(App& app, HttpResponse& res) {
    constexpr uint32_t kFrameMs = 650;  // pets.cpp: the in-range pose alternates this often
    const PresetColors& c = presetColors(app.settings.display.glucose.colors);
    JsonDocument doc;
    doc["frame_ms"] = kFrameMs;
    JsonObject colors = doc["colors"].to<JsonObject>();
    JsonArray pets = doc["pets"].to<JsonArray>();
    for (const auto& n : kPetNames) {
        char px[2][8][32];
        int left = 32, right = -1;
        for (int f = 0; f < 2; ++f) {
            petFrame(n.value, f * kFrameMs, false, false, PetStyle::PetOnly, 0, px[f]);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 32; ++x)
                    if (px[f][y][x] != '.') {
                        if (x < left) left = x;
                        if (x > right) right = x;
                    }
        }
        JsonObject pet = pets.add<JsonObject>();
        pet["id"] = n.name;
        JsonArray frames = pet["frames"].to<JsonArray>();
        for (int f = 0; f < 2; ++f) {
            JsonArray rows = frames.add<JsonArray>();
            for (int y = 0; y < 8; ++y) {
                char row[33] = "";
                int len = 0;
                for (int x = left; x <= right; ++x) {
                    const char k = px[f][y][x];
                    row[len++] = k;
                    if (k != '.') {
                        char key[2] = {k, '\0'};  // char*, not const char*: ArduinoJson copies it
                        if (!colors[key].is<const char*>()) {
                            char hex[12];
                            snprintf(hex, sizeof(hex), "#%06lx", (unsigned long)petPixelColor(k, c.low, c.inRange, c.high));
                            colors[key] = hex;
                        }
                    }
                }
                row[len] = '\0';
                rows.add(row);  // copied by ArduinoJson: row is a local buffer
            }
        }
    }
    sendJson(res, 200, doc);
}

void postPreview(App& app, const HttpRequest& req, HttpResponse& res) {
    JsonDocument doc;
    if (!parseBody(req, doc, res)) return;
    const char* screen = doc["screen"] | "";
    Preview p{};
    if (!strcmp(screen, "pet")) {
        p.kind = Preview::Pet;
        if (!enumFromName(kPetNames, doc["pet"] | "", p.pet)) return apiError(res, 400, "Unknown pet.", "pet");
        const char* range = doc["range"] | "in_range";
        if (!strcmp(range, "low")) p.range = 1;
        else if (!strcmp(range, "high")) p.range = 2;
        else if (!strcmp(range, "in_range")) p.range = 0;
        else return apiError(res, 400, "Unknown range.", "range");
    } else if (!strcmp(screen, "alerts")) {
        p.kind = Preview::Alerts;
    } else if (!strcmp(screen, "glucose")) {
        p.kind = Preview::Glucose;
        p.mgdl = doc["mgdl"] | 0;
        if (p.mgdl < kGlucoseMin || p.mgdl > kGlucoseMax) return apiError(res, 400, "Use 40 to 400 mg/dL.", "mgdl");
    } else {
        return apiError(res, 400, "Unknown screen.", "screen");
    }
    if (app.mirror.lowLocked)
        return apiError(res, 409, "The clock stays on the low reading until it's back above Low.", "low_lock");
    if (app.mirror.urgent) return apiError(res, 409, "Not while the reading is urgent.", "urgent");
    app.commands.preview = true;
    app.commands.p = p;
    accepted(res);
}

void postClear(App& app, HttpResponse& res) {
    if (app.clear.state != AlertsClear::Requested && app.clear.state != AlertsClear::Running) {
        app.clear.state = AlertsClear::Requested;
        app.clear.error[0] = '\0';
        app.platform.wakeNetwork();
    }
    accepted(res);
}

void getClear(App& app, HttpResponse& res) {
    JsonDocument doc;
    const AlertsClear& c = app.clear;
    doc["state"] = c.state == AlertsClear::Idle ? "idle"
                 : c.state == AlertsClear::Done ? "done"
                 : c.state == AlertsClear::Failed ? "failed"
                                                  : "running";
    setOrNull(doc.as<JsonObject>(), "error", c.state == AlertsClear::Failed ? c.error : "");
    sendJson(res, 200, doc);
}

// ---------------------------------------------------------------- schedule

void getSchedule(App& app, HttpResponse& res) {
    LocalTime t{};
    TimeSource src;
    app.platform.localTime(t, src);
    const ScheduleNow n = scheduleNow(app.settings.schedule, t.valid, t.weekday, t.hour * 60 + t.minute);
    JsonDocument doc;
    toJson(app.settings.schedule, doc["settings"].to<JsonObject>());
    JsonObject now = doc["now"].to<JsonObject>();
    now["time_valid"] = n.timeValid;
    if (n.timeValid) {
        now["weekday"] = kWeekdays[n.weekday];
        now["minute"] = n.minute;
    } else {
        now["weekday"] = nullptr;
        now["minute"] = nullptr;
    }
    now["state"] = n.state == DayState::Awake ? "awake" : n.state == DayState::Night ? "night" : "off";
    if (n.hasNext) {
        JsonObject nc = now["next_change"].to<JsonObject>();
        nc["weekday"] = kWeekdays[n.nextWeekday];
        nc["minute"] = n.nextMinute;
        nc["to"] = n.nextToAwake ? "awake" : "night";
    } else {
        now["next_change"] = nullptr;
    }
    sendJson(res, 200, doc);
}

void patchSchedule(App& app, const HttpRequest& req, HttpResponse& res) {
    JsonDocument doc;
    if (!parseBody(req, doc, res)) return;
    FieldError err;
    if (!patch(app.settings.schedule, doc.as<JsonObjectConst>(), err)) return apiError(res, 400, err.message, err.field);
    app.markDirty(kGroupSchedule);
    getSchedule(app, res);
}

// ---------------------------------------------------------------- device

void getDevice(App& app, HttpResponse& res) {
    JsonDocument doc;
    doc["name"] = app.name();
    WifiView w;
    app.platform.wifiView(w);
    setOrNull(doc.as<JsonObject>(), "local_name", w.localName);
    doc["password_set"] = app.settings.device.password[0] != '\0';

    JsonObject fw = doc["firmware"].to<JsonObject>();
    fw["version"] = app.platform.firmwareVersion();
    fw["hardware"] = app.platform.hardware();
    char from[24];
    if (app.platform.rolledBack(from, sizeof(from))) {
        JsonObject rb = fw["rolled_back"].to<JsonObject>();
        rb["from_version"] = from;
        rb["reason"] = "The new firmware didn't pass its first-start checks.";
    } else {
        fw["rolled_back"] = nullptr;
    }

    LocalTime t{};
    TimeSource src;
    app.platform.localTime(t, src);
    JsonObject time = doc["time"].to<JsonObject>();
    time["zone"] = app.settings.device.zone;
    time["synced"] = src == TimeSource::Ntp || src == TimeSource::Http;
    time["source"] = src == TimeSource::Ntp ? "ntp" : src == TimeSource::Rtc ? "rtc" : src == TimeSource::Http ? "http" : "none";
    if (t.valid) {
        char local[24];
        snprintf(local, sizeof(local), "%04d-%02d-%02dT%02d:%02d", t.year, t.month, t.day, t.hour, t.minute);
        time["local"] = local;
    } else {
        time["local"] = nullptr;
    }

    Diagnostics g{};
    app.platform.diagnostics(g);
    JsonObject d = doc["diagnostics"].to<JsonObject>();
    d["uptime_sec"] = g.uptimeSec;
    d["reset_reason"] = g.resetReason ? g.resetReason : "other";
    d["free_heap"] = g.freeHeap;
    d["min_free_heap"] = g.minFreeHeap;
    d["largest_free_block"] = g.largestBlock;
    d["light"] = g.light;
    d["brightness_now"] = g.brightnessNow;
    JsonObject b = d["buttons"].to<JsonObject>();
    b["left"] = g.left;
    b["middle"] = g.middle;
    b["right"] = g.right;
    const bool fetchFailed = app.source.fetched && app.source.failures > 0;
    setOrNull(d, "last_fetch_error", fetchFailed ? app.source.last.error : "");
    sendJson(res, 200, doc);
}

void patchDevice(App& app, const HttpRequest& req, HttpResponse& res) {
    JsonDocument doc;
    if (!parseBody(req, doc, res)) return;
    for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
        if (strcmp(kv.key().c_str(), "name") && strcmp(kv.key().c_str(), "time_zone"))
            return apiError(res, 400, "Unknown setting.", kv.key().c_str());
    }
    DeviceSettings next = app.settings.device;
    bool renamed = false, rezoned = false;
    JsonVariantConst name = doc["name"];
    if (!name.isNull()) {
        if (!name.is<const char*>()) return apiError(res, 400, "Enter a name.", "name");
        // Trim spaces around it.
        const char* s = name.as<const char*>();
        while (*s == ' ') ++s;
        char buf[kNameMax + 8];
        snprintf(buf, sizeof(buf), "%s", s);
        size_t n = strlen(buf);
        while (n && buf[n - 1] == ' ') buf[--n] = '\0';
        if (!n) return apiError(res, 400, "Enter a name.", "name");
        if (strlen(s) > kNameMax + 4 || n > kNameMax) return apiError(res, 400, "Names are 32 characters at most.", "name");
        if (!clock_name_valid(buf)) return apiError(res, 400, "Use at least one letter or digit.", "name");
        renamed = strcmp(buf, app.name()) != 0;
        copyStr(next.name, buf);
    }
    JsonVariantConst tz = doc["time_zone"];
    if (!tz.isNull()) {
        const char* zone = tz["zone"] | "";
        const char* posix = tz["posix"] | "";
        if (!zone[0] || !posix[0]) return apiError(res, 400, "Pick a time zone.", "time_zone");
        if (strlen(zone) >= sizeof(next.zone) || strlen(posix) >= sizeof(next.posix))
            return apiError(res, 400, "That time zone is too long.", "time_zone");
        rezoned = strcmp(posix, next.posix) != 0 || strcmp(zone, next.zone) != 0;
        copyStr(next.zone, zone);
        copyStr(next.posix, posix);
    }
    app.settings.device = next;
    if (renamed || rezoned) app.markDirty(kGroupDevice);
    if (rezoned) app.platform.applyTimezone(next.posix);
    if (renamed) app.platform.nameChanged();
    getDevice(app, res);
}

void putPassword(App& app, const HttpRequest& req, HttpResponse& res) {
    JsonDocument doc;
    if (!parseBody(req, doc, res)) return;
    JsonVariantConst p = doc["password"];
    if (!p.is<const char*>()) return apiError(res, 400, "Missing password.", "password");
    const char* pw = p.as<const char*>();
    const size_t n = strlen(pw);
    if (n && (n < kPasswordMin || n > kPasswordMax)) return apiError(res, 400, "Use 4 to 64 characters.", "password");
    copyStr(app.settings.device.password, pw);
    app.markDirty(kGroupDevice);
    noContent(res);
}

void postFactoryReset(App& app, const HttpRequest& req, HttpResponse& res) {
    JsonDocument doc;
    if (!parseBody(req, doc, res)) return;
    if (strcmp(doc["confirm"] | "", "erase") != 0) return apiError(res, 400, "Send { \"confirm\": \"erase\" }.", "confirm");
    app.platform.factoryResetSoon();
    accepted(res);
}

void getUpdate(App& app, HttpResponse& res) {
    UpdateView u;
    app.platform.updateView(u);
    JsonDocument doc;
    doc["state"] = u.state == UpdateView::Receiving ? "receiving"
                 : u.state == UpdateView::Restarting ? "restarting"
                 : u.state == UpdateView::Failed ? "failed"
                                                 : "idle";
    doc["received"] = u.received;
    setOrNull(doc.as<JsonObject>(), "error", u.error);
    sendJson(res, 200, doc);
}

} // namespace

void apiError(HttpResponse& res, int status, const char* message, const char* field) {
    JsonDocument doc;
    doc["error"] = message;
    if (field && field[0]) doc["field"] = field;
    sendJson(res, status, doc);
}

bool handleApi(App& app, const HttpRequest& req, HttpResponse& res) {
    if (strncmp(req.path, "/api/", 5) != 0) return false;
    Locked lock(app.mutex);

    if (is(req, "GET", "/api/status")) return getStatus(app, res), true;

    if (is(req, "GET", "/api/wifi")) return getWifi(app, res), true;
    if (is(req, "GET", "/api/wifi/networks")) return getNetworks(app, res), true;
    if (is(req, "POST", "/api/wifi/networks/scan")) {
        if (!app.platform.wifiStartScan()) return apiError(res, 409, "A scan is already running."), true;
        return accepted(res), true;
    }
    if (is(req, "POST", "/api/wifi/join")) return postJoin(app, req, res), true;
    if (is(req, "POST", "/api/wifi/checks")) return app.platform.netRecheck(), accepted(res), true;
    if (is(req, "PUT", "/api/wifi/ca")) return putCa(app, req, res), true;
    if (is(req, "DELETE", "/api/wifi/ca")) return deleteCa(app, res), true;

    if (is(req, "GET", "/api/source")) return getSource(app, res), true;
    if (is(req, "PUT", "/api/source")) return putSource(app, req, res), true;
    if (is(req, "POST", "/api/source/test")) return postSourceTest(app, req, res), true;
    if (is(req, "GET", "/api/source/test")) return getSourceTest(app, res), true;

    if (is(req, "GET", "/api/display/settings")) return getDisplay(app, res), true;
    if (is(req, "PATCH", "/api/display/settings")) return patchDisplay(app, req, res), true;
    if (is(req, "GET", "/api/display/frame")) return getFrame(app, req, res), true;
    if (is(req, "GET", "/api/display/pets")) return getPets(app, res), true;
    if (is(req, "POST", "/api/display/next") || is(req, "POST", "/api/display/prev")) {
        if (app.mirror.lowLocked)
            return apiError(res, 409, "The clock stays on the low reading until it's back above Low.", "low_lock"), true;
        (strcmp(req.path, "/api/display/next") == 0 ? app.commands.next : app.commands.prev) = true;
        return noContent(res), true;
    }
    if (is(req, "POST", "/api/display/preview")) return postPreview(app, req, res), true;

    if (is(req, "POST", "/api/alerts/clear")) return postClear(app, res), true;
    if (is(req, "GET", "/api/alerts/clear")) return getClear(app, res), true;

    if (is(req, "GET", "/api/schedule")) return getSchedule(app, res), true;
    if (is(req, "PATCH", "/api/schedule")) return patchSchedule(app, req, res), true;

    if (is(req, "GET", "/api/device")) return getDevice(app, res), true;
    if (is(req, "PATCH", "/api/device")) return patchDevice(app, req, res), true;
    if (is(req, "PUT", "/api/device/password")) return putPassword(app, req, res), true;
    if (is(req, "POST", "/api/device/restart")) return app.platform.restartSoon(), accepted(res), true;
    if (is(req, "POST", "/api/device/factory-reset")) return postFactoryReset(app, req, res), true;
    if (is(req, "GET", "/api/update")) return getUpdate(app, res), true;

    char msg[96];
    snprintf(msg, sizeof(msg), "No such endpoint: %s %s", req.method, req.path);
    apiError(res, 404, msg);
    return true;
}

} // namespace pk
