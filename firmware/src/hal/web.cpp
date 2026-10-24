#include "web.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "core/api.h"
#include "ota.h"
#include "web_assets.h"
#include "wifi.h"

namespace hal {

namespace {

using pk::Locked;

const char* const kUser = "pumpkin";
constexpr size_t kMaxBody = 8192;

AsyncWebServer server(80);
pk::App* app = nullptr;
AsyncWebServerRequest* uploader = nullptr;  // the request streaming an update

// The OS "am I online?" probes. On the setup hotspot they get a small page
// pointing at the real setup page: the captive sheet phones open can't stay
// open long enough to join a school network.
const char* const kProbes[] = {"/hotspot-detect.html", "/library/test/success.html", "/generate_204", "/gen_204",
                               "/ncsi.txt",            "/connecttest.txt",           "/redirect",     "/success.txt",
                               "/canonical.html"};

const char kSignpost[] PROGMEM =
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Pumpkin setup</title><style>"
    "body{margin:0;padding:2rem 1.25rem;font:16px/1.55 system-ui,sans-serif;background:#0f1115;color:#e8eaed}"
    "h1{font-size:1.35rem;margin:0 0 .75rem}p{margin:0 0 1rem;color:#b6bcc6}"
    "a{display:block;margin:1.25rem 0;padding:1rem;border-radius:12px;background:#1b1f27;border:1px solid #2b313c;"
    "font-size:1.5rem;font-weight:600;text-align:center;color:#ff9a3c;text-decoration:none}"
    "@media (prefers-color-scheme:light){body{background:#f6f7f9;color:#16181d}p{color:#4c525c}"
    "a{background:#fff;border-color:#dcdfe4;color:#c25e00}}</style></head><body>"
    "<h1>Finish setup in your browser</h1>"
    "<p>This little window closes too soon to join a school network. Stay on the clock's "
    "<b>Pumpkin-Setup</b> network and open this address in Safari or Chrome:</p>"
    "<a href=\"http://192.168.4.1/\">http://192.168.4.1</a></body></html>";

bool isProbe(const String& url) {
    for (const char* p : kProbes)
        if (url == p) return true;
    return false;
}

bool viaHotspot(AsyncWebServerRequest* r) {
    AsyncClient* c = r->client();
    return c && wifiOnAp((uint32_t)c->localIP());
}

bool authorized(AsyncWebServerRequest* r) {
    char pw[pk::kPasswordMax + 1];
    {
        Locked l(app->mutex);
        pk::copyStr(pw, app->settings.device.password);
    }
    return !pw[0] || r->authenticate(kUser, pw);
}

void sendSignpost(AsyncWebServerRequest* r) {
    AsyncWebServerResponse* res = r->beginResponse(200, "text/html; charset=utf-8", (const uint8_t*)kSignpost, strlen_P(kSignpost));
    res->addHeader("Cache-Control", "no-store");
    r->send(res);
}

void sendAsset(AsyncWebServerRequest* r, const WebAsset* a) {
    if (r->hasHeader("If-None-Match") && r->header("If-None-Match") == a->etag) {
        r->send(304);
        return;
    }
    AsyncWebServerResponse* res = r->beginResponse(200, a->mime, a->data, a->size);
    if (a->gzip) res->addHeader("Content-Encoding", "gzip");
    res->addHeader("Cache-Control", "no-cache");
    res->addHeader("ETag", a->etag);
    r->send(res);
}

const char* methodName(AsyncWebServerRequest* r) {
    switch (r->method()) {
        case HTTP_GET: return "GET";
        case HTTP_POST: return "POST";
        case HTTP_PUT: return "PUT";
        case HTTP_PATCH: return "PATCH";
        case HTTP_DELETE: return "DELETE";
        default: return "OTHER";
    }
}

void sendApi(AsyncWebServerRequest* r, const pk::HttpResponse& out) {
    AsyncWebServerResponse* res;
    if (out.status == 204 || out.status == 304 || out.body.empty()) {
        res = r->beginResponse(out.status);
    } else {
        AsyncResponseStream* s = r->beginResponseStream(out.contentType, out.body.size() + 16);
        s->setCode(out.status);
        s->write(reinterpret_cast<const uint8_t*>(out.body.data()), out.body.size());
        res = s;
    }
    if (!out.etag.empty()) res->addHeader("ETag", out.etag.c_str());
    if (out.screen) res->addHeader("X-Screen", out.screen);
    if (out.hold) res->addHeader("X-Hold", out.hold);
    res->addHeader("Cache-Control", "no-store");
    r->send(res);
}

void sendError(AsyncWebServerRequest* r, int status, const char* message, const char* field = nullptr) {
    pk::HttpResponse out;
    pk::apiError(out, status, message, field);
    sendApi(r, out);
}

// Request bodies are collected here (8 KB at most) and handled once complete.
struct Body {
    size_t len;
    bool tooBig;
    char data[1];
};

class ApiHandler : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest* r) const override {
        return r->url().startsWith("/api/") && !(r->method() == HTTP_POST && r->url() == "/api/update");
    }
    bool isRequestHandlerTrivial() const override { return false; }

    void handleBody(AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) override {
        // Nothing acts on a body before handleRequest(), which runs after the
        // password check: collecting it unchecked is safe.
        if (index == 0) {
            const bool big = total > kMaxBody;
            Body* b = (Body*)malloc(sizeof(Body) + (big ? 0 : total));
            if (!b) return;
            b->len = 0;
            b->tooBig = big;
            r->_tempObject = b;
        }
        Body* b = (Body*)r->_tempObject;
        if (!b || b->tooBig || b->len + len > kMaxBody) {
            if (b) b->tooBig = true;
            return;
        }
        memcpy(b->data + b->len, data, len);
        b->len += len;
        b->data[b->len] = '\0';
    }

    void handleRequest(AsyncWebServerRequest* r) override {
        Body* b = (Body*)r->_tempObject;
        if (b && b->tooBig) return sendError(r, 413, "That's too much data.");
        const String url = r->url();
        pk::HttpRequest req;
        req.method = methodName(r);
        req.path = url.c_str();
        req.body = b ? b->data : "";
        req.bodyLen = b ? b->len : 0;
        String inm;
        if (r->hasHeader("If-None-Match")) inm = r->header("If-None-Match");
        req.ifNoneMatch = inm.c_str();
        pk::HttpResponse out;
        if (!pk::handleApi(*app, req, out)) return sendError(r, 404, "No such API.");
        sendApi(r, out);
    }
};

// POST /api/update: the .bin as the raw body, checked and written while it streams.
class UpdateHandler : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest* r) const override {
        return r->method() == HTTP_POST && r->url() == "/api/update";
    }
    bool isRequestHandlerTrivial() const override { return false; }

    void handleBody(AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) override {
        // The data arrives before the password middleware runs: check here and
        // drop it when it's wrong (the middleware then answers 401).
        if (index == 0) {
            if (!authorized(r) || uploader) return;
            if (!otaUploadBegin(total)) return;
            uploader = r;
            r->onDisconnect([]() {
                otaUploadAbort();  // the browser went away mid-upload
                uploader = nullptr;
            });
        }
        if (r != uploader) return;
        if (!otaUploadWrite(data, len)) return;
        if (index + len == total) otaUploadEnd();
    }

    void handleRequest(AsyncWebServerRequest* r) override {
        pk::UpdateView v;
        otaView(v);
        if (r != uploader) {
            if (v.state == pk::UpdateView::Receiving) return sendError(r, 409, "An update is already running.", "busy");
            if (!r->contentLength()) return sendError(r, 400, "No file came through.", "truncated");
        }
        uploader = nullptr;
        if (v.state == pk::UpdateView::Receiving) otaUploadAbort(), otaView(v);
        if (v.state == pk::UpdateView::Restarting) {
            r->send(202);
            app->platform.restartSoon();
            return;
        }
        static const struct {
            const char* code;
            const char* message;
        } kMessages[] = {
            {"not_firmware", "That file isn't clock firmware."},
            {"wrong_chip", "That firmware is for a different chip."},
            {"not_pumpkin", "That's ESP32 firmware, but not Pumpkin."},
            {"wrong_hardware", "That Pumpkin firmware is for different hardware."},
            {"truncated", "The file is incomplete."},
            {"busy", "The clock is still checking its last update. Try again in a minute."},
            {"write_failed", "The clock couldn't write the update."},
        };
        const char* message = "The update didn't work.";
        for (const auto& m : kMessages)
            if (!strcmp(m.code, v.error)) message = m.message;
        sendError(r, 400, message, v.error[0] ? v.error : "write_failed");
    }
};

} // namespace

void webBegin(pk::App& a) {
    app = &a;

    // Everything asks for the password, except the hotspot probes.
    server.addMiddleware([](AsyncWebServerRequest* r, ArMiddlewareNext next) {
        if (isProbe(r->url()) || authorized(r)) return next();
        r->requestAuthentication(AsyncAuthType::AUTH_BASIC, "Pumpkin");
    });

    server.addHandler(new UpdateHandler());
    server.addHandler(new ApiHandler());

    for (const char* p : kProbes) {
        server.on(p, HTTP_GET, [](AsyncWebServerRequest* r) {
            if (viaHotspot(r)) return sendSignpost(r);
            r->send(404, "text/plain", "Not found");
        });
    }

    // "/": the setup page over the hotspot, the dashboard otherwise.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
        const WebAsset* page = findWebAsset(viaHotspot(r) ? "/setup.html" : "/index.html");
        if (!page) return r->send(500, "text/plain", "The dashboard is missing from this build.");
        sendAsset(r, page);
    });

    server.onNotFound([](AsyncWebServerRequest* r) {
        if (r->method() == HTTP_GET) {
            const WebAsset* a = findWebAsset(r->url().c_str());
            if (a) return sendAsset(r, a);
            // Wildcard DNS sends every name here while setting up.
            if (viaHotspot(r)) return sendSignpost(r);
        }
        r->send(404, "text/plain", "Not found");
    });

    server.begin();
    Serial.println("[web] Up on port 80");
}

} // namespace hal
