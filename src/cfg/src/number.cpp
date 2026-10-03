// SPDX-License-Identifier: MIT
//
// Delphi-compatible lenient number parsing (StrToFloat / StrToInt with a default of 0).
//
// Ported from crates/omsi-cfg/src/number.rs of the Rust project.

#include "omsi/cfg/cfg.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>

namespace omsi::cfg {
namespace {

// Delphi accepts a leading '+', and "1." / ".5"; std::from_chars accepts neither.
// This returns the first whitespace-delimited token, with a comma turned into the
// decimal point that the original's locale uses.
std::string normalise(std::string_view text) {
    std::string s(text);
    const auto end = s.find_first_of(" \t");
    if (end != std::string::npos) {
        s.resize(end);
    }
    std::replace(s.begin(), s.end(), ',', '.');
    // Delphi accepts a leading '+'; std::from_chars does not.
    if (s.size() >= 2 && s[0] == '+') {
        s.erase(s.begin());
    }
    if (!s.empty() && s.back() == '.') {
        s.push_back('0');
    }
    if (s.size() >= 2 && s[0] == '-' && s[1] == '.') {
        s.insert(s.begin() + 1, '0');  // "-.5" -> "-0.5" (the '.' is at index 1)
    } else if (!s.empty() && s.front() == '.') {
        s.insert(s.begin(), '0');      // ".5" -> "0.5"
    }
    return s;
}

}  // namespace

double parseF64(std::string_view text) {
    const std::string s = normalise(text);
    if (s.empty()) {
        return 0.0;
    }

    double value = 0.0;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    if (result.ec == std::errc{} && result.ptr == s.data() + s.size() && std::isfinite(value)) {
        return value;
    }

    // Fall back to the longest numeric prefix, so "5x" reads as 5 rather than 0.
    std::size_t end = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        const bool numeric = (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' ||
                             c == 'e' || c == 'E';
        if (!numeric) {
            break;
        }
        end = i + 1;
    }
    if (end == 0) {
        return 0.0;
    }
    const auto tail = std::from_chars(s.data(), s.data() + end, value);
    if (tail.ec != std::errc{} || !std::isfinite(value)) {
        return 0.0;
    }
    return value;
}

float parseF32(std::string_view text) {
    // A double beyond f32's range would become infinite, which is garbage like any other.
    const auto value = static_cast<float>(parseF64(text));
    return std::isfinite(value) ? value : 0.0F;
}

std::int64_t parseI64(std::string_view text) {
    std::string s(text);
    const auto end = s.find_first_of(" \t");
    if (end != std::string::npos) {
        s.resize(end);
    }
    if (s.empty()) {
        return 0;
    }

    std::int64_t value = 0;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    if (result.ec == std::errc{} && result.ptr == s.data() + s.size()) {
        return value;
    }
    // Delphi's StrToInt fails on "1.0"; OMSI usually goes through StrToFloat and truncates.
    return static_cast<std::int64_t>(parseF64(s));
}

std::int32_t parseI32(std::string_view text) {
    const auto value = parseI64(text);
    const auto lo = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min());
    const auto hi = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());
    return static_cast<std::int32_t>(std::clamp(value, lo, hi));
}

}  // namespace omsi::cfg