// SPDX-License-Identifier: MIT
//
// A very small logging facility.
//
// The launcher writes a log next to its settings the way OMSI does; that log is the only
// thing a user can send when something goes wrong, so it has to work before any window
// exists.

#pragma once

#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <string_view>

namespace omsi::core {

enum class LogLevel { Debug, Info, Warn, Error };

// Where log lines go. A file set with `logToFile` is opened in append mode; the console
// always receives Warn and above unless `setConsoleLevel` lowers it.
void setConsoleLevel(LogLevel level);
void setMinimumLevel(LogLevel level);

// Append to `path`, creating its directory. Passing an empty path turns file logging off.
void logToFile(const std::filesystem::path& path);

void logMessage(LogLevel level, std::string_view text);

// Declared before the templates below so they can filter before formatting.
LogLevel minimumLevel();

template <typename... Args>
void logFormat(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
    if (static_cast<int>(level) < static_cast<int>(minimumLevel())) {
        return;
    }
    logMessage(level, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    logFormat(LogLevel::Debug, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    logFormat(LogLevel::Info, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    logFormat(LogLevel::Warn, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    logFormat(LogLevel::Error, fmt, std::forward<Args>(args)...);
}

// Declared above so the templates can filter before formatting.

}  // namespace omsi::core