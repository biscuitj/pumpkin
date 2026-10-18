#include "http_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <thread>

namespace {

const char* reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 404: return "Not Found";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        default: return status < 400 ? "OK" : "Error";
    }
}

bool readRequest(int fd, SimRequest& req) {
    std::string data;
    char buf[4096];
    size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        data.append(buf, (size_t)n);
        headerEnd = data.find("\r\n\r\n");
        if (data.size() > 64 * 1024 && headerEnd == std::string::npos) return false;
    }
    const std::string head = data.substr(0, headerEnd);
    size_t lineEnd = head.find("\r\n");
    const std::string first = head.substr(0, lineEnd);
    const size_t sp1 = first.find(' '), sp2 = first.rfind(' ');
    if (sp1 == std::string::npos || sp2 == sp1) return false;
    req.method = first.substr(0, sp1);
    std::string target = first.substr(sp1 + 1, sp2 - sp1 - 1);
    const size_t q = target.find('?');
    req.path = target.substr(0, q);
    if (q != std::string::npos) req.query = target.substr(q + 1);
    size_t pos = lineEnd == std::string::npos ? head.size() : lineEnd + 2;
    while (pos < head.size()) {
        size_t end = head.find("\r\n", pos);
        if (end == std::string::npos) end = head.size();
        const std::string line = head.substr(pos, end - pos);
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = line.substr(0, colon);
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            std::string value = line.substr(colon + 1);
            value.erase(0, value.find_first_not_of(' '));
            req.headers[name] = value;
        }
        pos = end + 2;
    }
    const size_t length = req.headers.count("content-length") ? std::stoul(req.headers["content-length"]) : 0;
    req.body = data.substr(headerEnd + 4);
    while (req.body.size() < length) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        req.body.append(buf, (size_t)n);
    }
    req.body.resize(length);
    return true;
}

void handleConnection(int fd, SimHandler handler) {
    SimRequest req;
    if (readRequest(fd, req)) {
        SimResponse res;
        handler(req, res);
        if (!res.drop) {
            std::string out = "HTTP/1.1 " + std::to_string(res.status) + " " + reason(res.status) + "\r\n";
            const bool empty = res.status == 204 || res.status == 304;
            if (!empty) {
                out += "Content-Type: " + res.contentType + "\r\n";
                out += "Content-Length: " + std::to_string(res.body.size()) + "\r\n";
            }
            for (const auto& h : res.headers) out += h.first + ": " + h.second + "\r\n";
            out += "Connection: close\r\n\r\n";
            if (!empty) out += res.body;
            size_t sent = 0;
            while (sent < out.size()) {
                ssize_t n = send(fd, out.data() + sent, out.size() - sent, MSG_NOSIGNAL);
                if (n <= 0) break;
                sent += (size_t)n;
            }
        }
    }
    close(fd);
}

} // namespace

void serveHttp(int port, SimHandler handler) {
    signal(SIGPIPE, SIG_IGN);
    int server = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(server, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(server, 32) != 0) {
        perror("sim: listen");
        exit(1);
    }
    for (;;) {
        int fd = accept(server, nullptr, nullptr);
        if (fd < 0) continue;
        std::thread(handleConnection, fd, handler).detach();
    }
}
