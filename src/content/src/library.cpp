// SPDX-License-Identifier: MIT

#include "omsi/content/library.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>

#include "omsi/cfg/cfg.hpp"
#include "omsi/core/log.hpp"

namespace omsi::content {
namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

// Lowercased ASCII narrowing of a path component. path::string() *throws* on a character the
// current code page cannot hold, and this installation really does have one (a euro sign in
// a vehicle folder name), so extension tests go through here rather than through .string().
std::string lower(const std::filesystem::path& p) {
    std::string out;
    for (const wchar_t w : p.wstring()) {
        const auto c = static_cast<std::uint32_t>(w);
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c < 0x80 ? c : '?'))));
    }
    return out;
}

// The extension, lowercase and without the dot.
std::string extensionOf(const std::filesystem::path& file) {
    std::string ext = lower(file.extension());
    if (!ext.empty() && ext.front() == '.') {
        ext.erase(0, 1);
    }
    return ext;
}

// The path in the file itself, as the game spells it. std::filesystem::path::string()
// throws when a character cannot be represented in the current code page, and mod folder
// names really do contain them (a euro sign, Cyrillic, umlauts). The name is converted to
// UTF-8 by hand so it stays readable; the exact bytes are still on disk, and the library
// keeps the path itself for anything that needs to open the file.
std::string displayName(const std::filesystem::path& path) {
    const std::filesystem::path leaf = path.filename();
    std::string out;
    for (const wchar_t w : leaf.wstring()) {
        const auto c = static_cast<std::uint32_t>(w);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            // Two-byte UTF-8.
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back('?');
        }
    }
    return out;
}

// "Vehicles/HOH_KI_Autos/HOH_DAF_XF_105/DAF_XF_105.bus" -> "HOH_DAF_XF_105"
// Only the folder immediately above the file is the manufacturer: a mod may nest its packs
// a few folders deep, and joining the whole relative path into a display name produced
// things like "HOH_KI_Autos/HOH_DAF_XF_105 DAF_XF_105".
std::string manufacturerOf(const std::filesystem::path& file) {
    return displayName(file.parent_path());
}

// Read the map's own name out of global.cfg. OMSI's stock files do not all set one, so
// the folder name stays the fallback and the title is only a nicer label.
//
// The key may be written "name=Berlin" or "name Berlin" depending on which tool wrote the
// file, so both forms are accepted; the value is everything after the key.
std::string titleFromGlobalCfg(const std::filesystem::path& globalCfg, std::string& problem) {
    try {
        const omsi::cfg::CfgFile file = omsi::cfg::CfgFile::read(globalCfg);
        omsi::cfg::CfgReader reader(file);
        while (reader.seek()) {
            if (!reader.is("general")) {
                continue;
            }
            // The [general] block holds the title under a key that starts with "name".
            for (int i = 0; i < 16; ++i) {
                const std::string_view line = reader.nextNonEmpty();
                if (line.empty()) {
                    break;
                }
                const std::string_view key = line.substr(0, line.find_first_of("= \t"));
                if (lower(key) != "name") {
                    continue;
                }
                // Everything past the key, with any '=' skipped.
                std::size_t valueStart = key.size();
                while (valueStart < line.size() &&
                       (line[valueStart] == '=' || line[valueStart] == ' ' ||
                        line[valueStart] == '\t')) {
                    ++valueStart;
                }
                const std::string_view value = line.substr(valueStart);
                const auto first = value.find_first_not_of(" \t");
                const auto last = value.find_last_not_of(" \t\r");
                if (first != std::string_view::npos && last >= first) {
                    return std::string(value.substr(first, last - first + 1));
                }
            }
        }
    } catch (const std::exception& e) {
        problem = e.what();
    }
    return {};
}

}  // namespace

std::string VehicleEntry::displayName() const {
    // OMSI shows "Manufacturer Model"; without a manufacturer (a loose file) just the
    // model, so an unpacked drop-in pack still reads sensibly.
    return manufacturer.empty() ? model : manufacturer + " " + model;
}

std::string TimetableEntry::displayName() const {
    return map.empty() ? name : map + " \xC2\xB7 " + name;
}

bool looksLikeInstall(const std::filesystem::path& root) {
    std::error_code e;
    if (!std::filesystem::is_directory(root, e)) {
        return false;
    }
    return std::filesystem::is_directory(root / "Maps", e) &&
           std::filesystem::is_directory(root / "Vehicles", e) &&
           std::filesystem::exists(root / "envir.cfg", e);
}

std::vector<std::filesystem::path> rootCandidates(const std::filesystem::path& programDir) {
    std::vector<std::filesystem::path> out;

    if (const char* env = std::getenv("OMSI_ROOT"); env != nullptr && *env != '\0') {
        out.emplace_back(env);
    }

    const std::filesystem::path base =
        programDir.empty() ? std::filesystem::current_path() : programDir;
    for (const char* name : {"OMSI 2", "OMSI 2 Original", "Omsi 2", "OMSI2", "OMSI"}) {
        out.push_back(base / name);
    }

    if (const char* pf86 = std::getenv("ProgramFiles(x86)"); pf86 != nullptr) {
        const std::filesystem::path pf(pf86);
        out.push_back(pf / "Steam/steamapps/common/OMSI 2");
        out.push_back(pf / "Steam/steamapps/common/OMSI 2 Original");
    }
    if (const char* pf = std::getenv("ProgramFiles"); pf != nullptr) {
        out.push_back(std::filesystem::path(pf) / "Steam/steamapps/common/OMSI 2");
    }
    return out;
}

std::optional<std::filesystem::path> findInstall(const std::filesystem::path& programDir) {
    for (const auto& candidate : rootCandidates(programDir)) {
        if (looksLikeInstall(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

Library::Library(std::filesystem::path root) : root_(std::move(root)) {
    if (!looksLikeInstall(root_)) {
        throw std::runtime_error("not an OMSI 2 installation: " + root_.string());
    }
    scan();
}

void Library::scan() {
    maps_.clear();
    vehicles_.clear();
    timetables_.clear();
    problems_.clear();
    gameImage_.reset();
    scanMaps();
    scanVehicles();
    scanTimetables();
    scanGameImage();
    omsi::core::info("library: {} maps, {} vehicles, {} timetables, {} problems", maps_.size(),
                     vehicles_.size(), timetables_.size(), problems_.size());
}

void Library::scanGameImage() {
    // The game is looked up by name rather than by path: the file sits in the root, but
    // case is not something a Windows install guarantees.
    std::error_code e;
    if (!std::filesystem::is_directory(root_, e)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(root_, e)) {
        if (e || !entry.is_regular_file()) {
            continue;
        }
        std::string name = entry.path().filename().string();
        std::ranges::transform(name, name.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name != "omsi.exe") {
            continue;
        }
        gameImage_ = pe::inspect(entry.path());
        if (!gameImage_->valid) {
            problems_.push_back(entry.path().filename().string() + ": " + gameImage_->error);
        }
        return;
    }
}

void Library::scanMaps() {
    const auto mapsDir = root_ / "Maps";
    std::error_code e;
    if (!std::filesystem::is_directory(mapsDir, e)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(mapsDir, e)) {
        if (e || !entry.is_directory()) {
            continue;
        }
        const auto globalCfg = entry.path() / "global.cfg";
        if (!std::filesystem::exists(globalCfg, e)) {
            // A folder under Maps without a global.cfg is not a playable map, which is
            // normal - expansion folders keep their sources elsewhere.
            continue;
        }
        MapEntry map;
        map.directory = entry.path();
        map.globalCfg = globalCfg;
        map.folderName = entry.path().filename().string();
        std::string problem;
        map.title = titleFromGlobalCfg(globalCfg, problem);
        if (!problem.empty()) {
            problems_.push_back(map.folderName + "/global.cfg: " + problem);
        }
        maps_.push_back(std::move(map));
    }
    std::ranges::sort(maps_, {}, &MapEntry::folderName);
}

void Library::scanVehicles() {
    const auto vehiclesDir = root_ / "Vehicles";
    std::error_code e;
    if (!std::filesystem::is_directory(vehiclesDir, e)) {
        return;
    }
    // A trailer (.ovh) is not something the player picks, so it is not listed.
    static constexpr std::string_view kKinds[] = {"bus", "tram", "zug", "train", "lugger"};
    for (const auto& entry : std::filesystem::recursive_directory_iterator(vehiclesDir, e)) {
        if (e || !entry.is_regular_file()) {
            continue;
        }
        const std::string ext = extensionOf(entry.path());
        if (!std::ranges::any_of(kKinds, [&](std::string_view k) { return ext == k; })) {
            continue;
        }
        VehicleEntry vehicle;
        vehicle.file = entry.path();
        // manufacturerOf and the model both go through detail::displayName: a path's
        // .string() throws on a character the current code page cannot hold, and mod
        // folders in this installation do contain one.
        vehicle.manufacturer = manufacturerOf(entry.path());
        vehicle.model = displayName(entry.path().stem());
        vehicle.kind = ext;
        vehicles_.push_back(std::move(vehicle));
    }
    std::ranges::sort(vehicles_, {}, &VehicleEntry::displayName);
}

void Library::scanTimetables() {
    // Timetable files belong to a *map*, not to a vehicle: a real installation keeps
    // hundreds of them under maps/<Map>/*.ttp. Looking for them under Vehicles (and
    // matching .ttf/.oft, which are fonts) finds nothing at all on a normal install.
    const auto mapsDir = root_ / "Maps";
    std::error_code e;
    if (!std::filesystem::is_directory(mapsDir, e)) {
        return;
    }
    // .ttp is a bus or tram line, .ttl a tram line, .ttr a train line.
    static constexpr std::string_view kKinds[] = {"ttp", "ttl", "ttr"};
    for (const auto& mapDir : std::filesystem::directory_iterator(mapsDir, e)) {
        if (e || !mapDir.is_directory()) {
            continue;
        }
        const std::string mapName = mapDir.path().filename().string();
        for (const auto& entry : std::filesystem::recursive_directory_iterator(mapDir.path(), e)) {
            if (e || !entry.is_regular_file()) {
                continue;
            }
            const std::string ext = extensionOf(entry.path());
            if (!std::ranges::any_of(kKinds, [&](std::string_view k) { return ext == k; })) {
                continue;
            }
            TimetableEntry timetable;
            timetable.file = entry.path();
            timetable.map = mapName;
            timetable.name = entry.path().stem().string();
            timetable.kind = ext;
            timetables_.push_back(std::move(timetable));
        }
    }
    std::ranges::sort(timetables_, {}, &TimetableEntry::displayName);
}

}  // namespace omsi::content