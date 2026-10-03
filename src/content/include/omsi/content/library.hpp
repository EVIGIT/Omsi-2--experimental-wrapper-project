// SPDX-License-Identifier: MIT
//
// Reading an OMSI 2 installation: what maps, vehicles and timetable files it holds.
//
// This is the data side of the launcher. It parses files and never renders anything, so
// it has no dependency on Vulkan and is fully testable on its own.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "omsi/pe/pe.hpp"

namespace omsi::content {

// A map folder under <root>/Maps that holds a global.cfg.
struct MapEntry {
    std::filesystem::path directory;
    std::filesystem::path globalCfg;
    std::string folderName;  // as installed, e.g. "Berlin"
    std::string title;       // the [general] title when global.cfg names one
};

// A vehicle definition: one .bus, .ovh or .lugger/.tram/.zug file under Vehicles.
struct VehicleEntry {
    std::filesystem::path file;
    std::string manufacturer;  // the folder under Vehicles
    std::string model;         // the file's stem, e.g. "MAN_D92"
    std::string kind;          // "bus", "ovh", "tram", "train"

    [[nodiscard]] std::string displayName() const;
};

// A timetable file. OMSI keeps these *inside the map folder* (maps/<Map>/*.ttp), not
// beside the vehicle: .ttp is a bus or tram line, .ttl a tram line, .ttr a train line.
// (The .ttf and .oft that also turn up in an installation are TrueType and OMSI font
// files - an easy and expensive mistake to make.)
struct TimetableEntry {
    std::filesystem::path file;
    std::string map;     // the map folder it belongs to, e.g. "Berlin-Spandau"
    std::string name;    // the file's stem, e.g. "R5_Jh-Nauen"
    std::string kind;    // "ttp", "ttl", "ttr"

    [[nodiscard]] std::string displayName() const;
};

// Does this folder look like an OMSI 2 installation? It needs the Maps and Vehicles
// folders and an envir.cfg, which is what the original has in a complete installation.
bool looksLikeInstall(const std::filesystem::path& root);

// Folders to try, in the order the launcher offers them: $OMSI_ROOT, the folder beside
// the program, then the usual Steam and GOG locations.
std::vector<std::filesystem::path> rootCandidates(const std::filesystem::path& programDir);

// The first candidate that looks like an installation.
std::optional<std::filesystem::path> findInstall(const std::filesystem::path& programDir);

// Everything the launcher lists in its three main pickers.
class Library {
public:
    // Throws std::runtime_error when `root` is not an installation.
    explicit Library(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

    // Scans the installation. Safe to call again after mods were installed.
    void scan();

    [[nodiscard]] const std::vector<MapEntry>& maps() const noexcept { return maps_; }
    [[nodiscard]] const std::vector<VehicleEntry>& vehicles() const noexcept { return vehicles_; }
    [[nodiscard]] const std::vector<TimetableEntry>& timetables() const noexcept {
        return timetables_;
    }

    // Files that could not be read, with the reason. An installation with unreadable
    // files is normal - a mod may be half-installed - so these are reported, not fatal.
    [[nodiscard]] const std::vector<std::string>& problems() const noexcept {
        return problems_;
    }

    // The game executable and what its PE headers say, when the folder holds one.
    //
    // This is a *report*, never a change: Omsi.exe is 32-bit and stays that way, because
    // it loads 32-bit-only libraries (d3dx9.dll, steam_api.dll) that a 64-bit process
    // cannot load at all. What the user can act on is largeAddressAware() - the flag
    // that decides between a 2 GB and a 4 GB address space, and that a game update
    // silently clears. Empty when there is no Omsi.exe to look at.
    [[nodiscard]] const std::optional<pe::ImageInfo>& gameImage() const noexcept {
        return gameImage_;
    }

private:
    void scanMaps();
    void scanVehicles();
    void scanTimetables();
    void scanGameImage();

    std::filesystem::path root_;
    std::vector<MapEntry> maps_;
    std::vector<VehicleEntry> vehicles_;
    std::vector<TimetableEntry> timetables_;
    std::vector<std::string> problems_;
    std::optional<pe::ImageInfo> gameImage_;
};

}  // namespace omsi::content