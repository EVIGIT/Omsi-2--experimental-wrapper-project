// SPDX-License-Identifier: MIT
//
// Decoding OMSI's single-byte code pages.
//
// Ported from crates/omsi-cfg/src/codepage.rs of the Rust project.

#include "omsi/cfg/cfg.hpp"

#include <cstdint>
#include <string>

#include "tables.hpp"

namespace omsi::cfg {
namespace {

using detail::appendUtf8;
using detail::kC1_1250;
using detail::kC1_1251;
using detail::kC1_1252;

// Decode a UTF-16LE run (the BOM has already been consumed). OMSI's content is BMP; a
// surrogate half becomes U+FFFD rather than invalid UTF-8.
std::string decodeUtf16Le(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size());
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        const auto unit = static_cast<char32_t>(
            static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[i])) |
            (static_cast<unsigned char>(bytes[i + 1]) << 8));
        appendUtf8((unit >= 0xD800 && unit <= 0xDFFF) ? 0xFFFD : unit, out);
    }
    return out;
}

// Length of the UTF-8 sequence starting at `bytes[i]`, or 0 when it is not valid UTF-8.
std::size_t utf8SequenceLength(std::string_view bytes, std::size_t i) {
    const auto b0 = static_cast<unsigned char>(bytes[i]);
    std::size_t extra = 0;
    char32_t cp = 0;
    if (b0 < 0x80) {
        return 1;
    }
    if ((b0 & 0xE0) == 0xC0) {
        extra = 1;
        cp = b0 & 0x1FU;
    } else if ((b0 & 0xF0) == 0xE0) {
        extra = 2;
        cp = b0 & 0x0FU;
    } else if ((b0 & 0xF8) == 0xF0) {
        extra = 3;
        cp = b0 & 0x07U;
    } else {
        return 0;
    }
    if (i + extra >= bytes.size()) {
        return 0;
    }
    for (std::size_t k = 1; k <= extra; ++k) {
        const auto bk = static_cast<unsigned char>(bytes[i + k]);
        if ((bk & 0xC0) != 0x80) {
            return 0;
        }
        cp = (cp << 6) | (bk & 0x3FU);
    }
    // Reject over-long forms, surrogates and out-of-range code points.
    if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) ||
        (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
        return 0;
    }
    return extra + 1;
}

bool isValidUtf8(std::string_view bytes) {
    std::size_t i = 0;
    while (i < bytes.size()) {
        const std::size_t n = utf8SequenceLength(bytes, i);
        if (n == 0) {
            return false;
        }
        i += n;
    }
    return true;
}

bool isAsciiLetter(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

}  // namespace

void appendDecoded(CodePage cp, std::uint8_t byte, std::string& out) {
    if (byte < 0x80) {
        out.push_back(static_cast<char>(byte));
        return;
    }
    const auto index = static_cast<std::size_t>(byte) - 0x80U;
    char32_t unit = 0;
    switch (cp) {
        case CodePage::Windows1252:
            unit = (byte < 0xC0) ? kC1_1252[index] : static_cast<char32_t>(byte);
            break;
        case CodePage::Windows1250:
            if (byte < 0xC0) {
                unit = kC1_1250[index];
            } else if (byte < 0xE0) {
                // 0xC0..0xCF and 0xD0..0xDF are two runs of consecutive letters
                // (A-ogonek .. e-caron, then E-caron .. i-ogonek).
                unit = (byte < 0xD0) ? static_cast<char32_t>(0x0104U + (byte - 0xC0U))
                                     : static_cast<char32_t>(0x011AU + (byte - 0xD0U));
            } else {
                unit = static_cast<char32_t>(byte);
            }
            break;
        case CodePage::Windows1251:
            // 0xC0..0xFF is the Cyrillic block A..ya.
            unit = (byte < 0xC0) ? kC1_1251[index] : static_cast<char32_t>(0x0410U + (byte - 0xC0U));
            break;
    }
    appendUtf8(unit, out);
}

CodePage detect(std::string_view bytes) {
    if (isValidUtf8(bytes)) {
        // The caller still needs to know it may decode as UTF-8; that is signalled by the
        // absence of a NUL byte (see decodeText).
        return CodePage::Windows1252;
    }

    // Windows-1251: Cyrillic letters stand in long runs. German has at most two 0xC0.. 
    // bytes in a row ("Grosse"), Russian words run to three or more.
    std::size_t letters = 0;
    std::size_t longRuns = 0;
    for (std::size_t i = 0; i < bytes.size();) {
        const auto b = static_cast<unsigned char>(bytes[i]);
        if (b >= 0xC0) {
            std::size_t run = 1;
            std::size_t j = i + 1;
            while (j < bytes.size() && static_cast<unsigned char>(bytes[j]) >= 0xC0) {
                ++run;
                ++j;
            }
            ++letters;
            if (run >= 3) {
                ++longRuns;
            }
            i = j;
        } else {
            ++i;
        }
    }
    if (letters > 0 && longRuns * 2 >= letters) {
        return CodePage::Windows1251;
    }

    // Windows-1250: a Polish or Czech letter standing next to another letter. In 1252
    // those bytes are punctuation that rarely appears inside a word.
    auto is1250Sign = [](unsigned char c) {
        return c == 0xA3 || c == 0xB3 || c == 0xA5 || c == 0xB9 || c == 0xAA || c == 0xBA ||
               c == 0xAF || c == 0xBF;
    };
    for (std::size_t i = 1; i + 1 < bytes.size(); ++i) {
        if (!is1250Sign(static_cast<unsigned char>(bytes[i]))) {
            continue;
        }
        if (isAsciiLetter(static_cast<unsigned char>(bytes[i - 1])) ||
            isAsciiLetter(static_cast<unsigned char>(bytes[i + 1]))) {
            return CodePage::Windows1250;
        }
    }

    return CodePage::Windows1252;
}
std::string decodeText(std::string_view bytes) {
    // A byte-order mark settles the encoding; map files carry one, most others do not.
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
        static_cast<unsigned char>(bytes[1]) == 0xFE) {
        return decodeUtf16Le(bytes.substr(2));
    }
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        return std::string(bytes.substr(3));
    }

    // detect() returns Windows-1252 both for "it is Windows-1252" and for "it is valid
    // UTF-8", so try UTF-8 first and fall back when it does not decode cleanly.
    std::string out;
    out.reserve(bytes.size());
    std::size_t i = 0;
    bool utf8 = true;
    while (i < bytes.size()) {
        const std::size_t n = utf8SequenceLength(bytes, i);
        if (n == 0) {
            utf8 = false;
            break;
        }
        const auto b0 = static_cast<unsigned char>(bytes[i]);
        if (n == 1) {
            out.push_back(static_cast<char>(b0));
        } else {
            char32_t cp = 0;
            const std::size_t extra = n - 1;
            if (extra == 1) {
                cp = b0 & 0x1FU;
            } else if (extra == 2) {
                cp = b0 & 0x0FU;
            } else {
                cp = b0 & 0x07U;
            }
            for (std::size_t k = 1; k <= extra; ++k) {
                cp = (cp << 6) | (static_cast<unsigned char>(bytes[i + k]) & 0x3FU);
            }
            appendUtf8(cp, out);
        }
        i += n;
    }
    if (utf8) {
        return out;
    }

    const CodePage cp = detect(bytes);
    out.clear();
    for (const char c : bytes) {
        appendDecoded(cp, static_cast<std::uint8_t>(c), out);
    }
    return out;
}

std::string encodeText(std::string_view text) {
    // Windows-1252 on the way out, like OMSI writes its own files. A character the code
    // page does not have becomes '?'.
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }
        const std::size_t n = utf8SequenceLength(text, i);
        if (n <= 1) {
            out.push_back('?');
            ++i;
            continue;
        }
        char32_t cp = 0;
        const std::size_t extra = n - 1;
        if (extra == 1) {
            cp = c & 0x1FU;
        } else if (extra == 2) {
            cp = c & 0x0FU;
        } else {
            cp = c & 0x07U;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3FU);
        }
        i += n;

        std::uint8_t back = '?';
        if (cp >= 0xA0 && cp <= 0xFF) {
            back = static_cast<std::uint8_t>(cp);
        } else {
            for (std::size_t j = 0; j < kC1_1252.size(); ++j) {
                if (kC1_1252[j] == cp) {
                    back = static_cast<std::uint8_t>(0x80U + j);
                    break;
                }
            }
        }
        out.push_back(static_cast<char>(back));
    }
    return out;
}

std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    current.reserve(128);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            // CRLF counts once.
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            out.push_back(std::move(current));
            current.clear();
        } else if (c == '\n') {
            out.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        out.push_back(std::move(current));
    }
    return out;
}

}  // namespace omsi::cfg