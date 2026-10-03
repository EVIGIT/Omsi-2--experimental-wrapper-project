// SPDX-License-Identifier: MIT
//
// A read-only survey of how much content an installation actually holds.
//
// This exists to answer one question with a number instead of a guess: on a heavy install,
// is the game memory-bound, load-bound or CPU-bound? The answer decides whether a
// renderer change is worth building at all - a Vulkan D3DX9 (roadmap 4.1) buys nothing if
// the bottleneck is reading files off a disk.
//
// Nothing here writes, and nothing here launches the game. It only walks folders and sums
// sizes, so it is safe to point at any installation.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace omsi::content {

// What a whole extension costs across the installation.
struct ExtensionStat {
    std::string extension;  // lowercase, without the dot, e.g. "bus"
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
};

// One map's weight, so a heavy map can be told from a light one.
struct MapStat {
    std::string folderName;
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
    // Scenery models are *not* stored in the map folder. They live once in the shared
    // Sceneryobjects/ library and the map only references them, so this counts nothing for
    // a normal map - which is why the report prints a dash rather than a misleading zero.
    // Counting an install's models means walking Sceneryobjects/ instead.
    std::uint64_t objects = 0;
};

// Which folder the content sits in, and how big that folder's volume is. A spinning disk
// is a finding in its own right: it makes an install load-bound however good the rest is.
struct VolumeStat {
    std::string label;       // "E:" - the drive letter, or the folder when there is none
    std::string fileSystem;  // "NTFS", "ReFS", ...
    std::uint64_t volumeBytes = 0;
    std::uint64_t freeBytes = 0;
};

struct Profile {
    std::uint64_t totalFiles = 0;
    std::uint64_t totalBytes = 0;

    // Texture bytes are tracked separately because they dominate the memory budget far
    // more than their file count suggests.
    std::uint64_t textureBytes = 0;
    std::uint64_t textureFiles = 0;

    std::vector<ExtensionStat> extensions;  // sorted by bytes, largest first
    std::vector<MapStat> maps;              // sorted by bytes, largest first
    std::optional<VolumeStat> volume;
    std::vector<std::string> problems;      // folders that could not be read

    // A one-line judgement of what is most likely to be the ceiling. Deliberately blunt:
    // this is a starting point for investigation, not a benchmark.
    [[nodiscard]] std::string verdict() const;
};

// Walks the installation and sums it up. Never throws for an unreadable folder - those go
// into `problems`, because a partially-readable install is normal with mods.
Profile profile(const std::filesystem::path& root);

}  // namespace omsi::content