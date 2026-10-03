// SPDX-License-Identifier: MIT

#include "omsi/core/log.hpp"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>

namespace omsi::core {
namespace {

struct LogState {
    std::mutex mutex;
    LogLevel console = LogLevel::Info;
    LogLevel minimum = LogLevel::Debug;
    std::ofstream file;
};

LogState& state() {
    static LogState s;
    return s;
}

std::string_view levelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

}  // namespace

LogLevel minimumLevel() {
    return state().minimum;
}

void setConsoleLevel(LogLevel level) {
    state().console = level;
}

void setMinimumLevel(LogLevel level) {
    state().minimum = level;
}

void logToFile(const std::filesystem::path& path) {
    LogState& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);
    s.file.close();
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    s.file.open(path, std::ios::app);
}

void logMessage(LogLevel level, std::string_view text) {
    LogState& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);

    // A local time stamp, which is what a user reading a log after the fact expects.
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    char stamp[32]{};
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);

    if (static_cast<int>(level) >= static_cast<int>(s.console)) {
        std::ostream& out = (level == LogLevel::Error || level == LogLevel::Warn)
                                ? std::cerr
                                : std::cout;
        out << '[' << stamp << "] [" << levelName(level) << "] " << text << '\n';
        if (level == LogLevel::Error) {
            out.flush();
        }
    }
    if (s.file.is_open()) {
        s.file << '[' << stamp << "] [" << levelName(level) << "] " << text << '\n';
        s.file.flush();
    }
}

}  // namespace omsi::core