// SPDX-License-Identifier: MIT
//
// Auditing an OMSI 2 installation: what is unused, what is unreadable, what is broken.
//
// Two questions, both answered without writing anything:
//
//   1. Which files can never be opened? OMSI resolves a texture by *stem*, trying a
//      same-folder .dds before the exact name and then .bmp/.tga/.jpg/.png. So a .bmp
//      that has a .dds of the same stem in the same folder is unreachable - the game
//      never opens it. Design sources (.psd, .blend, .pdn) are never read at all.
//
//   2. Which content files fail to parse? This is the accuracy target every parser
//      in this project is measured against.
//
// The same-folder rule is not a simplification. Texture resolution walks a search path
// of folders, and a .dds in the *global* Texture/ folder does not make an object's
// local .bmp redundant: folder precedence is decided before the extension search runs.
// Seasonal (Texture\Spring\) and _LOW variants are separate lookups too, so a file with
// only a _LOW sibling is still used.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace omsi::content {

// Extensions OMSI will open as a texture, in the order the loader tries them.
extern const std::vector<std::string> kTextureExtensions;

// Design formats the game never reads. Safe to remove from a *content* folder - never
// from the installation root, which holds documentation and the updater.
extern const std::vector<std::string> kDesignExtensions;

// Why a file is considered removable. The distinction matters: a redundant texture is
// provably never opened, whereas a design source is merely unused today.
enum class RedundantReason {
    TextureShadowedByDds,  // a same-folder .dds of the same stem wins the lookup
    DesignSource,          // .psd/.blend/... - never read by the game at all
};

// Returns const char* rather than std::string_view: doctest compares failure messages with
// operator+, which string_view does not support.
const char* toString(RedundantReason reason) noexcept;

struct RedundantFile {
    std::filesystem::path file;
    RedundantReason reason = RedundantReason::DesignSource;
    std::uint64_t bytes = 0;
    // For a shadowed texture: the .dds that takes its place.
    std::filesystem::path shadowedBy;
};

struct ParseFailure {
    std::filesystem::path file;
    std::string reason;
};

// The outcome of walking an installation.
struct Audit {
    std::uint64_t filesScanned = 0;
    std::uint64_t bytesScanned = 0;

    std::vector<RedundantFile> redundant;
    std::vector<ParseFailure> parseFailures;
    std::vector<std::string> unreadable;  // folders that could not be walked

    [[nodiscard]] std::uint64_t reclaimableBytes() const noexcept;
    [[nodiscard]] std::uint64_t reclaimableTextureBytes() const noexcept;

    // "120 files, 38.40 GiB" style summary, largest bucket first.
    [[nodiscard]] std::string reclaimableSummary() const;
    // Counts of what was read and what failed, as a fraction.
    [[nodiscard]] std::string coverageSummary(std::uint64_t parsableFiles) const;
};

// Walks `root` read-only. Never throws for a bad file: problems are collected.
Audit audit(const std::filesystem::path& root);

// ---------------------------------------------------------------------------
// Cleaning
// ---------------------------------------------------------------------------
//
// Cleaning *moves* files, it does not delete them. A shadowed texture is genuinely
// redundant, but a design source is somebody's working file: it exists nowhere else and
// the mod author may well want it back. Relocating keeps the installation lean while
// leaving every byte recoverable, so undoing a clean is a move in the other direction.
//
// Nothing here runs without --clean, and nothing here picks a destination on its own.

// Why a given file is or is not eligible to be moved out.
enum class Cleanable : std::uint8_t {
    Yes,               // provably unreachable, or a format the game never reads
    OutsideContentDir, // a design format outside Maps/Vehicles/Sceneryobjects
    DesignSource,      // eligible, but only with --include-design explicitly asked for
    NotCleanable,      // anything else
};

struct CleanDecision {
    Cleanable verdict = Cleanable::NotCleanable;
    // For DesignSource when includeDesign is false; otherwise the reason it is eligible.
    std::string reason;
};

// Decides on one file. Kept separate from the moving so the decision can be tested, and
// so `--dry-run` and a real run use exactly the same rule.
CleanDecision decideClean(const RedundantFile& file, bool includeDesign);

// Where a file would go. The relative path is kept under the destination so two mods with
// a file of the same name cannot collide, and a name that already exists gets a counter.
struct CleanPlan {
    std::vector<RedundantFile> moved;
    std::vector<std::string> skipped;
    std::uint64_t bytesMoved = 0;
};

// Computes what a clean would do, touching nothing.
CleanPlan planClean(const Audit& a, bool includeDesign, std::uint32_t limit);

// Performs the plan: every file is moved to `destination`, preserving the relative path
// from the installation root. On the first failure it stops and returns what it did, so
// nothing is left half-done without saying so.
CleanPlan applyClean(const Audit& a, const std::filesystem::path& installRoot,
                     const std::filesystem::path& destination, bool includeDesign,
                     std::uint32_t limit);

// Undoes a clean: every file under `destination` that belongs to an installation goes back
// to its original path, creating parent folders as needed. Files that would overwrite
// something are skipped and reported rather than replacing it.
CleanPlan applyRestore(const std::filesystem::path& destination,
                       std::uint32_t limit);

}  // namespace omsi::content