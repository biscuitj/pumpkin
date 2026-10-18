// The API against the contract: shapes, errors with fields, side effects.
#include "core/api.h"
#include "core/net_jobs.h"
#include "fake_platform.h"
#include "test.h"

using namespace pk;

namespace {

struct Api {
    FakePlatform p;
    App app{p};
    HttpResponse res;
    JsonDocument doc;

    int call(const char* method, const char* path, const char* body = "", const char* etag = "") {
        HttpRequest req;
        req.method = method;
        req.path = path;
        req.body = body;
        req.bodyLen = strlen(body);
        req.ifNoneMatch = etag;
        res = HttpResponse{};
        handleApi(app, req, res);
        doc.clear();
        if (!res.body.empty() && strcmp(res.contentType, "application/json") == 0) deserializeJson(doc, res.body);
        return res.status;
    }
};

struct FakeHttp : HttpTransport {
    int status = 200;
    std::string body = R"({"glucose":142,"trend":"Flat","timestamp":1791555420,"tir":78,"iob":1.2,"next_poll_sec":120,"source":"carelink","bridge_alert":""})";
    int gets = 0, posts = 0;
    std::string lastUrl, lastToken;
    void get(const char* url, const char* token, HttpResult& out) override {
        ++gets;
        lastUrl = url;
        lastToken = token;
        out.status = status;
        out.body = body;
        out.ms = 120;
    }
    void post(const char* url, const char* token, HttpResult& out) override {
        ++posts;
        lastUrl = url;
        out.status = 200;
    }
};

} // namespace

TEST(api_status) {
    Api a;
    CHECK_EQ(a.call("GET", "/api/status"), 200);
    CHECK_STR(a.doc["firmware_version"].as<const char*>(), "1.0.0");
    CHECK_EQ(a.doc["use_mmol"].as<bool>(), false);
}

TEST(api_unknown_route) {
    Api a;
    CHECK_EQ(a.call("GET", "/api/nope"), 404);
    CHECK(a.doc["error"].is<const char*>());
}

TEST(api_display_patch) {
    Api a;
    CHECK_EQ(a.call("PATCH", "/api/display/settings", R"({"glucose":{"unit":"mmol"}})"), 200);
    CHECK_STR(a.doc["glucose"]["unit"].as<const char*>(), "mmol");
    CHECK(a.app.dirty & kGroupDisplay);
    CHECK_EQ(a.call("PATCH", "/api/display/settings", R"({"glucose":{"high":60}})"), 400);
    CHECK_STR(a.doc["field"].as<const char*>(), "glucose.low");
    CHECK_EQ(a.call("PATCH", "/api/display/settings", "not json"), 400);
}

TEST(api_wifi_status_shape) {
    Api a;
    CHECK_EQ(a.call("GET", "/api/wifi"), 200);
    CHECK_STR(a.doc["mode"].as<const char*>(), "connected");
    CHECK_STR(a.doc["local_name"].as<const char*>(), "keuken.local");
    CHECK(a.doc["ap"].isNull());
    CHECK_STR(a.doc["trial"]["state"].as<const char*>(), "idle");
    CHECK(a.doc["trial"]["ssid"].isNull());
    CHECK(a.doc["checks"]["dns"].is<const char*>());
}

TEST(api_wifi_join_validates) {
    Api a;
    CHECK_EQ(a.call("POST", "/api/wifi/join", R"({"ssid":"Thuis","security":"personal","password":"short"})"), 400);
    CHECK_STR(a.doc["field"].as<const char*>(), "password");
    CHECK_EQ(a.call("POST", "/api/wifi/join", R"({"ssid":"School","security":"enterprise","eap":"peap","identity":"","password":"x","anon_identity":"","validate_ca":false})"), 400);
    CHECK_STR(a.doc["field"].as<const char*>(), "identity");
    CHECK_EQ(a.call("POST", "/api/wifi/join", R"({"ssid":"Thuis","security":"personal","password":"longenough"})"), 202);
    CHECK_EQ(a.p.joins, 1);
    a.p.joinOk = false;
    CHECK_EQ(a.call("POST", "/api/wifi/join", R"({"ssid":"Thuis","security":"personal","password":""})"), 409);
}

TEST(api_ca) {
    Api a;
    CHECK_EQ(a.call("PUT", "/api/wifi/ca", "hello"), 400);
    CHECK_EQ(a.call("PUT", "/api/wifi/ca", "-----BEGIN CERTIFICATE-----\nabc\n-----END CERTIFICATE-----\n"), 204);
    CHECK(a.p.ca);
    a.app.settings.wifi.validateCa = true;
    CHECK_EQ(a.call("DELETE", "/api/wifi/ca"), 204);
    CHECK(!a.app.settings.wifi.validateCa);
}

TEST(api_source_save_and_fetch) {
    Api a;
    CHECK_EQ(a.call("GET", "/api/source"), 200);
    CHECK_STR(a.doc["state"].as<const char*>(), "not_configured");
    CHECK_EQ(a.call("PUT", "/api/source", R"({"kind":"bridge","bridge_url":"http://x/clock","token":"t"})"), 400);
    CHECK_STR(a.doc["field"].as<const char*>(), "bridge_url");
    CHECK_EQ(a.call("PUT", "/api/source", R"({"kind":"bridge","bridge_url":"https://b.workers.dev/clock"})"), 400);
    CHECK_STR(a.doc["field"].as<const char*>(), "token");
    CHECK_EQ(a.call("PUT", "/api/source", R"({"kind":"bridge","bridge_url":"https://b.workers.dev/clock","token":"secret"})"), 200);
    CHECK_STR(a.doc["state"].as<const char*>(), "waiting");
    CHECK_EQ(a.doc["config"]["token_set"].as<bool>(), true);
    CHECK(a.doc["config"]["token"].isNull());  // never sent back

    FakeHttp http;
    NetJobs jobs(a.app, http);
    jobs.step(true, 1791555480);
    CHECK_EQ(http.gets, 1);
    CHECK_STR(http.lastToken, "secret");
    CHECK_EQ(a.call("GET", "/api/source"), 200);
    CHECK_STR(a.doc["state"].as<const char*>(), "ok");
    CHECK_EQ(a.doc["glucose_mgdl"].as<int>(), 142);
    CHECK_EQ(a.doc["reading_age_sec"].as<int>(), 60);
    CHECK_STR(a.doc["upstream"].as<const char*>(), "carelink");
    CHECK(a.doc["error"].isNull());

    // Not due again yet (next_poll_sec 120).
    a.p.now += 60000;
    jobs.step(true, 1791555540);
    CHECK_EQ(http.gets, 1);

    // A refused token.
    http.status = 401;
    a.p.now += 61000;
    jobs.step(true, 1791555600);
    a.call("GET", "/api/source");
    CHECK_STR(a.doc["state"].as<const char*>(), "unauthorized");
    CHECK_EQ(a.doc["http_status"].as<int>(), 401);
}

TEST(api_source_test) {
    Api a;
    CHECK_EQ(a.call("POST", "/api/source/test", R"({"bridge_url":"https://b.workers.dev/clock"})"), 400);
    CHECK_STR(a.doc["field"].as<const char*>(), "token");
    CHECK_EQ(a.call("POST", "/api/source/test", R"({"bridge_url":"https://b.workers.dev/clock","token":"t"})"), 202);
    a.call("GET", "/api/source/test");
    CHECK_STR(a.doc["state"].as<const char*>(), "running");
    FakeHttp http;
    http.body = R"({"glucose":90,"timestamp":1791555420,"bridge_alert":"CARELINK LOGIN NEEDED"})";
    NetJobs jobs(a.app, http);
    jobs.step(true, 1791555480);
    a.call("GET", "/api/source/test");
    CHECK_STR(a.doc["state"].as<const char*>(), "done");
    CHECK_STR(a.doc["result"]["state"].as<const char*>(), "login_needed");
    CHECK_STR(a.doc["result"]["bridge_alert"].as<const char*>(), "CARELINK LOGIN NEEDED");
    CHECK_EQ(a.doc["result"]["ms"].as<int>(), 120);
}

TEST(api_alerts_clear) {
    Api a;
    copyStr(a.app.settings.source.bridgeUrl, "https://b.workers.dev/clock");
    copyStr(a.app.settings.source.token, "t");
    CHECK_EQ(a.call("POST", "/api/alerts/clear"), 202);
    FakeHttp http;
    NetJobs jobs(a.app, http);
    jobs.step(true, 1791555480);
    CHECK_EQ(http.posts, 1);
    CHECK_STR(http.lastUrl, "https://b.workers.dev/reminders/clear?id=all");
    a.call("GET", "/api/alerts/clear");
    CHECK_STR(a.doc["state"].as<const char*>(), "done");
}

TEST(api_frame_etag) {
    Api a;
    CHECK_EQ(a.call("GET", "/api/display/frame"), 204);
    CHECK(a.app.mirror.watched(a.p.now));
    a.app.mirror.ready = true;
    a.app.mirror.sequence = 7;
    CHECK_EQ(a.call("GET", "/api/display/frame"), 200);
    CHECK_EQ((int)a.res.body.size(), 768);
    CHECK_STR(a.res.etag, "\"7\"");
    CHECK_EQ(a.call("GET", "/api/display/frame", "", "\"7\""), 304);
}

TEST(api_pet_sprites) {
    Api a;
    CHECK_EQ(a.call("GET", "/api/display/pets"), 200);
    CHECK_EQ((int)a.doc["frame_ms"], 650);
    JsonArrayConst pets = a.doc["pets"].as<JsonArrayConst>();
    CHECK_EQ((int)pets.size(), 8);
    CHECK_STR(pets[0]["id"].as<const char*>(), "patch");
    for (JsonObjectConst p : pets) {
        JsonArrayConst frames = p["frames"].as<JsonArrayConst>();
        CHECK_EQ((int)frames.size(), 2);
        const size_t width = strlen(frames[0][0] | "");
        CHECK(width >= 8 && width <= 14);
        for (JsonArrayConst rows : frames) {
            CHECK_EQ((int)rows.size(), 8);
            for (JsonVariantConst row : rows) {
                const char* r = row.as<const char*>();
                CHECK_EQ(strlen(r), width);
                for (const char* k = r; *k; ++k) {
                    if (*k == '.') continue;
                    const char key[2] = {*k, '\0'};
                    CHECK(a.doc["colors"][key].is<const char*>());
                }
            }
        }
    }
    // The pumpkin's body is its orange.
    CHECK_STR(a.doc["colors"]["O"].as<const char*>(), "#ff9d48");
}

TEST(api_preview_and_buttons) {
    Api a;
    CHECK_EQ(a.call("POST", "/api/display/preview", R"({"screen":"pet","pet":"pip","range":"low"})"), 400);
    CHECK_EQ(a.call("POST", "/api/display/preview", R"({"screen":"pet","pet":"hoot","range":"low"})"), 202);
    CHECK(a.app.commands.preview);
    CHECK(a.app.commands.p.pet == PetId::Hoot);
    CHECK_EQ(a.call("POST", "/api/display/next"), 204);
    CHECK(a.app.commands.next);
}

TEST(api_schedule) {
    Api a;
    CHECK_EQ(a.call("PATCH", "/api/schedule", R"({"enabled":true,"awake":{"sat":null,"sun":null}})"), 200);
    CHECK(a.doc["settings"]["awake"]["sat"].isNull());
    CHECK_STR(a.doc["now"]["weekday"].as<const char*>(), "fri");
    CHECK_STR(a.doc["now"]["state"].as<const char*>(), "awake");
    CHECK_STR(a.doc["now"]["next_change"]["weekday"].as<const char*>(), "fri");
    CHECK_EQ(a.doc["now"]["next_change"]["minute"].as<int>(), 21 * 60);
    CHECK_EQ(a.call("PATCH", "/api/schedule", R"({"night_brightness":0})"), 400);
    CHECK_STR(a.doc["field"].as<const char*>(), "night_brightness");
}

TEST(api_device) {
    Api a;
    CHECK_EQ(a.call("GET", "/api/device"), 200);
    CHECK_STR(a.doc["name"].as<const char*>(), "pumpkin-e5f6");
    CHECK_STR(a.doc["time"]["local"].as<const char*>(), "2026-10-17T15:00");
    CHECK_EQ(a.call("PATCH", "/api/device", R"({"name":"  Klas 1B  "})"), 200);
    CHECK_STR(a.doc["name"].as<const char*>(), "Klas 1B");
    CHECK_EQ(a.p.renames, 1);
    CHECK_EQ(a.call("PATCH", "/api/device", R"({"name":"!!!"})"), 400);
    CHECK_EQ(a.call("PATCH", "/api/device", R"({"time_zone":{"zone":"Europe/London","posix":"GMT0BST,M3.5.0/1,M10.5.0"}})"), 200);
    CHECK_STR(a.p.posix, "GMT0BST,M3.5.0/1,M10.5.0");
    CHECK_EQ(a.call("PUT", "/api/device/password", R"({"password":"abc"})"), 400);
    CHECK_EQ(a.call("PUT", "/api/device/password", R"({"password":"pompoen"})"), 204);
    a.call("GET", "/api/device");
    CHECK_EQ(a.doc["password_set"].as<bool>(), true);
    CHECK_EQ(a.call("POST", "/api/device/factory-reset", R"({})"), 400);
    CHECK_EQ(a.call("POST", "/api/device/factory-reset", R"({"confirm":"erase"})"), 202);
    CHECK_EQ(a.p.resets, 1);
}
