// SPDX-License-Identifier: MIT
//
// Reading the headers of a Windows PE executable. Nothing here writes.
//
// The launcher does not modify the game, so this module only *looks*. It answers the
// questions that decide whether an executable can run at all and how much room it has
// once it does:
//
//   - is this a PE image, and is it 32- or 64-bit?
//   - does it carry IMAGE_FILE_LARGE_ADDRESS_AWARE?
//
// The second one is the interesting one. A 32-bit process on 64-bit Windows is given a
// 2 GB user address space, or 4 GB when that flag is set. The loader reads the flag from
// the file when it creates the process, so it cannot be turned on at run time - and a
// game update overwrites it. That makes it worth reporting, not worth doing quietly.
//
// Everything is bounds-checked against the real file size: these headers come from
// whatever is on disk, so a truncated or hostile file must produce an error rather than
// a read past the end.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace omsi::pe {

// The IMAGE_FILE_MACHINE_* values worth naming. Anything else is kept in machineRaw
// rather than guessed at.
enum class Machine : std::uint16_t {
    Unknown = 0x0000,
    I386    = 0x014C,  // x86
    Amd64   = 0x8664,  // x86-64
    Arm64   = 0xAA64,  // ARM64
};

inline constexpr std::uint16_t kLargeAddressAware = 0x0020;
inline constexpr std::uint16_t kExecutableImage  = 0x0002;
inline constexpr std::uint16_t kDll              = 0x2000;

std::string_view machineName(Machine machine) noexcept;
std::string_view formatName(bool pe32Plus) noexcept;
std::string_view subsystemName(std::uint16_t subsystem) noexcept;

struct ImageInfo {
    bool valid = false;   // the file was read and is a PE image
    std::string error;    // why not, when valid is false
    std::filesystem::path path;

    Machine machine = Machine::Unknown;
    std::uint16_t machineRaw = 0;
    bool pe32Plus = false;
    std::uint16_t characteristics = 0;
    std::uint16_t subsystem = 0;
    std::uint32_t sizeOfImage = 0;  // bytes reserved when the image is mapped

    [[nodiscard]] bool largeAddressAware() const noexcept {
        return (characteristics & kLargeAddressAware) != 0;
    }
    [[nodiscard]] bool is64Bit() const noexcept {
        return machine == Machine::Amd64 || machine == Machine::Arm64;
    }
    [[nodiscard]] bool isDll() const noexcept { return (characteristics & kDll) != 0; }

    // "x86 (32-bit)" and friends, for a one-line report.
    [[nodiscard]] std::string describe() const;
};

// Reads the headers of `path`. Never throws for a bad file: an unreadable or malformed
// one comes back with valid == false and the reason in `error`.
ImageInfo inspect(const std::filesystem::path& path);

}  // namespace omsi::pe