// SPDX-License-Identifier: MIT
//
// A very small JSON writer.
//
// The launcher hands its parse results to a web front end as JSON. This is deliberately
// not a general JSON library: it writes, it never parses, and it escapes exactly the
// characters that can break a <script> tag or a JSON string. Nothing else is needed, and
// staying dependency-free keeps this buildable without the Vulkan SDK.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace omsi::web {

// Escape a string for use inside a JSON string literal. Also escapes <, > and & so the
// result is safe to embed in a <script> block, and U+2028/U+2029, which are line
// terminators in JavaScript but not in JSON.
std::string escape(std::string_view text);

// Builds one JSON value by appending to a string.
//
// Use beginObject/endObject, beginArray/endArray and the value helpers; the writer keeps
// track of what needs a comma so the result is always well formed. Example:
//
//   Json j;
//   j.beginObject();
//   j.key("name").value("Berlin");
//   j.key("tiles").beginArray().value(12).endArray();
//   j.endObject();
//   // {"name":"Berlin","tiles":[12]}
class Json {
public:
    // Container state: what we are inside and whether a comma is needed.
    struct Frame {
        bool isObject = false;
        bool hasContent = false;
        bool expectingValue = false;  // just after a key, so no comma
    };

    Json& beginObject();
    Json& endObject();
    Json& beginArray();
    Json& endArray();

    // Writes an object key. Valid only directly inside an object.
    Json& key(std::string_view name);

    Json& value(std::string_view text);
    Json& value(const char* text);
    Json& value(bool flag);
    Json& value(std::int64_t number);
    Json& value(std::uint64_t number);
    Json& value(int number);
    Json& value(double number);
    Json& nullValue();

    // Convenience: a key and its value in one call.
    Json& member(std::string_view name, std::string_view text);
    Json& member(std::string_view name, std::int64_t number);
    Json& member(std::string_view name, bool flag);

    [[nodiscard]] const std::string& str() const noexcept { return out_; }
    [[nodiscard]] std::string take() { return std::move(out_); }

private:
    void separate();   // the comma between entries
    void indent();     // a newline plus the current depth

    std::string out_;
    std::vector<Frame> stack_;
    bool pendingKey_ = false;
    int depth_ = 0;
};

}  // namespace omsi::web