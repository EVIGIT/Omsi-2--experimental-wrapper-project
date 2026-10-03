// SPDX-License-Identifier: MIT
//
// The JSON writer the launcher hands to the front end.

#include <doctest/doctest.h>

#include "omsi/web/json.hpp"

using omsi::web::Json;

TEST_CASE("an empty object and array") {
    Json j;
    CHECK((j.beginObject().endObject().str() == "{}"));
    Json a;
    CHECK((a.beginArray().endArray().str() == "[]"));
}

TEST_CASE("scalars") {
    Json j;
    j.beginObject();
    j.key("text").value("hello");
    j.key("yes").value(true);
    j.key("no").value(false);
    j.key("int").value(std::int64_t{42});
    j.key("zero").value(0);
    j.key("nothing").nullValue();
    j.endObject();
    const std::string out = j.str();
    CHECK(out.find("\"text\": \"hello\"") != std::string::npos);
    CHECK(out.find("\"yes\": true") != std::string::npos);
    CHECK(out.find("\"no\": false") != std::string::npos);
    CHECK(out.find("\"int\": 42") != std::string::npos);
    CHECK(out.find("\"nothing\": null") != std::string::npos);
}

TEST_CASE("a comma only separates entries") {
    Json j;
    j.beginArray();
    j.value(1).value(2).value(3);
    j.endArray();
    // Each entry goes on its own line with indentation, so the commas sit at the end of a
    // line rather than between two numbers.
    const std::string out = j.str();
    CHECK(out.find("1,") != std::string::npos);
    CHECK(out.find("2,") != std::string::npos);
    // No trailing comma before the closing bracket, and nothing after it.
    CHECK(out.find("3\n") != std::string::npos);
    CHECK(out.back() == ']');
    CHECK(out.find(",]") == std::string::npos);
}

TEST_CASE("an empty container inside another one") {
    Json j;
    j.beginObject();
    j.key("list").beginArray().endArray();
    j.key("obj").beginObject().endObject();
    j.endObject();
    CHECK(j.str().find("\"list\": []") != std::string::npos);
    CHECK(j.str().find("\"obj\": {}") != std::string::npos);
}

TEST_CASE("nested containers stay balanced") {
    Json j;
    j.beginObject();
    j.key("maps").beginArray();
    j.beginObject().key("id").value("Berlin").endObject();
    j.beginObject().key("id").value("Hamburg").endObject();
    j.endArray();
    j.endObject();

    // Count the braces and brackets: they must match, which is what a client needs.
    const std::string out = j.str();
    const auto count = [&out](char c) {
        int n = 0;
        for (const char ch : out) {
            if (ch == c) ++n;
        }
        return n;
    };
    CHECK(count('{') == count('}'));
    CHECK(count('[') == count(']'));
    CHECK(out.find("\"id\": \"Berlin\"") != std::string::npos);
    CHECK(out.find("\"id\": \"Hamburg\"") != std::string::npos);
}

TEST_CASE("strings escape what JSON requires") {
    CHECK(omsi::web::escape("a\"b") == "a\\\"b");
    CHECK(omsi::web::escape("a\\b") == "a\\\\b");
    CHECK(omsi::web::escape("a\nb") == "a\\nb");
    CHECK(omsi::web::escape("a\tb") == "a\\tb");
    CHECK(omsi::web::escape("a\rb") == "a\\rb");
}

TEST_CASE("a control character becomes \\u") {
    CHECK(omsi::web::escape(std::string_view("a\x01\x1F", 3)) == "a\\u0001\\u001f");
}

TEST_CASE("<, > and & are escaped so a payload is safe in a script tag") {
    // Valid JSON, but exactly what would let a map folder close a <script> element.
    CHECK(omsi::web::escape("<script>") == "\\u003cscript\\u003e");
    CHECK(omsi::web::escape("a & b") == "a \\u0026 b");
}

TEST_CASE("UTF-8 passes through") {
    CHECK(omsi::web::escape("Gro\xc3\x9f""e") == "Gro\xc3\x9f""e");
    CHECK(omsi::web::escape("\xd0\x9c\xd0\xb8\xd1\x80") == "\xd0\x9c\xd0\xb8\xd1\x80");  // Мир
}

TEST_CASE("the JavaScript line separators are escaped") {
    // U+2028 and U+2029 are legal in JSON but end a line in a JavaScript string.
    const std::string eol = "\xe2\x80\xa8";  // U+2028
    CHECK(omsi::web::escape(eol) == "\\u2028");
    const std::string sep = "\xe2\x80\xa9";  // U+2029
    CHECK(omsi::web::escape(sep) == "\\u2029");
}

TEST_CASE("invalid UTF-8 becomes the replacement character, not garbage") {
    // A lone continuation byte cannot start a sequence.
    const std::string broken = "\x80\x80";
    const std::string out = omsi::web::escape(broken);
    CHECK(out.find("\x80") == std::string::npos);
    CHECK(out == "\xef\xbf\xbd\xef\xbf\xbd");
}

TEST_CASE("a non-finite number is null, not NaN") {
    // JSON has no NaN or infinity; a client would fail to parse them.
    Json j;
    j.beginObject();
    j.key("nan").value(std::numeric_limits<double>::quiet_NaN());
    j.key("inf").value(std::numeric_limits<double>::infinity());
    j.endObject();
    CHECK(j.str().find("\"nan\": null") != std::string::npos);
    CHECK(j.str().find("\"inf\": null") != std::string::npos);
}

TEST_CASE("member() writes a key and its value together") {
    Json j;
    j.beginObject();
    j.member("name", std::string_view("Berlin"));
    j.member("count", std::int64_t{7});
    j.member("ok", true);
    j.endObject();
    const std::string out = j.str();
    CHECK(out.find("\"name\": \"Berlin\"") != std::string::npos);
    CHECK(out.find("\"count\": 7") != std::string::npos);
    CHECK(out.find("\"ok\": true") != std::string::npos);
}