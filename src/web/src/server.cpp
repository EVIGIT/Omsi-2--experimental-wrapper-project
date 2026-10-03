// SPDX-License-Identifier: MIT

#include "omsi/web/server.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include <winsock2.h>
#include <ws2tcpip.h>

namespace omsi::web {
namespace {

// The most a browser may send for the request head. Anything longer is a mistake or an
// attack, not a page we serve.
constexpr std::size_t kMaxHeadBytes = 16 * 1024;

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string reasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        default:  return "OK";
    }
}

// The MIME type for a static file, chosen by extension. Nothing exotic is served.
std::string contentTypeFor(const std::filesystem::path& path) {
    const std::string ext = lowerAscii(path.extension().string());
    if (ext == ".html")  return "text/html; charset=utf-8";
    if (ext == ".css")   return "text/css; charset=utf-8";
    if (ext == ".js")    return "text/javascript; charset=utf-8";
    if (ext == ".json")  return "application/json; charset=utf-8";
    if (ext == ".svg")   return "image/svg+xml";
    if (ext == ".png")   return "image/png";
    if (ext == ".ico")   return "image/x-icon";
    if (ext == ".woff2") return "font/woff2";
    return "application/octet-stream";
}

// Percent-decoding, because a map folder may carry a space or an umlaut.
bool percentDecode(std::string_view in, std::string& out) {
    out.clear();
    out.reserve(in.size());
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '%') {
            out.push_back(in[i]);
            continue;
        }
        if (i + 2 >= in.size()) {
            return false;
        }
        const int hi = hex(in[i + 1]);
        const int lo = hex(in[i + 2]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
    }
    return true;
}

}  // namespace

std::string Request::header(std::string_view name) const {
    const auto it = headers.find(lowerAscii(name));
    return it == headers.end() ? std::string{} : it->second;
}

Server::~Server() {
    stop();
    WSACleanup();
}

void Server::serveFiles(const std::string& webRoot) {
    webRoot_ = webRoot;
}

void Server::route(std::string prefix, Handler handler) {
    routes_.emplace_back(std::move(prefix), std::move(handler));
}

std::string Server::url() const {
    return "http://127.0.0.1:" + std::to_string(port_) + "/";
}

bool Server::listen(std::uint16_t port, std::string& error) {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        error = "WSAStartup failed";
        return false;
    }

    listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener_ == INVALID_SOCKET) {
        error = "could not create a socket";
        return false;
    }

    // Loopback only: this must never be reachable from the network.
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);

    if (::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error = std::string("could not bind to 127.0.0.1:") + std::to_string(port);
        return false;
    }
    if (::listen(listener_, 16) != 0) {
        error = "could not listen";
        return false;
    }

    // Port 0 asks the system for a free one, so the real port has to be read back.
    sockaddr_in actual{};
    int length = sizeof(actual);
    if (::getsockname(listener_, reinterpret_cast<sockaddr*>(&actual), &length) == 0) {
        port_ = ntohs(actual.sin_port);
    } else {
        port_ = port;
    }
    return true;
}

void Server::stop() {
    running_ = false;

    // accept() blocks, so the only way to wake it is to close the listener underneath the
    // call; it then fails and the loop sees running_ == false. Without this a caller could
    // never shut the server down.
    if (listener_ != INVALID_SOCKET) {
        ::closesocket(listener_);
        listener_ = INVALID_SOCKET;
    }

    // Wait for the workers too: one that is still inside handleConnection() would call
    // through a destroyed object if the Server went away first.
    std::vector<Worker> workers;
    {
        const std::lock_guard<std::mutex> lock(workersMutex_);
        workers.swap(workers_);
    }
    for (Worker& worker : workers) {
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }
}

void Server::serve() {
    running_ = true;
    while (running_) {
        sockaddr_in peer{};
        int length = sizeof(peer);
        const SOCKET client = ::accept(listener_, reinterpret_cast<sockaddr*>(&peer), &length);
        if (client == INVALID_SOCKET) {
            // accept() fails once the listener is closed, which is how stop() ends us.
            if (!running_) {
                return;
            }
            continue;
        }
            // One thread per connection: a browser opens several sockets for one page and would
            // otherwise wait behind the first one.
            auto finished = std::make_shared<std::atomic<bool>>(false);
            Worker worker;
            worker.finished = finished;
            worker.thread = std::thread([this, client, finished] {
                // Nothing may escape a thread: an exception out of here ends the process
                // through std::terminate, and one bad request must not take the launcher
                // with it.
                try {
                    handleConnection(client);
                } catch (const std::exception&) {
                    // The client simply gets no answer.
                } catch (...) {
                }
                ::closesocket(client);
                finished->store(true, std::memory_order_release);
            });

            {
                const std::lock_guard<std::mutex> lock(workersMutex_);
                // Reap the ones that have finished, so the list cannot grow without bound
                // over a long session.
                for (auto it = workers_.begin(); it != workers_.end();) {
                    if (it->finished->load(std::memory_order_acquire)) {
                        if (it->thread.joinable()) {
                            it->thread.join();
                        }
                        it = workers_.erase(it);
                    } else {
                        ++it;
                    }
                }
                workers_.push_back(std::move(worker));
            }
    }
}

void Server::handleConnection(SOCKET client) {
    std::string head;
    char buffer[4096];

    // Read the request head, up to and including the blank line that ends it.
    std::size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        const int got = ::recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (got <= 0) {
            return;
        }
        head.append(buffer, static_cast<std::size_t>(got));
        headerEnd = head.find("\r\n\r\n");
        if (headerEnd == std::string::npos && head.size() > kMaxHeadBytes) {
            return;
        }
    }

    Request request;
    {
        std::istringstream lines(head.substr(0, headerEnd));
        std::string line;
        if (!std::getline(lines, line)) {
            return;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream requestLine(line);
        std::string target;
        std::string version;
        requestLine >> request.method >> target >> version;
        if (request.method.empty() || target.empty()) {
            return;
        }

        const auto mark = target.find('?');
        if (mark == std::string::npos) {
            request.path = target;
        } else {
            request.path = target.substr(0, mark);
            request.query = target.substr(mark + 1);
        }

        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const auto colon = line.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            std::string value = line.substr(colon + 1);
            const auto first = value.find_first_not_of(" \t");
            request.headers[lowerAscii(line.substr(0, colon))] =
                (first == std::string::npos) ? std::string{} : value.substr(first);
        }
    }

    // Decode the path before anything looks at it.
    {
        std::string decoded;
        if (percentDecode(request.path, decoded)) {
            request.path = std::move(decoded);
        }
    }

    Response response;
    bool handled = false;

    // Only GET and HEAD: this server never changes anything.
    if (request.method != "GET" && request.method != "HEAD") {
        response.status = 405;
        response.contentType = "text/plain; charset=utf-8";
        response.body = "only GET is served";
        handled = true;
    } else {
        for (const auto& [prefix, handler] : routes_) {
            if (request.path.rfind(prefix, 0) == 0 && handler(request, response)) {
                handled = true;
                break;
            }
        }

        if (!handled && !webRoot_.empty()) {
            std::string relative = request.path;
            if (relative.empty() || relative == "/") {
                relative = "index.html";
            }
            // A leading slash has to go: on Windows "/app.css" is a rooted path, and
            // joining a rooted path replaces the left side instead of appending to it.
            if (relative.front() == '/') {
                relative.erase(relative.begin());
            }
            // A path that tries to climb out of the web root is refused rather than
            // normalised, so the check cannot be fooled.
            if (relative.find("..") != std::string::npos ||
                relative.find('\\') != std::string::npos ||
                relative.find(':') != std::string::npos) {
                response.status = 403;
                response.contentType = "text/plain; charset=utf-8";
                response.body = "refused";
                handled = true;
            } else {
                const std::filesystem::path full = std::filesystem::path(webRoot_) / relative;
                std::ifstream file(full, std::ios::binary);
                if (file) {
                    std::ostringstream contents;
                    contents << file.rdbuf();
                    response.status = 200;
                    response.contentType = contentTypeFor(full);
                    response.body = contents.str();
                    handled = true;
                }
            }
        }
    }

    if (!handled) {
        response.status = 404;
        response.contentType = "text/plain; charset=utf-8";
        response.body = "not found: " + request.path;
    }

    if (request.method == "HEAD") {
        response.body.clear();
    }

    std::ostringstream out;
    out << "HTTP/1.1 " << response.status << ' ' << reasonPhrase(response.status) << "\r\n"
        << "Content-Type: " << response.contentType << "\r\n"
        << "Content-Length: " << response.body.size() << "\r\n"
        // The front end is reloaded during development; caching would hide changes.
        << "Cache-Control: no-store\r\n"
        // Nothing is loaded from anywhere else, and nothing is framed.
        << "X-Content-Type-Options: nosniff\r\n"
        << "Connection: close\r\n\r\n"
        << response.body;

    const std::string text = out.str();
    std::size_t sent = 0;
    while (sent < text.size()) {
        const int wrote =
            ::send(client, text.data() + sent, static_cast<int>(text.size() - sent), 0);
        if (wrote <= 0) {
            return;
        }
        sent += static_cast<std::size_t>(wrote);
    }
}

}  // namespace omsi::web