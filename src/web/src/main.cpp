// SPDX-License-Identifier: MIT
//
// evigit-web - the launcher's window.
//
// Serves the front end over loopback and, unless told otherwise, opens it in the
// default browser. There is no embedded browser yet: WebView2 is the obvious next step
// and needs only a different way of pointing the same URL at a window, because the front
// end is already a plain page.

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

// WIN32_LEAN_AND_MEAN keeps windows.h from dragging in winsock.h (see cmake/warnings.cmake),
// which also means shellapi.h has to be asked for by name.
#include <windows.h>
#include <shellapi.h>

#include "omsi/content/library.hpp"
#include "omsi/core/log.hpp"
#include "omsi/web/api.hpp"
#include "omsi/web/server.hpp"

namespace {

constexpr std::string_view kUsage =
    R"(evigit-web - the launcher window for an OMSI 2 installation

usage:
  evigit-web [options] [root]

  root              OMSI 2 installation folder; found automatically when omitted

options:
  -h, --help        show this text
  --port <n>        serve on this port (default: a free one the system picks)
  --no-browser      do not open a browser, just print the address
  --web <dir>       serve the front end from this folder

)";

struct Options {
    std::filesystem::path root;
    std::filesystem::path webRoot;
    std::uint16_t port = 0;
    bool help = false;
    bool openBrowser = true;
};

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            o.help = true;
        } else if (arg == "--no-browser") {
            o.openBrowser = false;
        } else if (arg == "--port" && i + 1 < argc) {
            o.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--web" && i + 1 < argc) {
            o.webRoot = argv[++i];
        } else if (!arg.empty() && arg.front() == '-') {
            throw std::runtime_error("unknown option: " + std::string(arg));
        } else {
            o.root = std::filesystem::path(arg);
        }
    }
    return o;
}

// The folder the front end lives in. It sits beside the executable in a normal install,
// and the search walks up so the program can also be run straight out of the build tree.
std::filesystem::path findWebRoot(const std::filesystem::path& given) {
    if (!given.empty()) {
        return given;
    }
    std::error_code ec;
    wchar_t exe[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    const std::filesystem::path binDir = std::filesystem::path(exe).parent_path();

    const std::filesystem::path beside = binDir / "web";
    if (std::filesystem::is_directory(beside, ec)) {
        return beside;
    }
    for (std::filesystem::path up = binDir; up.has_parent_path(); up = up.parent_path()) {
        for (const char* candidate : {"web", "../web", "../../web", "../../../web"}) {
            const std::filesystem::path tryPath = up / candidate;
            if (std::filesystem::is_directory(tryPath, ec)) {
                return std::filesystem::weakly_canonical(tryPath, ec);
            }
        }
    }
    return {};
}

void openInBrowser(const std::string& url) {
    // ShellExecuteW returns an HINSTANCE, not an int, and comparing it against the <= 32
    // error range needs a wider type on x64.
    const auto result =
        reinterpret_cast<std::intptr_t>(ShellExecuteW(
            nullptr, L"open", std::wstring(url.begin(), url.end()).c_str(), nullptr, nullptr,
            SW_SHOWNORMAL));
    if (result <= 32) {
        omsi::core::warn("could not open a browser (ShellExecute returned {}); "
                         "open the address yourself", result);
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse(argc, argv);
        if (options.help) {
            std::cout << kUsage;
            return 0;
        }

        omsi::core::setConsoleLevel(omsi::core::LogLevel::Info);
        omsi::core::logToFile(std::filesystem::temp_directory_path() / "evigit-web.log");

        std::filesystem::path root = options.root;
        if (root.empty()) {
            const auto found = omsi::content::findInstall(std::filesystem::current_path());
            if (!found) {
                std::cerr << "No OMSI 2 installation found.\n\n"
                          << "Pass the folder as an argument, set OMSI_ROOT, or put an "
                             "\"OMSI 2\" folder beside the program.\n";
                return 2;
            }
            root = *found;
        }

        const omsi::content::Library library(root);
        std::cout << "Installation: " << root.string() << '\n';

        const std::filesystem::path webRoot = findWebRoot(options.webRoot);
        if (webRoot.empty()) {
            std::cerr << "Could not find the front end.\n"
                      << "Pass --web <folder>, or build so that web/ sits beside the "
                         "executable.\n";
            return 2;
        }
        std::cout << "Front end:   " << webRoot.string() << '\n';

        omsi::web::Server server;
        std::string error;
        if (!server.listen(options.port, error)) {
            std::cerr << "Could not start the launcher server: " << error << '\n';
            return 1;
        }
        omsi::web::registerLibraryRoutes(server, library);
        server.serveFiles(webRoot.string());

        const std::string url = server.url();
        std::cout << "\n  " << url << "\n\nClose this window, or press Ctrl+C, to stop.\n\n"
                  << std::flush;

        if (options.openBrowser) {
            openInBrowser(url);
        }

        server.serve();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "evigit-web: " << e.what() << '\n';
        return 1;
    }
}