// SPDX-License-Identifier: MIT

#include "omsi/content/profile.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>

#include <windows.h>

#include "omsi/core/log.hpp"

namespace omsi::content {
namespace {

// Image formats a scenery object or vehicle can carry. Counted separately because these
// are what the memory budget is actually spent on.
bool isTexture(std::string_view ext) {
    return ext == "dds" || ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" ||
           ext == "tga";
}

std::string lower(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Sums one folder tree into `exts`, `files` and `bytes`. Recursion is on the stack rather
// than through a recursive_directory_iterator so that a deep mod folder cannot blow the
// stack, and so an unreadable subfolder can be skipped without ending the whole walk.
void walk(const std::filesystem::path& dir, std::map<std::string, ExtensionStat>& exts,
          std::uint64_t& files, std::uint64_t& bytes, std::uint64_t& textureBytes,
          std::uint64_t& textureFiles, std::vector<std::string>& problems,
          std::uint64_t& objects) {
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) {
        problems.push_back(dir.string() + ": " + ec.message());
        return;
    }

    for (const auto& entry : it) {
        std::error_code inner;
        if (entry.is_directory(inner)) {
            walk(entry.path(), exts, files, bytes, textureBytes, textureFiles, problems,
                 objects);
            continue;
        }
        if (!entry.is_regular_file(inner)) {
            continue;
        }

        const auto size = entry.file_size(inner);
        if (inner) {
            continue;
        }

        std::string ext = lower(entry.path().extension().string());
        if (!ext.empty() && ext.front() == '.') {
            ext.erase(0, 1);
        }

        ++files;
        bytes += size;

        auto& stat = exts[ext];
        stat.extension = ext;
        stat.files += 1;
        stat.bytes += size;

        if (isTexture(ext)) {
            textureBytes += size;
            textureFiles += 1;
        }
        // A scenery object compiles to one .o3d model plus its textures, and that is what
        // the game loads. (.obj here is a Wavefront *source* mesh from a Blender export -
        // only 25 of them exist in a whole install - so counting those as objects reports
        // zero for every map and says nothing about load weight.)
        if (ext == "o3d") {
            ++objects;
        }
    }
}
// The volume the installation sits on, when it has one. Uses the Windows API because
// std::filesystem cannot report free space for a volume.
std::optional<VolumeStat> volumeOf(const std::filesystem::path& root) {
    const auto rootName = std::filesystem::path(root).root_name();
    if (rootName.empty()) {
        return std::nullopt;
    }

    VolumeStat stat;
    stat.label = rootName.string();

    wchar_t label[MAX_PATH + 4]{};
    wchar_t fs[MAX_PATH]{};
    DWORD flags = 0;
    if (!GetVolumeInformationW(rootName.c_str(), label, MAX_PATH, nullptr, nullptr, &flags, fs,
                               MAX_PATH)) {
        return std::nullopt;
    }
    if (fs[0] != L'\0') {
        // Neither wchar_t[MAX_PATH] nor std::wstring converts to std::string implicitly.
        // std::filesystem::path is the one that does the wide-to-narrow conversion in the
        // right code page, and its wchar_t* constructor stops at the terminator.
        stat.fileSystem = std::filesystem::path(fs).string();
    }

    ULARGE_INTEGER available{};
    ULARGE_INTEGER total{};
    ULARGE_INTEGER freeBytes{};
    if (GetDiskFreeSpaceExW(rootName.c_str(), &available, &total, &freeBytes)) {
        stat.volumeBytes = total.QuadPart;
        stat.freeBytes = freeBytes.QuadPart;
    }
    return stat;
}

std::string gib(std::uint64_t bytes) {
    return std::format("{:.2f} GiB",
                       static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
}

}  // namespace

// Reads OMSI's [texmemlimit] from options.cfg: the texture memory in MB above which the
// game lowers the resolution of distant textures. Stock is 401; the value is meaningless
// without also knowing how much address space the process has, so it is reported next to
// the image rather than on its own.
//
// Public, so it lives outside the anonymous namespace above.
std::optional<double> textureMemoryLimit(const std::filesystem::path& root) {
    std::ifstream in(root / "options.cfg", std::ios::binary);
    if (!in) {
        return std::nullopt;
    }

    std::string line;
    while (std::getline(in, line)) {
        // A block keyword on its own line. Compared as a whole trimmed line so that a
        // longer keyword cannot match this one.
        while (!line.empty() &&
               (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line.substr(first) != "[texmemlimit]") {
            continue;
        }
        // The value is the next line: OMSI writes one parameter per line, and an empty line
        // is a legal empty parameter rather than the end of the block.
        if (!std::getline(in, line)) {
            return std::nullopt;
        }
        try {
            return std::stod(line);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::string Profile::verdict() const {
    // The thresholds are deliberately rough. A verdict here is a hypothesis to test, and
    // the way to test it is to remove one thing and see whether the game gets better.
    if (totalBytes == 0) {
        return "nothing to measure";
    }

    constexpr std::uint64_t kGiB = 1024ull * 1024 * 1024;
    const bool heavy = totalBytes >= 20 * kGiB;
    const bool textureHeavy = textureBytes * 2 >= totalBytes;

    if (textureHeavy && heavy) {
        return "memory-bound: textures dominate. A renderer change will not help much; "
               "thinning what loads will.";
    }
    if (heavy) {
        return "load-bound: a lot of content. Check the disk first, then thin the content.";
    }
    return "not obviously content-bound. A renderer change is worth measuring.";
}

Profile profile(const std::filesystem::path& root) {
    Profile result;

    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec) {
        result.problems.push_back(root.string() + ": not a folder");
        return result;
    }

    std::map<std::string, ExtensionStat> exts;
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
    std::uint64_t objects = 0;

    walk(root, exts, files, bytes, result.textureBytes, result.textureFiles, result.problems,
         objects);

    result.totalFiles = files;
    result.totalBytes = bytes;
    // std::map holds pair<const string, ExtensionStat>, which will not convert to a
    // vector of ExtensionStat, so the values are copied across by hand.
    result.extensions.reserve(exts.size());
    for (const auto& [name, stat] : exts) {
        ExtensionStat copy = stat;
        copy.extension = name;
        result.extensions.push_back(std::move(copy));
    }
    std::ranges::sort(result.extensions, [](const ExtensionStat& a, const ExtensionStat& b) {
        if (a.bytes != b.bytes) {
            return a.bytes > b.bytes;
        }
        return a.extension < b.extension;
    });

    // Per-map weight, so a heavy map can be told from a light one. Textures are counted
    // per map here but deliberately not added to the installation total again - the walk
    // above has already seen them, and double counting would inflate the verdict.
    const auto mapsDir = root / "Maps";
    std::error_code mapEc;
    if (std::filesystem::is_directory(mapsDir, mapEc)) {
        for (const auto& entry : std::filesystem::directory_iterator(mapsDir, mapEc)) {
            std::error_code inner;
            if (!entry.is_directory(inner)) {
                continue;
            }
            MapStat stat;
            stat.folderName = entry.path().filename().string();

            std::map<std::string, ExtensionStat> mapExts;
            std::uint64_t mapFiles = 0;
            std::uint64_t mapBytes = 0;
            std::uint64_t mapObjects = 0;
            std::uint64_t mapTextures = 0;
            std::uint64_t mapTextureFiles = 0;
            std::vector<std::string> mapProblems;
            walk(entry.path(), mapExts, mapFiles, mapBytes, mapTextures, mapTextureFiles,
                 mapProblems, mapObjects);

            stat.files = mapFiles;
            stat.bytes = mapBytes;
            stat.objects = mapObjects;
            result.maps.push_back(std::move(stat));
        }
    }
    std::ranges::sort(result.maps, [](const MapStat& a, const MapStat& b) {
        return a.bytes > b.bytes;
    });

    result.volume = volumeOf(root);

    omsi::core::info("profile: {} files, {} across {} maps", files, gib(bytes),
                     result.maps.size());
    return result;
}

}  // namespace omsi::content