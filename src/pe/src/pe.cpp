// SPDX-License-Identifier: MIT

#include "omsi/pe/pe.hpp"

#include <array>
#include <cstdio>
#include <fstream>

namespace omsi::pe {
namespace {

std::uint16_t le16(const unsigned char* p) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(p[0]) |
                                      (static_cast<std::uint32_t>(p[1]) << 8));
}

std::uint32_t le32(const unsigned char* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// Reads exactly `n` bytes at `offset`. Returns false at end of file or on error, which
// is what keeps a truncated header from being read past its end.
bool readAt(std::ifstream& file, std::uint64_t offset, unsigned char* out, std::size_t n) {
    if (!file.seekg(static_cast<std::streamoff>(offset))) {
        return false;
    }
    file.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(n));
    return file.gcount() == static_cast<std::streamsize>(n);
}

}  // namespace

std::string_view machineName(Machine machine) noexcept {
    switch (machine) {
        case Machine::I386:
            return "x86";
        case Machine::Amd64:
            return "x86-64";
        case Machine::Arm64:
            return "ARM64";
        case Machine::Unknown:
            break;
    }
    return "unknown";
}

std::string_view formatName(bool pe32Plus) noexcept {
    return pe32Plus ? "PE32+ (64-bit)" : "PE32 (32-bit)";
}

std::string_view subsystemName(std::uint16_t subsystem) noexcept {
    switch (subsystem) {
        case 2:
            return "Windows GUI";
        case 3:
            return "Windows console";
        case 1:
            return "Native";
        case 9:
            return "Windows CE";
        case 10:
            return "EFI application";
        default:
            break;
    }
    return "unknown";
}

std::string ImageInfo::describe() const {
    return std::string{machineName(machine)} + (is64Bit() ? " (64-bit)" : " (32-bit)");
}

ImageInfo inspect(const std::filesystem::path& path) {
    ImageInfo info;
    info.path = path;

    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        info.error = "not a regular file";
        return info;
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        info.error = "cannot determine the file size";
        return info;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        info.error = "cannot open the file";
        return info;
    }

    // The DOS header starts with "MZ" and holds the offset of the PE header at 0x3C.
    std::array<unsigned char, 64> dos{};
    if (!readAt(file, 0, dos.data(), dos.size())) {
        info.error = "file is too small to be a PE image";
        return info;
    }
    if (dos[0] != 'M' || dos[1] != 'Z') {
        info.error = "missing the MZ signature";
        return info;
    }

    const std::uint32_t peOffset = le32(dos.data() + 0x3C);
    if (peOffset + 24 > size) {
        info.error = "the PE header lies outside the file";
        return info;
    }

    // "PE\0\0", then the COFF header: machine, section count, timestamp, symbol table,
    // symbol count, size of the optional header, then the characteristics word.
    std::array<unsigned char, 24> coff{};
    if (!readAt(file, peOffset, coff.data(), coff.size())) {
        info.error = "cannot read the PE header";
        return info;
    }
    if (coff[0] != 'P' || coff[1] != 'E' || coff[2] != 0 || coff[3] != 0) {
        info.error = "missing the PE signature";
        return info;
    }

    info.machineRaw = le16(coff.data() + 4);
    info.machine = static_cast<Machine>(info.machineRaw);
    info.characteristics = le16(coff.data() + 22);

    const std::uint16_t optionalSize = le16(coff.data() + 20);
    if (optionalSize == 0) {
        info.error = "the image has no optional header";
        return info;
    }

    // The optional header carries the magic that tells 32-bit from 64-bit, plus the
    // subsystem and the size the loader reserves for the image.
    const std::uint64_t optional = peOffset + 24;
    std::array<unsigned char, 72> opt{};
    const std::size_t optWant = optionalSize < opt.size() ? optionalSize : opt.size();
    if (!readAt(file, optional, opt.data(), optWant)) {
        info.error = "cannot read the optional header";
        return info;
    }

    const std::uint16_t magic = le16(opt.data());
    if (magic == 0x20B) {
        info.pe32Plus = true;
    } else if (magic != 0x10B) {
        info.error = "unrecognised optional header magic";
        return info;
    }

    info.subsystem = le16(opt.data() + 68);
    info.sizeOfImage = le32(opt.data() + 56);
    info.valid = true;
    return info;
}

}  // namespace omsi::pe