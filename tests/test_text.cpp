// SPDX-License-Identifier: MIT
//
// Text decoding: line splitting, code pages and byte-order marks.

#include <doctest/doctest.h>

#include <string>

#include "omsi/cfg/cfg.hpp"

using omsi::cfg::CodePage;
using omsi::cfg::decodeText;
using omsi::cfg::splitLines;

TEST_CASE("splitLines handles CRLF, LF and a bare CR") {
    CHECK(splitLines("a\r\nb\r\nc").size() == 3);
    CHECK(splitLines("a\nb\nc").size() == 3);
    CHECK(splitLines("a\rb\rc").size() == 3);
    CHECK(splitLines("a\r\nb\nc\rd").size() == 4);
    // A CRLF pair is one break, never two.
    CHECK(splitLines("a\r\n").size() == 1);
    CHECK(splitLines("").empty());
    // A trailing line without a newline is still a line.
    CHECK(splitLines("a\nb").size() == 2);
    // An empty line between two lines is a line of its own.
    CHECK(splitLines("a\n\nb").size() == 3);
    CHECK(splitLines("a\n\nb")[1].empty());
}

TEST_CASE("a UTF-16LE byte-order mark decides the encoding") {
    // "hi" in UTF-16LE, which is how every map file is written. Built from explicit bytes:
    // the literal "h\0" would stop at the NUL and leave out the zero byte.
    const std::string utf16 = {'\xFF', '\xFE', 'h', '\x00', 'i', '\x00'};
    CHECK(decodeText(utf16) == "hi");
}

TEST_CASE("a UTF-8 byte-order mark is skipped") {
    const std::string utf8 = std::string("\xEF\xBB\xBF", 3) + "hi";
    CHECK(decodeText(utf8) == "hi");
}

TEST_CASE("UTF-8 without a byte-order mark survives") {
    CHECK(decodeText("Gro\xc3\x9f""e") == "Gro\xc3\x9f""e");   // Große
    CHECK(decodeText("\xd0\x9c\xd0\xb8\xd1\x80") == "\xd0\x9c\xd0\xb8\xd1\x80");  // Мир
}

TEST_CASE("Windows-1252 is the stock code page") {
    // 0x80 is the euro, 0x93 a left double quote, 0xE4 an a-umlaut.
    CHECK(decodeText("\x80") == "\xe2\x82\xac");
    CHECK(decodeText("\x93") == "\xe2\x80\x9c");
    CHECK(decodeText("\xe4") == "\xc3\xa4");
    // "Grosse" in German has at most two high bytes in a row, so it stays 1252.
    CHECK(decodeText("Grosse") == "Grosse");
}

TEST_CASE("Windows-1251 is chosen for Russian text") {
    // "Мир" in Windows-1251. U+0410 (А) is byte 0xC0 and the block runs to U+044F (я) at
    // 0xFF, so: М = U+041C = 0xCC, и = U+0438 = 0xE8, р = U+0440 = 0xF0. All three are at
    // or above 0xC0 and in one run, which is what tells it apart from German. Written as
    // an explicit byte array so the escapes cannot run into the characters beside them.
    const std::string russian = {'\xCC', '\xE8', '\xF0'};
    CHECK(decodeText(russian) == "\xD0\x9C\xD0\xB8\xD1\x80");
    // The Cyrillic block runs from U+0410 at byte 0xC0 to U+044F at 0xFF.
    CHECK(omsi::cfg::detect(russian) == CodePage::Windows1251);
}

TEST_CASE("Windows-1250 is chosen for a Polish word") {
    // "zab" with the l-with-stroke of 1250 (0xB3) inside it. Written as an explicit byte
    // array: "\xb3b" would read the trailing "b" as part of the hex escape.
    const std::string polish = {'z', 'a', '\xB3', 'b'};
    // "za" + U+0142 (l-with-stroke, encoded as 0xC5 0x82) + "b". The string literal is
    // split so the trailing "b" is not swallowed by the hex escape.
    CHECK(decodeText(polish) == "za\xC5\x82" "b");
}

TEST_CASE("ASCII passes through untouched") {
    const std::string plain = "MAN SD200 [mesh] 12.5 -3";
    CHECK(decodeText(plain) == plain);
    CHECK(omsi::cfg::detect(plain) == CodePage::Windows1252);
}

TEST_CASE("encodeText writes Windows-1252 with '?' for the rest") {
    const std::string sharpS = {'G', 'r', 'o', '\xDF', 'e'};  // "Große"
    CHECK(omsi::cfg::encodeText("Gro\xc3\x9f" "e") == sharpS);
    CHECK(omsi::cfg::encodeText("\xe2\x82\xac") == "\x80");
    // Not representable in 1252.
    CHECK(omsi::cfg::encodeText("\xd0\x9c") == "?");
}

TEST_CASE("a round trip through decode and encode is stable for 1252") {
    const std::string original = {'G', 'r', 'o', '\xDF', 'e', ' ', '\x93', 'q', 'u', 'o',
                                  't', 'e', 'd', '\x94', ' ', '\x80'};
    CHECK(omsi::cfg::encodeText(decodeText(original)) == original);
}