// SPDX-License-Identifier: MIT
//
// The loopback server and the library API.
//
// These bind a real socket on 127.0.0.1 with a port the system picks, so nothing here
// can collide with anything else on the machine and no firewall rule is involved.

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

#include "omsi/web/api.hpp"
#include "omsi/web/server.hpp"

#if OMSI_HAVE_WEB
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

using namespace std::chrono_literals;

namespace {

#if OMSI_HAVE_WEB

// A request over a fresh socket to `port`, returning the whole reply.
std::string httpGet(std::uint16_t port, const std::string& path) {
    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        return {};
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);

    if (::connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::closesocket(sock);
        return {};
    }

    const std::string request = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                                                         "Connection: close\r\n\r\n";
    ::send(sock, request.data(), static_cast<int>(request.size()), 0);

    std::string reply;
    char buffer[4096];
    for (;;) {
        const int got = ::recv(sock, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (got <= 0) {
            break;
        }
        reply.append(buffer, static_cast<std::size_t>(got));
    }
    ::closesocket(sock);
    return reply;
}

// Starts a server on a free port and runs it until the test ends.
class TestServer {
public:
    TestServer() {
        std::string error;
        if (!server_.listen(0, error)) {
            throw std::runtime_error("could not listen: " + error);
        }
        // serve() blocks, so it runs on a thread of our own; the Server keeps no thread of
        // its own, which is what keeps stop() free of a race.
        thread_ = std::thread([this] { server_.serve(); });
    }
    ~TestServer() {
        server_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    TestServer(const TestServer&) = delete;
    TestServer& operator=(const TestServer&) = delete;

    [[nodiscard]] std::uint16_t port() const { return server_.port(); }
    [[nodiscard]] omsi::web::Server& server() { return server_; }

private:
    omsi::web::Server server_;
    std::thread thread_;
};

#endif  // OMSI_HAVE_WEB

}  // namespace

#if OMSI_HAVE_WEB

TEST_CASE("a request is answered on loopback") {
    TestServer ts;
    ts.server().route("/api/ping", [](const omsi::web::Request& r, omsi::web::Response& out) {
        out.body = "pong " + r.path;
        return true;
    });

    const std::string reply = httpGet(ts.port(), "/api/ping");
    REQUIRE(!reply.empty());
    CHECK(reply.rfind("HTTP/1.1 200 OK", 0) == 0);
    CHECK(reply.find("pong /api/ping") != std::string::npos);
    // A length that matches the body is what keeps a client from hanging.
    CHECK(reply.find("Content-Length: 14") != std::string::npos);
}

TEST_CASE("an unknown path is a 404, not a crash") {
    TestServer ts;
    const std::string reply = httpGet(ts.port(), "/nothing/here");
    REQUIRE(!reply.empty());
    CHECK(reply.rfind("HTTP/1.1 404 Not Found", 0) == 0);
}

TEST_CASE("a path that tries to climb out of the web root is refused") {
    TestServer ts;
    // Even with a web root that exists, ".." must not escape it.
    ts.server().serveFiles(".");
    const std::string reply = httpGet(ts.port(), "/../../windows/win.ini");
    REQUIRE(!reply.empty());
    // Served as a 403 or 404, never the file outside the root.
    CHECK(reply.rfind("HTTP/1.1 200", 0) != 0);
    CHECK(reply.find("[fonts]") == std::string::npos);
}

TEST_CASE("static files are served from the web root") {
    // A regression this caught: on Windows "/app.css" is a *rooted* path, so joining it onto
    // the web root replaced the root instead of appending to it, and every file 404'd.
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "evigit-webtest-assets";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    {
        std::ofstream(dir / "probe.txt") << "served";
    }

    TestServer ts;
    ts.server().serveFiles(dir.string());

    const std::string reply = httpGet(ts.port(), "/probe.txt");
    REQUIRE(!reply.empty());
    CHECK(reply.rfind("HTTP/1.1 200 OK", 0) == 0);
    CHECK(reply.find("served") != std::string::npos);

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("the query string does not become part of the path") {
    TestServer ts;
    std::string seenPath;
    std::string seenQuery;
    ts.server().route("/api/search", [&](const omsi::web::Request& r, omsi::web::Response& out) {
        seenPath = r.path;
        seenQuery = r.query;
        out.body = "{}";
        return true;
    });

    httpGet(ts.port(), "/api/search?q=bus&n=5");
    CHECK(seenPath == "/api/search");
    CHECK(seenQuery == "q=bus&n=5");
}

TEST_CASE("the library API answers with the three lists") {
    TestServer ts;
    ts.server().route("/api/library", [](const omsi::web::Request&, omsi::web::Response& out) {
        omsi::web::Json j;
        j.beginObject();
        j.member("root", std::string_view("D:/Games/OMSI 2"));
        j.key("maps").beginArray();
        j.beginObject().member("id", std::string_view("Berlin")).endObject();
        j.endArray();
        j.key("vehicles").beginArray();
        j.beginObject().member("id", std::string_view("MAN/MAN_D92")).endObject();
        j.endArray();
        j.key("timetables").beginArray().endArray();
        j.key("problems").beginArray().endArray();
        j.endObject();
        out.body = j.take();
        return true;
    });

    const std::string reply = httpGet(ts.port(), "/api/library");
    REQUIRE(!reply.empty());
    CHECK(reply.find("application/json") != std::string::npos);
    CHECK(reply.find("\"Berlin\"") != std::string::npos);
    CHECK(reply.find("MAN/MAN_D92") != std::string::npos);
}

TEST_CASE("a route wins over a static file") {
    TestServer ts;
    ts.server().route("/api/library", [](const omsi::web::Request&, omsi::web::Response& out) {
        out.body = "from the route";
        return true;
    });
    const std::string reply = httpGet(ts.port(), "/api/library");
    CHECK(reply.find("from the route") != std::string::npos);
}

#endif  // OMSI_HAVE_WEB