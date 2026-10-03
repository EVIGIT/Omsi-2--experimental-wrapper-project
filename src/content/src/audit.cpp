// SPDX-License-Identifier: MIT

#include "omsi/content/audit.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "omsi/cfg/cfg.hpp"
#include "omsi/core/log.hpp"

namespace omsi::content {

const std::vector<std::string> kTextureExtensions{"dds", "bmp", "tga", "jpg", "jpeg", "png"};

const std::vector<std::string> kDesignExtensions{"psd",  "psb",  "pdn",  "blend", "blend1",
                                                "blend2", "skb", "xcf",  "c4d",  "afphoto",
                                                "kra",  "clip", "ora",  "mdp",  "xcf2"};

// Folders whose contents the game loads, lowercase (the comparison lowercases the other
// side). A design source in one of these is removable; the same extension in the
// installation root is a document or part of the updater, and is never touched.
const std::vector<std::string> kContentFolders{"maps", "vehicles", "sceneryobjects"};

namespace {

// A path component narrowed to lowercase without throwing. path::string() *throws* when a
// character cannot be represented in the current code page, and this installation has a
// vehicle folder with a euro sign in it - so every comparison below has to go through here.
std::string lower(const std::filesystem::path& path) {
    std::string out;
    for (const wchar_t w : path.wstring()) {
        const auto c = static_cast<std::uint32_t>(w);
        const char byte = c < 0x80 ? static_cast<char>(c)
                                   : static_cast<char>('?');  // ASCII-fold what we cannot keep
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(byte))));
    }
    return out;
}

std::string lower(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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

bool contains(const std::vector<std::string>& list, std::string_view value) {
    return std::ranges::any_of(list, [&](const std::string& s) { return s == value; });
}

// True when the path sits under one of the game's content folders.
bool underContentFolder(const std::filesystem::path& root, const std::filesystem::path& file) {
    // Compare against the root rather than walking components: the file may be reached by
    // a different spelling of the same path, and std::filesystem::relative is the one
    // function that normalises both.
    std::error_code ec;
    auto relative = std::filesystem::relative(file.parent_path(), root, ec);
    if (ec || relative.empty()) {
        return false;
    }
    const auto first = *relative.begin();
    // Both sides lowercased: the comparison is case-insensitive, and lowercasing only one
    // side made "sceneryobjects" fail to match "Sceneryobjects" for every file.
    return contains(kContentFolders, lower(first));
}

// Whether the file's folder - or the one above it - is the seasonal Texture\Spring style
// folder. A seasonal texture is a separate lookup and must not be judged against a file
// from another folder.
bool inSeasonalFolder(const std::filesystem::path& file) {
    const auto parent = lower(file.parent_path().filename());
    return parent == "texture" || parent == "spring" || parent == "summer" ||
           parent == "autumn" || parent == "winter";
}

// A texture the game can open.
bool isTexture(const std::string& ext) {
    return contains(kTextureExtensions, ext);
}

// A _LOW variant is a separate resolution target, so it is never redundant against the
// full-size file: the game asks for both by name.
bool isLowVariant(const std::filesystem::path& file) {
    const std::string stem = lower(file.stem());
    return stem.size() > 4 && stem.ends_with("_low");
}

std::string gib(std::uint64_t bytes) {
    return std::format("{:.2f} GiB",
                       static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
}

// Collects every candidate texture path under the installation, plus counts every file as
// it goes. `std::filesystem::path` is copied rather than referenced so nothing dangles
// when the iterator moves on.
void walkFolder(const std::filesystem::path& dir, const std::filesystem::path& root,
                Audit& audit, std::vector<std::filesystem::path>& textures) {
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) {
        // A folder that cannot be walked is reported, not fatal: a half-installed mod is
        // normal and the rest of the installation is still worth auditing.
        audit.unreadable.push_back(ec.message());
        return;
    }

    for (const auto& entry : it) {
        std::error_code inner;
        if (entry.is_directory(inner)) {
            walkFolder(entry.path(), root, audit, textures);
            continue;
        }
        if (!entry.is_regular_file(inner)) {
            continue;
        }
        const auto size = entry.file_size(inner);
        if (inner) {
            continue;
        }

        ++audit.filesScanned;
        audit.bytesScanned += size;

        const std::string ext = extensionOf(entry.path());
        const bool designSource = contains(kDesignExtensions, ext);

        if (designSource) {
            // Only inside a content folder. The same extension in the root is a document.
            if (underContentFolder(root, entry.path())) {
                audit.redundant.push_back(
                    RedundantFile{entry.path(), RedundantReason::DesignSource, size, {}});
            }
            continue;
        }

        // A texture the game could open, and one whose lookup is decided by a plain
        // same-folder stem search.
        if (isTexture(ext) && ext != "dds" && !isLowVariant(entry.path()) &&
            !inSeasonalFolder(entry.path()) && underContentFolder(root, entry.path())) {
            textures.push_back(entry.path());
        }
    }
}

}  // namespace

// Returns const char* rather than std::string_view: doctest compares failure messages with
// operator+, which string_view does not support.
const char* toString(RedundantReason reason) noexcept {
    switch (reason) {
        case RedundantReason::TextureShadowedByDds:
            return "shadowed by a same-folder .dds";
        case RedundantReason::DesignSource:
            return "design source, never read by the game";
    }
    return "unknown";
}

std::uint64_t Audit::reclaimableBytes() const noexcept {
    std::uint64_t total = 0;
    for (const auto& r : redundant) {
        total += r.bytes;
    }
    return total;
}

std::uint64_t Audit::reclaimableTextureBytes() const noexcept {
    std::uint64_t total = 0;
    for (const auto& r : redundant) {
        if (r.reason == RedundantReason::TextureShadowedByDds) {
            total += r.bytes;
        }
    }
    return total;
}

std::string Audit::reclaimableSummary() const {
    std::uint64_t textures = 0;
    std::uint64_t design = 0;
    for (const auto& r : redundant) {
        if (r.reason == RedundantReason::TextureShadowedByDds) {
            ++textures;
        } else {
            ++design;
        }
    }
    return std::format("{} files, {} ({} textures + {} design sources)", redundant.size(),
                       gib(reclaimableBytes()), gib(reclaimableTextureBytes()),
                       gib(reclaimableBytes() - reclaimableTextureBytes())) +
           std::format("; {} shadowed textures", textures) +
           (design == 0 ? "" : std::format("; {} design sources", design));
}

std::string Audit::coverageSummary(std::uint64_t parsableFiles) const {
    if (parsableFiles == 0) {
        return "no parsable content files were found";
    }
    const auto failed = static_cast<double>(parseFailures.size());
    const auto total = static_cast<double>(parsableFiles);
    return std::format("{}/{} content files parsed, {} failed ({:.2f}%)", parsableFiles,
                       parsableFiles, parseFailures.size(), failed * 100.0 / total);
}

Audit audit(const std::filesystem::path& root) {
    Audit result;

    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec) {
        result.unreadable.push_back("the given folder cannot be read");
        return result;
    }

    std::vector<std::filesystem::path> textures;
    walkFolder(root, root, result, textures);

    // Shadow detection: a same-folder .dds of the same stem wins the lookup, so the other
    // file can never be opened. One probe per candidate rather than one per extension.
    std::size_t shadowed = 0;
    for (const auto& texture : textures) {
        std::error_code inner;
        // Built from the wide stem: stem().string() throws on a name the code page cannot
        // hold, and this loop runs on every candidate texture in the installation.
        const auto dds = texture.parent_path() / (texture.stem().wstring() + L".dds");
        if (!std::filesystem::is_regular_file(dds, inner) || inner) {
            continue;
        }
        const auto size = std::filesystem::file_size(texture, inner);
        if (inner) {
            continue;
        }
        result.redundant.push_back(
            RedundantFile{texture, RedundantReason::TextureShadowedByDds, size, dds});
        ++shadowed;
    }

    std::ranges::sort(result.redundant, [](const RedundantFile& a, const RedundantFile& b) {
        if (a.bytes != b.bytes) {
            return a.bytes > b.bytes;
        }
        return a.file < b.file;
    });

    omsi::core::info("audit: {} files scanned, {} redundant ({} shadowed textures)",
                     result.filesScanned, gib(result.reclaimableBytes()), shadowed);
    return result;
}

// ---------------------------------------------------------------------------
// Cleaning
// ---------------------------------------------------------------------------

// A file name as plain ASCII, for a message. Skipped is a vector<std::string>, and a wide
// string does not concatenate with a narrow one; anything unrepresentable becomes '?'.
std::string narrowName(const std::filesystem::path& p) {
    std::string out;
    for (const wchar_t w : p.filename().wstring()) {
        const auto c = static_cast<std::uint32_t>(w);
        out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return out;
}

CleanDecision decideClean(const RedundantFile& file, bool includeDesign) {
    switch (file.reason) {
        case RedundantReason::TextureShadowedByDds:
            return {Cleanable::Yes, "a same-folder .dds of the same stem wins the lookup"};
        case RedundantReason::DesignSource:
            if (includeDesign) {
                return {Cleanable::Yes, "a design format the game never reads"};
            }
            return {Cleanable::DesignSource,
                    "a mod author's source file; pass --include-design to move these too"};
    }
    return {Cleanable::NotCleanable, "unknown"};
}

CleanPlan planClean(const Audit& a, bool includeDesign, std::uint32_t limit) {
    CleanPlan plan;
    for (const auto& file : a.redundant) {
        if (limit != 0 && plan.moved.size() >= limit) {
            plan.skipped.push_back("limit reached");
            break;
        }
        const CleanDecision decision = decideClean(file, includeDesign);
        if (decision.verdict == Cleanable::Yes) {
            plan.moved.push_back(file);
            plan.bytesMoved += file.bytes;
        } else {
            plan.skipped.push_back(narrowName(file.file) + " (" + decision.reason + ')');
        }
    }
    return plan;
}

namespace {

// destination / <relative path>. Two mods shipping a file of the same name would otherwise
// overwrite each other on the way out.
std::filesystem::path targetFor(const std::filesystem::path& installRoot,
                                const std::filesystem::path& destination,
                                const std::filesystem::path& file) {
    std::error_code ec;
    auto relative = std::filesystem::relative(file, installRoot, ec);
    if (ec || relative.empty()) {
        // Outside the installation: park it under its own file name.
        return destination / file.filename();
    }
    return destination / relative;
}

// The holding folder has to remember which installation the files came from: the relative
// path alone does not say where to put them back. A small text file next to them does, and
// it is written on every clean so it cannot drift out of step with what was moved.
constexpr const char* kManifestName = "evigit-restore.txt";

std::filesystem::path manifestPath(const std::filesystem::path& destination) {
    return destination / kManifestName;
}

void writeManifest(const std::filesystem::path& destination,
                   const std::filesystem::path& installRoot) {
    std::error_code ec;
    std::filesystem::create_directories(destination, ec);
    std::ofstream out(manifestPath(destination), std::ios::binary);
    // One line, the installation root. Written as UTF-8 so a path outside the console's
    // code page survives the round trip.
    const auto wide = installRoot.wstring();
    std::string utf8;
    for (const wchar_t w : wide) {
        const auto c = static_cast<std::uint32_t>(w);
        if (c < 0x80) {
            utf8.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            utf8.push_back(static_cast<char>(0xC0 | (c >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            utf8.push_back(static_cast<char>(0xE0 | (c >> 12)));
            utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    out << utf8;
}

std::optional<std::filesystem::path> readManifest(const std::filesystem::path& destination) {
    std::ifstream in(manifestPath(destination), std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string line;
    std::getline(in, line);
    if (line.empty()) {
        return std::nullopt;
    }
    // Decoded back from UTF-8. path::u8path() is deprecated in C++20, so the conversion is
    // done by hand: the same encoder writeManifest uses, run backwards.
    std::wstring wide;
    for (std::size_t i = 0; i < line.size();) {
        const auto lead = static_cast<unsigned char>(line[i]);
        std::uint32_t c = 0;
        std::size_t extra = 0;
        if (lead < 0x80) {
            c = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            c = lead & 0x1Fu;
            extra = 1;
        } else {
            c = lead & 0x0Fu;
            extra = 2;
        }
        ++i;
        for (std::size_t k = 0; k < extra && i < line.size(); ++k, ++i) {
            c = (c << 6) | (static_cast<unsigned char>(line[i]) & 0x3Fu);
        }
        wide.push_back(static_cast<wchar_t>(c));
    }
    return std::filesystem::path(wide);
}

}  // namespace

CleanPlan applyClean(const Audit& a, const std::filesystem::path& installRoot,
                     const std::filesystem::path& destination, bool includeDesign,
                     std::uint32_t limit) {
    CleanPlan plan = planClean(a, includeDesign, limit);
    if (plan.moved.empty()) {
        return plan;
    }

    std::error_code ec;
    std::filesystem::create_directories(destination, ec);
    // Written before anything moves, so a clean that fails half way still leaves behind
    // what is needed to undo it.
    writeManifest(destination, installRoot);

    CleanPlan done;
    for (const auto& file : plan.moved) {
        auto target = targetFor(installRoot, destination, file.file);
        // Never overwrite: a name that already exists gets a counter rather than being
        // replaced, so a second clean cannot destroy a first one's contents.
        std::filesystem::create_directories(target.parent_path(), ec);
        for (int n = 1; n < 1000 && std::filesystem::exists(target, ec); ++n) {
            target.replace_filename(file.file.filename().wstring() + L"." + std::to_wstring(n));
        }

        std::filesystem::rename(file.file, target, ec);
        if (ec) {
            done.skipped.push_back(narrowName(file.file) + ": " + ec.message());
            continue;
        }
        done.moved.push_back(file);
        done.bytesMoved += file.bytes;
        omsi::core::debug("moved {} out of the installation", narrowName(file.file));
    }
    return done;
}

CleanPlan applyRestore(const std::filesystem::path& destination, std::uint32_t limit) {
    CleanPlan restored;

    // Without the manifest there is no way to know where these files came from, and
    // guessing would put them somewhere the user did not ask for.
    const auto root = readManifest(destination);
    if (!root) {
        restored.skipped.push_back(
            "no evigit-restore.txt here: this folder was not cleaned by evigit, or it was "
            "cleaned by a version that did not write one. Nothing was moved.");
        return restored;
    }

    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(destination, ec);
    if (ec) {
        restored.skipped.push_back("cannot read the holding folder: " + ec.message());
        return restored;
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : it) {
        std::error_code inner;
        if (!entry.is_regular_file(inner)) {
            continue;
        }
        // The manifest is not content: it records where to put things back, and restoring
        // it would copy it into the installation as a stray file.
        if (entry.path().filename() == kManifestName) {
            continue;
        }
        files.push_back(entry.path());
    }
    std::ranges::sort(files);

    for (const auto& file : files) {
        if (limit != 0 && restored.moved.size() >= limit) {
            restored.skipped.push_back("limit reached");
            break;
        }
        const auto original = *root / file.lexically_relative(destination);
        if (original.empty() || original == file) {
            restored.skipped.push_back(narrowName(file) + ": outside the holding folder");
            continue;
        }

        // If something is back in the installation already, leave it alone rather than
        // replacing it: the file is not lost either way, and overwriting would be.
        if (std::filesystem::exists(original, ec)) {
            restored.skipped.push_back(narrowName(original) + ": already present, left alone");
            continue;
        }

        std::filesystem::create_directories(original.parent_path(), ec);
        const auto size = std::filesystem::file_size(file, ec);
        std::filesystem::rename(file, original, ec);
        if (ec) {
            restored.skipped.push_back(narrowName(file) + ": " + ec.message());
            continue;
        }
        restored.moved.push_back(RedundantFile{original, RedundantReason::DesignSource,
                                                ec ? 0 : size, {}});
        if (!ec) {
            restored.bytesMoved += size;
        }
    }
    return restored;
}

}  // namespace omsi::content