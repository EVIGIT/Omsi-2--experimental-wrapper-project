// SPDX-License-Identifier: MIT
//
// OMSI 2's text formats.
//
// Almost every OMSI content file (.bus, .sco, .sli, .cfg, .hof, .map, ...) is a list of
// lines in which a *keyword* line such as "[mesh]" is followed by a fixed number of
// parameter lines. Everything else is free text and is ignored.
//
// The rules below were read off the loaders of Omsi.exe 2.2.032 (each compares the line it
// read with its keyword literals by plain Delphi string equality) and are documented in
// docs/FORMATS.md of the Rust project this was ported from.
//
//  * a keyword is recognised only when the whole line is "[name]": an indented keyword
//    (the stock files' help texts, a mod's disabled block) is free text, and so is one
//    followed by spaces;
//  * the name is spelled as the original spells it ("[matl_noZwrite]", not
//    "[matl_nozWrite]"); names the original does not know are compared case-insensitively;
//  * two loaders differ: .hof cuts trailing tabs, spaces and quotes off every line,
//    ailists.cfg lower-cases its lines;
//  * the main content loaders skip "-<DISABLED>-" ... "-<ENABLED>-" between blocks;
//  * parameters are read verbatim, one per line, empty lines included.
//
// Text is decoded from the code page it was written in: a byte-order mark says so,
// otherwise detect() tells UTF-8, Windows-1251 (Russian mods), Windows-1250 (Polish,
// Czech) and the stock content's Windows-1252 apart.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace omsi::cfg {

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

// Which single-byte code page a file without a byte-order mark was written in.
enum class CodePage {
    Windows1252,  // the stock content
    Windows1250,  // Polish and Czech mods
    Windows1251,  // Russian mods
};

// Decide the code page of bytes that carry no BOM.
//
// Mirrors the Rust original: valid UTF-8 wins; then Windows-1251 when at least half of the
// letters in 0xC0.. stand in runs of three or more (Russian words - German has at most
// two in a row, as in "Grosse"); then Windows-1250 when letters that are signs in 1252
// (l-with-stroke, a-ogonek, s-acute and their capitals) stand next to letters;
// Windows-1252 otherwise.
CodePage detect(std::string_view bytes);

// Decode one byte of `cp` to UTF-8 and append it to `out`.
void appendDecoded(CodePage cp, std::uint8_t byte, std::string& out);

// Decode raw file bytes to UTF-8 using OMSI's rules: a UTF-16LE BOM (FF FE) wins, then a
// UTF-8 BOM (EF BB BF), then a single-byte code page chosen by detect().
std::string decodeText(std::string_view bytes);

// Encode to Windows-1252 the way OMSI writes its own files.
std::string encodeText(std::string_view text);

// Split into lines. Handles CRLF, LF and a bare CR.
std::vector<std::string> splitLines(std::string_view text);

// ---------------------------------------------------------------------------
// Lenient number parsing
// ---------------------------------------------------------------------------
//
// OMSI reads numbers with Delphi's StrToFloat / StrToInt and keeps their default of 0 when
// the text is not a number, so nothing here ever fails.

// Parse a float the way OMSI does: '.' as decimal separator, optional exponent, garbage
// yields 0. A trailing comment or unit after whitespace is ignored ("5 (metres)" -> 5).
double parseF64(std::string_view text);
float parseF32(std::string_view text);
std::int64_t parseI64(std::string_view text);
std::int32_t parseI32(std::string_view text);

// ---------------------------------------------------------------------------
// Keyword lines
// ---------------------------------------------------------------------------

// How a loader of the original tells a keyword line from free text.
enum class KeywordRule {
    // The line is exactly "[name]" with the original's spelling (almost every loader).
    Exact,
    // Trailing tabs, spaces, CR/LF and double quotes are cut off first (.hof, whose stock
    // files are spreadsheet exports: "[infosystem_trip]" followed by twelve tabs).
    TrimEnd,
    // The line is lower-cased first (ailists.cfg).
    AnyCase,
};

// Is `line` a keyword line? If so, returns the keyword as written, without brackets.
std::string_view keywordWith(std::string_view line, KeywordRule rule);
std::string_view keywordOf(std::string_view line);

// ---------------------------------------------------------------------------
// CfgFile
// ---------------------------------------------------------------------------

// A loaded text file, decoded to UTF-8 and split into lines.
class CfgFile {
public:
    CfgFile() = default;

    // Read from disk, throwing std::runtime_error when the file cannot be opened.
    static CfgFile read(const std::filesystem::path& path);

    // Build from bytes already in memory (this is how mounted archives are read).
    static CfgFile fromBytes(std::filesystem::path path, std::string_view bytes);

    // Build from text that is already UTF-8. Does not re-detect the code page.
    static CfgFile fromString(std::filesystem::path path, std::string_view text);

    [[nodiscard]] const std::vector<std::string>& lines() const noexcept { return lines_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    // Directory the file lives in, for resolving the relative paths inside it.
    [[nodiscard]] std::filesystem::path dir() const;

    [[nodiscard]] std::size_t lineCount() const noexcept { return lines_.size(); }
    [[nodiscard]] std::string_view line(std::size_t index) const;

private:
    std::filesystem::path path_;
    std::vector<std::string> lines_;
};

// ---------------------------------------------------------------------------
// CfgReader
// ---------------------------------------------------------------------------

// Sequential reader over a CfgFile with the original semantics.
//
// Parameters are consumed verbatim and in order; an empty line is a legal (empty)
// parameter, so use nextNonEmpty() when the stock files' blank separators would otherwise
// be read as a value.
class CfgReader {
public:
    explicit CfgReader(const CfgFile& file) noexcept;

    // Read keyword lines by `rule` instead of KeywordRule::Exact.
    CfgReader& withRule(KeywordRule rule) noexcept;

    // Skip "-<DISABLED>-" ... "-<ENABLED>-" between blocks, as the original's loaders of
    // .bus/.ovh, .sco, model.cfg, passengercabin.cfg, paths.cfg, .hum and envir.cfg do.
    // The marker lines must stand alone, exactly like that; a marker read as a parameter
    // stays a parameter. Mods switch whole meshes off this way.
    CfgReader& skippingDisabledBlocks(bool on = true) noexcept;

    // Advance to the next keyword line. `tokens` additionally accepts a line that is
    // exactly one of those bare tokens (e.g. "-<DISABLED>-").
    // Returns false at end of file.
    bool seek(std::vector<std::string_view> tokens = {});

    // The keyword the reader stopped at, without brackets, as written in the file.
    [[nodiscard]] std::string_view keyword() const noexcept { return keyword_; }

    // 0-based line number of the keyword `seek()` stopped at.
    [[nodiscard]] std::size_t keywordLine() const noexcept { return blockLine_; }

    // True when the current keyword matches `name`, compared without regard to case.
    [[nodiscard]] bool is(std::string_view name) const;

    // The next parameter line, verbatim. An empty line is a legal (empty) parameter.
    [[nodiscard]] std::string_view next();

    // The next parameter line that is not entirely whitespace.
    [[nodiscard]] std::string_view nextNonEmpty();

    [[nodiscard]] double nextFloat();
    [[nodiscard]] float nextF32();
    [[nodiscard]] std::int64_t nextI64();
    [[nodiscard]] std::int32_t nextI32();

    // 0-based index of the next line to be read.
    [[nodiscard]] std::size_t pos() const noexcept { return pos_; }

private:
    const CfgFile* file_;
    std::size_t pos_ = 0;
    std::size_t blockLine_ = 0;
    KeywordRule rule_ = KeywordRule::Exact;
    bool skipDisabled_ = false;
    std::string_view keyword_;
};

}  // namespace omsi::cfg