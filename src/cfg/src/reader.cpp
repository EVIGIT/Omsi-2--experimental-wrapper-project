// SPDX-License-Identifier: MIT
//
// Keyword recognition and the sequential reader.
//
// Ported from crates/omsi-cfg/src/lib.rs of the Rust project.

#include "omsi/cfg/cfg.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace omsi::cfg {
namespace {

constexpr std::string_view kDisabled = "-<DISABLED>-";
constexpr std::string_view kEnabled = "-<ENABLED>-";

// What .hof cuts off the end of every line.
bool isHofTrailing(char c) {
    return c == '\t' || c == '\n' || c == '\r' || c == ' ' || c == '"';
}

std::string toLowerAscii(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// The stock content's keywords that carry a capital letter. The original compares these
// by exact string equality, so "[matl_noZwrite]" is a keyword while "[matl_nozwrite]" is
// free text. A name the original does not know is matched without regard to case, which
// is what lets a mod spell it however it likes.
struct MixedCaseKeyword {
    std::string_view lower;
    std::string_view spelling;
};
inline constexpr MixedCaseKeyword kMixedCase[] = {
    {"matl_nozwrite", "matl_noZwrite"},   {"matl_noztest", "matl_noZtest"},
    {"lod", "LOD"},                       {"nightmapmode", "NightMapMode"},
    {"nodistancecheck", "noDistanceCheck"},
};

// The original's spelling of a keyword it knows, or nullopt when it does not know the
// name (in which case the caller compares case-insensitively).
std::optional<std::string_view> omsiSpelling(std::string_view name) {
    const std::string key = toLowerAscii(name);
    for (const auto& k : kMixedCase) {
        if (k.lower == key) {
            return k.spelling;
        }
    }
    return std::nullopt;
}

}  // namespace

std::string_view keywordWith(std::string_view line, KeywordRule rule) {
    std::string_view t = line;
    if (rule == KeywordRule::TrimEnd) {
        while (!t.empty() && isHofTrailing(t.back())) {
            t.remove_suffix(1);
        }
    }
    if (t.size() < 2 || t.front() != '[' || t.back() != ']') {
        return {};
    }
    const std::string_view inner = t.substr(1, t.size() - 2);

    // A keyword never contains a second bracket. Free text such as "[mesh] macht ..." is
    // not a keyword either, because it does not end with ']'.
    if (inner.empty() || inner.find('[') != std::string_view::npos ||
        inner.find(']') != std::string_view::npos) {
        return {};
    }

    if (rule != KeywordRule::AnyCase) {
        if (const auto spelling = omsiSpelling(inner); spelling && *spelling != inner) {
            return {};
        }
    }
    return inner;
}

std::string_view keywordOf(std::string_view line) {
    return keywordWith(line, KeywordRule::Exact);
}

// ---------------------------------------------------------------------------
// CfgFile
// ---------------------------------------------------------------------------

CfgFile CfgFile::read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string bytes = buffer.str();
    return fromBytes(path, bytes);
}

CfgFile CfgFile::fromBytes(std::filesystem::path path, std::string_view bytes) {
    CfgFile file;
    file.path_ = std::move(path);
    file.lines_ = splitLines(decodeText(bytes));
    return file;
}

CfgFile CfgFile::fromString(std::filesystem::path path, std::string_view text) {
    CfgFile file;
    file.path_ = std::move(path);
    file.lines_ = splitLines(text);
    return file;
}

std::filesystem::path CfgFile::dir() const {
    return path_.has_parent_path() ? path_.parent_path() : std::filesystem::path{};
}

std::string_view CfgFile::line(std::size_t index) const {
    return index < lines_.size() ? std::string_view{lines_[index]} : std::string_view{};
}

// ---------------------------------------------------------------------------
// CfgReader
// ---------------------------------------------------------------------------

CfgReader::CfgReader(const CfgFile& file) noexcept : file_(&file) {}

CfgReader& CfgReader::withRule(KeywordRule rule) noexcept {
    rule_ = rule;
    return *this;
}

CfgReader& CfgReader::skippingDisabledBlocks(bool on) noexcept {
    skipDisabled_ = on;
    return *this;
}

bool CfgReader::seek(std::vector<std::string_view> tokens) {
    bool disabled = false;
    while (pos_ < file_->lineCount()) {
        const std::string_view line = file_->line(pos_);

        if (skipDisabled_) {
            if (line == kDisabled) {
                disabled = true;
                ++pos_;
                continue;
            }
            if (line == kEnabled) {
                disabled = false;
                ++pos_;
                continue;
            }
            // Everything between the markers is skipped, keyword lines included.
            if (disabled) {
                ++pos_;
                continue;
            }
        }

        for (const std::string_view token : tokens) {
            if (line == token) {
                blockLine_ = pos_++;
                keyword_ = line;
                return true;
            }
        }

        if (const auto kw = keywordWith(line, rule_); !kw.empty()) {
            blockLine_ = pos_++;
            keyword_ = kw;
            return true;
        }
        ++pos_;
    }
    return false;
}

bool CfgReader::is(std::string_view name) const {
    return equalsIgnoreCase(keyword_, name);
}

std::string_view CfgReader::next() {
    return file_->line(pos_++);
}

std::string_view CfgReader::nextNonEmpty() {
    while (pos_ < file_->lineCount()) {
        const std::string_view line = file_->line(pos_);
        ++pos_;
        if (line.find_first_not_of(" \t\r") != std::string_view::npos) {
            return line;
        }
    }
    return {};
}

double CfgReader::nextFloat() {
    return parseF64(next());
}

float CfgReader::nextF32() {
    return parseF32(next());
}

std::int64_t CfgReader::nextI64() {
    return parseI64(next());
}

std::int32_t CfgReader::nextI32() {
    return parseI32(next());
}

}  // namespace omsi::cfg