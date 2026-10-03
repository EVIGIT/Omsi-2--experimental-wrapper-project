// SPDX-License-Identifier: MIT

#include "omsi/web/json.hpp"

#include <cmath>
#include <cstdio>

namespace omsi::web {
namespace {

void appendUtf8(char32_t cp, std::string& out) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0U | (cp >> 6)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0U | (cp >> 12)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0U | (cp >> 18)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 12) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
}

}  // namespace

std::string escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        switch (c) {
            case '"':  out += "\\\""; ++i; continue;
            case '\\': out += "\\\\"; ++i; continue;
            case '\n': out += "\\n";  ++i; continue;
            case '\r': out += "\\r";  ++i; continue;
            case '\t': out += "\\t";  ++i; continue;
            case '\b': out += "\\b";  ++i; continue;
            case '\f': out += "\\f";  ++i; continue;
            default: break;
        }
        // Valid in JSON, but exactly what makes a payload unsafe to drop inside a
        // <script> tag, so they are escaped too.
        if (c == '<' || c == '>' || c == '&') {
            out += "\\u00";
            out.push_back("0123456789abcdef"[c >> 4]);
            out.push_back("0123456789abcdef"[c & 0x0F]);
            ++i;
            continue;
        }
        if (c < 0x20) {
            char buf[7];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
            ++i;
            continue;
        }
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }

        // Pass a well-formed UTF-8 sequence through; replace anything else with U+FFFD so
        // the result is always valid JSON.
        std::size_t extra = 0;
        char32_t cp = 0;
        if ((c & 0xE0) == 0xC0) {
            extra = 1;
            cp = c & 0x1FU;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            cp = c & 0x0FU;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            cp = c & 0x07U;
        } else {
            appendUtf8(0xFFFD, out);
            ++i;
            continue;
        }
        if (i + extra >= text.size()) {
            appendUtf8(0xFFFD, out);
            break;
        }
        bool ok = true;
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto bk = static_cast<unsigned char>(text[i + k]);
            if ((bk & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (bk & 0x3FU);
        }
        if (!ok) {
            appendUtf8(0xFFFD, out);
            ++i;
            continue;
        }
        const std::size_t start = i;
        i += extra + 1;
        // U+2028 and U+2029 are line terminators in JavaScript but legal in JSON.
        if (cp == 0x2028 || cp == 0x2029) {
            out += "\\u202";
            out.push_back(cp == 0x2028 ? '8' : '9');
            continue;
        }
        out.append(text.substr(start, extra + 1));
    }
    return out;
}

void Json::separate() {
    if (stack_.empty()) {
        return;
    }
    Frame& frame = stack_.back();
    if (frame.expectingValue) {
        // We are the value of the key just written; no comma.
        frame.expectingValue = false;
        return;
    }
    if (frame.hasContent) {
        out_ += ',';
    }
    frame.hasContent = true;
    out_ += '\n';
    out_.append(static_cast<std::size_t>(depth_) * 2, ' ');
}

void Json::indent() {
    out_ += '\n';
    out_.append(static_cast<std::size_t>(depth_) * 2, ' ');
}

Json& Json::beginObject() {
    separate();
    out_ += '{';
    stack_.push_back(Frame{true, false, false});
    ++depth_;
    return *this;
}

Json& Json::endObject() {
    --depth_;
    const bool had = stack_.back().hasContent;
    stack_.pop_back();
    if (had) {
        indent();
    }
    out_ += '}';
    return *this;
}

Json& Json::beginArray() {
    separate();
    out_ += '[';
    stack_.push_back(Frame{false, false, false});
    ++depth_;
    return *this;
}

Json& Json::endArray() {
    --depth_;
    const bool had = stack_.back().hasContent;
    stack_.pop_back();
    if (had) {
        indent();
    }
    out_ += ']';
    return *this;
}

Json& Json::key(std::string_view name) {
    separate();
    out_ += '"';
    out_ += escape(name);
    out_ += "\": ";
    stack_.back().expectingValue = true;
    pendingKey_ = true;
    return *this;
}

Json& Json::value(std::string_view text) {
    separate();
    out_ += '"';
    out_ += escape(text);
    out_ += '"';
    pendingKey_ = false;
    return *this;
}

Json& Json::value(const char* text) {
    return value(std::string_view(text == nullptr ? "" : text));
}

Json& Json::value(bool flag) {
    separate();
    out_ += flag ? "true" : "false";
    pendingKey_ = false;
    return *this;
}

Json& Json::value(std::int64_t number) {
    separate();
    out_ += std::to_string(number);
    pendingKey_ = false;
    return *this;
}

Json& Json::value(std::uint64_t number) {
    separate();
    out_ += std::to_string(number);
    pendingKey_ = false;
    return *this;
}

Json& Json::value(int number) {
    return value(static_cast<std::int64_t>(number));
}

Json& Json::value(double number) {
    separate();
    if (!std::isfinite(number)) {
        // JSON has no infinity or NaN; null is what every client expects here.
        out_ += "null";
        pendingKey_ = false;
        return *this;
    }
    char buf[40];
    const int n = std::snprintf(buf, sizeof(buf), "%.17g", number);
    if (n > 0) {
        out_.append(buf, static_cast<std::size_t>(n));
    } else {
        out_ += '0';
    }
    pendingKey_ = false;
    return *this;
}

Json& Json::nullValue() {
    separate();
    out_ += "null";
    pendingKey_ = false;
    return *this;
}

Json& Json::member(std::string_view name, std::string_view text) {
    return key(name).value(text);
}

Json& Json::member(std::string_view name, std::int64_t number) {
    return key(name).value(number);
}

Json& Json::member(std::string_view name, bool flag) {
    return key(name).value(flag);
}

}  // namespace omsi::web