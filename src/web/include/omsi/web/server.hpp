// SPDX-License-Identifier: MIT
//
// A very small HTTP/1.1 server for the launcher's web front end.
//
// It binds the loopback interface only, serves a handful of JSON endpoints and a few
// static files, and nothing else. There is no routing framework and no template engine:
// the front end is plain HTML, CSS and JavaScript served from disk.
//
// Why a server at all rather than embedding a browser: the parsers stay free of any
// UI dependency and stay testable, and the same server can back a WebView2 window later
// without the front end knowing the difference.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// SOCKET is UINT_PTR (64-bit), so it must not be stored in an int: on x64 that silently
// truncates a handle. This project is Windows-only, so the header can say so outright.
#include <winsock2.h>

namespace omsi::web {

// What a route produced.
struct Response {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
    // Set when body is the exact bytes to send, rather than a UTF-8 string.
    bool binary = false;
};

// A request as far as the front end is concerned.
struct Request {
    std::string method;
    std::string path;   // without the query string
    std::string query;  // without the leading '?'
    std::map<std::string, std::string> headers;  // keys lower-cased

    [[nodiscard]] std::string header(std::string_view name) const;
};

// Serve one request. Returns false for an unhandled path, which becomes a 404.
using Handler = std::function<bool(const Request&, Response&)>;

class Server {
public:
    Server() = default;
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Serves files from `webRoot` (index.html for "/", plus .html/.css/.js/.svg/.png).
    void serveFiles(const std::string& webRoot);

    // Handles GET /api/... before the static files, so a route always wins over a file.
    void route(std::string prefix, Handler handler);

    // Binds 127.0.0.1 on `port`; 0 asks the system for a free one.
    // Returns false and fills `error` when the socket cannot be created.
    bool listen(std::uint16_t port, std::string& error);

    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

    // The address to open, e.g. "http://127.0.0.1:53211/".
    [[nodiscard]] std::string url() const;

    // Runs until stop() is called. It blocks, so a caller that wants to keep going runs it
    // on a thread of its own; the thread doing that is the caller's, which is what keeps
    // stop() free of a race on an internal std::thread.
    void serve();

    // Asks serve() to return; safe from another thread.
    void stop();

private:
    // A connection worker. The finished flag lets the accept loop reap the ones that are
    // done without blocking on the ones that are not.
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> finished;
    };

    void handleConnection(SOCKET client);

    std::vector<std::pair<std::string, Handler>> routes_;
    std::string webRoot_;
    std::uint16_t port_ = 0;
    SOCKET listener_ = INVALID_SOCKET;
    std::atomic<bool> running_{false};

    // Workers are kept rather than detached: a detached one could still be inside
    // handleConnection() when the Server is destroyed, which would call through a freed
    // `this` and touch a socket after WSACleanup.
    std::mutex workersMutex_;
    std::vector<Worker> workers_;
};

}  // namespace omsi::web