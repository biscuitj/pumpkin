// A deliberately small HTTP/1.1 server for the simulator: one thread per
// connection, Connection: close, Content-Length bodies only. Not for production.
#pragma once

#include <functional>
#include <map>
#include <string>

struct SimRequest {
    std::string method, path, query;
    std::map<std::string, std::string> headers;  // lower-case names
    std::string body;
    std::string header(const char* name) const {
        auto it = headers.find(name);
        return it == headers.end() ? std::string() : it->second;
    }
};

struct SimResponse {
    int status = 200;
    std::string contentType = "application/json";
    std::map<std::string, std::string> headers;
    std::string body;
    bool drop = false;  // close without answering (a restarting clock)
};

using SimHandler = std::function<void(const SimRequest&, SimResponse&)>;

// Blocks forever serving on `port`.
void serveHttp(int port, SimHandler handler);
