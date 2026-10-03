// SPDX-License-Identifier: MIT
//
// Reading PE headers. The fixtures are built byte by byte rather than copied from a real
// executable, so the expected values are stated here instead of inherited from whatever a
// compiled file happens to contain today.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <windows.h>

#include <doctest/doctest.h>

#include "omsi/pe/pe.hpp"

namespace {

// Offsets inside a PE image, named so the tests read like the format they describe.
// The ones that matter are relative to e_lfanew, or to the optional header.
constexpr std::size_t kMachine         = 0x04;
constexpr std::size_t kSizeOfOptHdr    = 0x14;
constexpr std::size_t kCharacteristics = 0x16;
constexpr std::size_t kOptMagic        = 0x00;
constexpr std::size_t kSizeOfImage     = 0x38;
constexpr std::size_t kSubsystem       = 0x44;

void put16(std::vector<unsigned char>& b, std::size_t at, std::uint16_t v) {
    b[at] = static_cast<unsigned char>(v & 0xFF);
    b[at + 1] = static_cast<unsigned char>((v >> 8) & 0xFF);
}

void put32(std::vector<unsigned char>& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<unsigned char>(v & 0xFF);
    b[at + 1] = static_cast<unsigned char>((v >> 8) & 0xFF);
    b[at + 2] = static_cast<unsigned char>((v >> 16) & 0xFF);
    b[at + 3] = static_cast<unsigned char>((v >> 24) & 0xFF);
}

// A minimal but structurally complete PE image of the requested width.
std::vector<unsigned char> makeImage(std::uint16_t machine, std::uint16_t characteristics,
                                    bool pe32Plus) {
    constexpr std::size_t kPeOffset = 0x80;
    constexpr std::size_t kOptOffset = kPeOffset + 24;
    constexpr std::size_t kTotal = kOptOffset + 128;

    std::vector<unsigned char> bytes(kTotal, 0);

    bytes[0] = 'M';
    bytes[1] = 'Z';
    // e_lfanew lives at 0x3C in the DOS header and points at the PE header. Getting this
    // wrong silently shifts every offset read after it.
    put32(bytes, 0x3C, static_cast<std::uint32_t>(kPeOffset));

    bytes[kPeOffset] = 'P';
    bytes[kPeOffset + 1] = 'E';
    put16(bytes, kPeOffset + kMachine, machine);
    put16(bytes, kPeOffset + kSizeOfOptHdr, 240);  // a normal PE32 optional header
    put16(bytes, kPeOffset + kCharacteristics, characteristics);

    put16(bytes, kOptOffset + kOptMagic, pe32Plus ? 0x20B : 0x10B);
    put32(bytes, kOptOffset + kSizeOfImage, 0x00400000);
    put16(bytes, kOptOffset + kSubsystem, 2);  // Windows GUI

    return bytes;
}

// Writes bytes to a temporary file that is removed again when the guard goes out of scope.
struct TempImage {
    std::filesystem::path path;

    explicit TempImage(const std::vector<unsigned char>& bytes) {
        static int counter = 0;
        path = std::filesystem::temp_directory_path() /
               ("evigit-pe-" + std::to_string(counter++) + ".bin");
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }

    ~TempImage() {
        std::error_code e;
        std::filesystem::remove(path, e);
    }

    TempImage(const TempImage&) = delete;
    TempImage& operator=(const TempImage&) = delete;
};

TempImage write(const std::vector<unsigned char>& bytes) {
    return TempImage(bytes);
}

}  // namespace

TEST_CASE("a 32-bit image without the flag reports a 2 GB limit") {
    // 0x818E is what an ordinary Delphi-built game carries: executable, line numbers
    // stripped, local symbols stripped, 32-bit machine, reversed bytes.
    const auto info = omsi::pe::inspect(write(makeImage(0x014C, 0x818E, false)).path);

    REQUIRE(info.valid);
    CHECK(info.error.empty());
    CHECK(info.machine == omsi::pe::Machine::I386);
    CHECK_FALSE(info.is64Bit());
    CHECK_FALSE(info.pe32Plus);
    CHECK_FALSE(info.largeAddressAware());
    CHECK(info.characteristics == 0x818E);
    CHECK(info.subsystem == 2);
    CHECK(omsi::pe::subsystemName(info.subsystem) == "Windows GUI");
    CHECK(info.sizeOfImage == 0x00400000);
}
TEST_CASE("the large-address-aware flag is bit 0x20 of the characteristics") {
    const auto info = omsi::pe::inspect(
        write(makeImage(0x014C, 0x818E | omsi::pe::kLargeAddressAware, false)).path);

    REQUIRE(info.valid);
    CHECK(info.largeAddressAware());
    // The rest of the word must survive: only the one bit is added.
    CHECK((info.characteristics & ~omsi::pe::kLargeAddressAware) == 0x818E);
    CHECK_FALSE(info.is64Bit());
}

TEST_CASE("a 64-bit image is reported as 64-bit") {
    const auto info = omsi::pe::inspect(write(makeImage(0x8664, 0x0022, true)).path);

    REQUIRE(info.valid);
    CHECK(info.machine == omsi::pe::Machine::Amd64);
    CHECK(info.is64Bit());
    CHECK(info.pe32Plus);
    // 0x0022 is executable + large-address-aware, which every 64-bit PE has.
    CHECK(info.largeAddressAware());
    CHECK(info.describe() == "x86-64 (64-bit)");
}

TEST_CASE("a DLL is told apart from an executable") {
    const auto info = omsi::pe::inspect(write(makeImage(0x014C, 0x2000 | 0x0002, false)).path);

    REQUIRE(info.valid);
    CHECK(info.isDll());
}

TEST_CASE("ARM64 is recognised") {
    const auto info = omsi::pe::inspect(write(makeImage(0xAA64, 0x0022, true)).path);

    REQUIRE(info.valid);
    CHECK(info.machine == omsi::pe::Machine::Arm64);
    CHECK(info.is64Bit());
}

TEST_CASE("a file that is not an executable is reported, not thrown") {
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "evigit-does-not-exist.exe";
    const auto info = omsi::pe::inspect(missing);

    CHECK_FALSE(info.valid);
    CHECK_FALSE(info.error.empty());
}

TEST_CASE("a file without an MZ header is rejected") {
    const std::vector<unsigned char> junk(512, 0x41);
    const auto info = omsi::pe::inspect(write(junk).path);

    CHECK_FALSE(info.valid);
    CHECK(info.error == "missing the MZ signature");
}

TEST_CASE("a file too small to hold a DOS header is rejected") {
    const std::vector<unsigned char> tiny{'M', 'Z'};
    const auto info = omsi::pe::inspect(write(tiny).path);

    CHECK_FALSE(info.valid);
    CHECK(info.error == "file is too small to be a PE image");
}

TEST_CASE("a truncated PE header is rejected rather than read past the end") {
    // MZ is present and e_lfanew points beyond the end of the file. A reader that trusted
    // the offset would read off the end of the buffer here.
    std::vector<unsigned char> bytes(128, 0);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    put32(bytes, 0x3C, 0x7FFFFFFF);

    const auto info = omsi::pe::inspect(write(bytes).path);

    CHECK_FALSE(info.valid);
    CHECK(info.error == "the PE header lies outside the file");
}

TEST_CASE("a bad PE signature is rejected") {
    auto bytes = makeImage(0x014C, 0x818E, false);
    bytes[0x80] = 'X';  // break "PE\0\0"

    const auto info = omsi::pe::inspect(write(bytes).path);

    CHECK_FALSE(info.valid);
    CHECK(info.error == "missing the PE signature");
}

TEST_CASE("an unrecognised optional header magic is rejected") {
    auto bytes = makeImage(0x014C, 0x818E, false);
    put16(bytes, 0x80 + 24 + kOptMagic, 0x1234);

    const auto info = omsi::pe::inspect(write(bytes).path);

    CHECK_FALSE(info.valid);
    CHECK(info.error == "unrecognised optional header magic");
}

TEST_CASE("the reader handles a real 64-bit executable") {
    // The test binary itself, which guards against the offsets being right for the
    // synthetic fixtures only.
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);

    const auto info = omsi::pe::inspect(std::filesystem::path(buffer));

    REQUIRE(info.valid);
    CHECK(info.is64Bit());
    CHECK(info.largeAddressAware());
}