// The HTTP API from dashboard/src/contract.ts, independent of any web server.
// hal/web.cpp feeds it requests from ESPAsyncWebServer; host/sim.cpp from a
// small socket server, so the dashboard can be tested against this exact code.
// POST /api/update streams a firmware image and is handled by the server side.
#pragma once

#include <string>

#include "app.h"

namespace pk {

struct HttpRequest {
    const char* method = "GET";
    const char* path = "/";
    const char* body = "";
    size_t bodyLen = 0;
    const char* ifNoneMatch = "";
};

struct HttpResponse {
    int status = 200;
    const char* contentType = "application/json";
    std::string body;
    std::string etag;
    const char* screen = nullptr;  // X-Screen header for the mirror
    const char* hold = nullptr;    // X-Hold header: "low" during the low lock
};

// Fills `res`; returns false when the path isn't an API route at all.
bool handleApi(App& app, const HttpRequest& req, HttpResponse& res);

// Error body helper: { "error": message, "field": field }.
void apiError(HttpResponse& res, int status, const char* message, const char* field = nullptr);

} // namespace pk
