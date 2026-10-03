// SPDX-License-Identifier: MIT
//
// Scanning an OMSI 2 installation.
//
// These build a synthetic installation in a temporary folder, the way the Rust project's
// tests do: no file from a real installation is ever copied into the repository.

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "omsi/content/audit.hpp"
#include "omsi/content/library.hpp"
#include "omsi/content/profile.hpp"

namespace fs = std::filesystem;

namespace {

// A folder that is removed again when the test ends, however it ends.
class TempDir {
public:
    TempDir() {
        static int counter = 0;
        path_ = fs::temp_directory_path() /
                ("evigit-test-" + std::to_string(counter++) + "-" +
                 std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const fs::path& path() const { return path_; }

    void write(const fs::path& relative, const std::string& text) const {
        const fs::path full = path_ / relative;
        fs::create_directories(full.parent_path());
        std::ofstream out(full, std::ios::binary);
        out << text;
    }

    void touch(const fs::path& relative) const {
        const fs::path full = path_ / relative;
        fs::create_directories(full.parent_path());
        std::ofstream out(full, std::ios::binary);
    }

private:
    fs::path path_;
};

}  // namespace

TEST_CASE("a folder without Maps, Vehicles and envir.cfg is not an installation") {
    const TempDir dir;
    CHECK_FALSE(omsi::content::looksLikeInstall(dir.path()));
    CHECK_FALSE(omsi::content::looksLikeInstall(dir.path() / "nope"));
}

TEST_CASE("an empty but complete installation is recognised") {
    const TempDir dir;
    dir.touch("Maps/.keep");
    dir.touch("Vehicles/.keep");
    dir.touch("envir.cfg");
    CHECK(omsi::content::looksLikeInstall(dir.path()));
}

TEST_CASE("the library lists maps, vehicles and timetables") {
    const TempDir dir;
    dir.touch("envir.cfg");

    dir.write("Maps/Berlin/global.cfg", "[general]\nname Berlin\n");
    dir.write("Maps/Hamburg/global.cfg", "[general]\nname Hamburg\n");
    // A folder under Maps with no global.cfg is not a playable map.
    dir.touch("Maps/Expansion/some_source.txt");

    dir.touch("Vehicles/MAN_SD200/MAN_D92.bus");
    dir.touch("Vehicles/MAN_SD200/MAN_D92.ovh");   // a trailer, not listed
    dir.touch("Vehicles/Setra_OM_Linie/S415gtp.tram");
    // A timetables file lives in the *map* folder, not beside the vehicle.
    dir.write("Maps/Berlin/Berlin_AI.ttp", "[line]\n");
    // These are fonts, not timetables, and must not be listed as duties.
    dir.touch("Maps/Berlin/transport.ttf");
    dir.touch("Maps/Berlin/17_LW_Benefit_Linie.oft");
    dir.touch("Vehicles/Berlin/leftover.ttf");

    const omsi::content::Library library(dir.path());

    CHECK(library.maps().size() == 2);
    CHECK(library.maps()[0].folderName == "Berlin");
    CHECK(library.maps()[0].title == "Berlin");
    CHECK(library.maps()[1].folderName == "Hamburg");

    // The .ovh is left out; the .tram is listed.
    CHECK(library.vehicles().size() == 2);
    CHECK(library.vehicles()[0].model == "MAN_D92");
    CHECK(library.vehicles()[0].manufacturer == "MAN_SD200");
    CHECK(library.vehicles()[0].displayName() == "MAN_SD200 MAN_D92");
    CHECK(library.vehicles()[1].kind == "tram");

    CHECK(library.timetables().size() == 1);
    CHECK(library.timetables()[0].name == "Berlin_AI");
    CHECK(library.timetables()[0].map == "Berlin");
    CHECK(library.timetables()[0].kind == "ttp");
    CHECK(library.problems().empty());
}

TEST_CASE("a manufacturer is the folder directly above the vehicle, not a path") {
    const TempDir dir;
    dir.touch("envir.cfg");
    dir.touch("Maps/.keep");
    // Mods nest their packs; the relative path must not leak into the display name.
    dir.touch("Vehicles/HOH_KI_Autos/HOH_DAF_XF_105/DAF_XF_105.bus");

    const omsi::content::Library library(dir.path());
    REQUIRE(library.vehicles().size() == 1);
    CHECK(library.vehicles()[0].manufacturer == "HOH_DAF_XF_105");
    CHECK(library.vehicles()[0].displayName() == "HOH_DAF_XF_105 DAF_XF_105");
}

TEST_CASE("a map without a title falls back to its folder name") {
    const TempDir dir;
    dir.touch("envir.cfg");
    dir.touch("Vehicles/.keep");
    dir.write("Maps/Winter/global.cfg", "[general]\n");
    const omsi::content::Library library(dir.path());
    REQUIRE(library.maps().size() == 1);
    CHECK(library.maps()[0].title.empty());
    CHECK(library.maps()[0].folderName == "Winter");
}

TEST_CASE("an unreadable global.cfg is a problem, not a crash") {
    const TempDir dir;
    dir.touch("envir.cfg");
    dir.touch("Vehicles/.keep");
    // A directory where the file should be: opening it for reading fails.
    fs::create_directories(dir.path() / "Maps/Broken/global.cfg");

    omsi::content::Library library(dir.path());
    CHECK(library.problems().size() == 1);
}

TEST_CASE("opening a folder that is not an installation throws") {
    const TempDir dir;
    CHECK_THROWS_AS(omsi::content::Library(dir.path()), std::runtime_error);
}

TEST_CASE("$OMSI_ROOT is the first candidate") {
    const std::vector<fs::path> candidates = omsi::content::rootCandidates(fs::path{});
    REQUIRE_FALSE(candidates.empty());
    const char* root = std::getenv("OMSI_ROOT");
    if (root != nullptr && root[0] != '\0') {
        CHECK(candidates.front() == fs::path(root));
    }
}

TEST_CASE("a texture shadowed by a same-folder .dds is redundant") {
    const TempDir dir;
    dir.write("envir.cfg", "[general]\n");
    // Same stem, same folder: the loader tries the .dds first, so the .bmp is unreachable.
    dir.write("Sceneryobjects/Trees/oak.bmp", std::string(500, 'x'));
    dir.write("Sceneryobjects/Trees/oak.dds", std::string(100, 'x'));
    // Used: no .dds beside it.
    dir.write("Sceneryobjects/Trees/ash.tga", std::string(400, 'x'));

    const omsi::content::Audit a = omsi::content::audit(dir.path());

    REQUIRE(a.redundant.size() == 1);
    CHECK(a.redundant[0].file.filename() == "oak.bmp");
    // Compared as text: doctest cannot stringify a scoped enum, so CHECK on one would not
    // compile. The string form is what a reader sees in the report anyway.
    CHECK(std::string{omsi::content::toString(a.redundant[0].reason)} ==
          "shadowed by a same-folder .dds");
    CHECK(a.redundant[0].bytes == 500);
    CHECK(a.redundant[0].shadowedBy.filename() == "oak.dds");
    CHECK(a.reclaimableTextureBytes() == 500);
}

TEST_CASE("a .dds in another folder does NOT make a texture redundant") {
    const TempDir dir;
    dir.write("envir.cfg", "[general]\n");
    // Folder precedence is decided before the extension search, so a global Texture/
    // .dds cannot shadow an object's local .bmp.
    dir.write("Sceneryobjects/Trees/oak.bmp", std::string(500, 'x'));
    dir.write("Texture/oak.dds", std::string(100, 'x'));

    const omsi::content::Audit a = omsi::content::audit(dir.path());

    CHECK(a.redundant.empty());
}

TEST_CASE("_LOW and seasonal textures are never judged redundant") {
    const TempDir dir;
    dir.write("envir.cfg", "[general]\n");
    // A _LOW variant is its own lookup target, and a seasonal folder is resolved
    // separately - neither may be compared against a full-size file.
    dir.write("Sceneryobjects/Trees/oak_LOW.bmp", std::string(300, 'x'));
    dir.write("Sceneryobjects/Trees/oak_LOW.dds", std::string(50, 'x'));
    dir.write("Sceneryobjects/Trees/Texture/Spring/pine.bmp", std::string(300, 'x'));
    dir.write("Sceneryobjects/Trees/Texture/Spring/pine.dds", std::string(50, 'x'));

    const omsi::content::Audit a = omsi::content::audit(dir.path());

    CHECK(a.redundant.empty());
}

TEST_CASE("design sources are redundant only inside a content folder") {
    const TempDir dir;
    dir.write("envir.cfg", "[general]\n");
    dir.write("Sceneryobjects/Trees/oak.psd", std::string(1000, 'x'));
    dir.write("Vehicles/MAN/model.blend", std::string(500, 'x'));
    // The same extension in the root is documentation, and must be left alone.
    dir.write("readme.psd", std::string(2000, 'x'));

    const omsi::content::Audit a = omsi::content::audit(dir.path());

    REQUIRE(a.redundant.size() == 2);
    for (const auto& r : a.redundant) {
        CHECK(std::string{omsi::content::toString(r.reason)} ==
              "design source, never read by the game");
        CHECK(r.file.filename() != "readme.psd");
    }
    CHECK(a.reclaimableBytes() == 1500);
}

TEST_CASE("the audit reports a folder it cannot read rather than throwing") {
    const TempDir dir;
    const omsi::content::Audit a = omsi::content::audit(dir.path() / "nope");

    CHECK(a.filesScanned == 0);
    REQUIRE_FALSE(a.unreadable.empty());
}

TEST_CASE("an install with nothing redundant says so") {
    const TempDir dir;
    dir.write("envir.cfg", "[general]\n");
    dir.write("Maps/Berlin/global.cfg", "[general]\n");
    dir.write("Vehicles/MAN/MAN_D92.bus", "[general]\n");
    dir.write("Sceneryobjects/Trees/oak.bmp", std::string(500, 'x'));

    const omsi::content::Audit a = omsi::content::audit(dir.path());

    CHECK(a.redundant.empty());
    CHECK(a.reclaimableBytes() == 0);
    CHECK(a.filesScanned == 4);
}

using omsi::content::Cleanable;
using omsi::content::RedundantFile;
using omsi::content::RedundantReason;

TEST_CASE("a design source is only moved when explicitly asked for") {
    const RedundantFile design{"Maps/Berlin/tree.psd", RedundantReason::DesignSource, 10, {}};
    const RedundantFile shadow{"Maps/Berlin/tree.bmp", RedundantReason::TextureShadowedByDds,
                               20, "Maps/Berlin/tree.dds"};

    // Without the flag a design source is refused, and says why.
    CHECK(decideClean(design, false).verdict == Cleanable::DesignSource);
    CHECK_FALSE(decideClean(design, false).reason.empty());

    // With it, it is eligible.
    CHECK(decideClean(design, true).verdict == Cleanable::Yes);

    // A shadowed texture needs no flag either way.
    CHECK(decideClean(shadow, false).verdict == Cleanable::Yes);
    CHECK(decideClean(shadow, true).verdict == Cleanable::Yes);
}

TEST_CASE("cleaning moves files out and restore puts every byte back") {
    const TempDir dir;
    const TempDir holding;
    dir.write("envir.cfg", "[general]\n");
    dir.write("Sceneryobjects/Trees/oak.bmp", std::string(500, 'x'));
    dir.write("Sceneryobjects/Trees/oak.dds", std::string(100, 'x'));
    dir.write("Sceneryobjects/Trees/pine.psd", std::string(1000, 'x'));

    const fs::path install = dir.path();
    const fs::path out = holding.path() / "quarantine";

    const omsi::content::Audit a = omsi::content::audit(install);
    REQUIRE(a.redundant.size() == 2);

    // Without --include-design only the shadowed texture moves.
    const auto cleaned = omsi::content::applyClean(a, install, out, /*includeDesign=*/false, 0);
    CHECK(cleaned.moved.size() == 1);
    CHECK(cleaned.bytesMoved == 500);
    CHECK_FALSE(fs::exists(install / "Sceneryobjects/Trees/oak.bmp"));
    CHECK(fs::exists(install / "Sceneryobjects/Trees/oak.dds"));   // the one in use
    CHECK(fs::exists(install / "Sceneryobjects/Trees/pine.psd"));  // left alone

    // The relative path is preserved, so the restore knows where it came from.
    CHECK(fs::exists(out / "Sceneryobjects/Trees/oak.bmp"));

    const auto restored = omsi::content::applyRestore(out, 0);
    CHECK(restored.moved.size() == 1);
    CHECK(fs::exists(install / "Sceneryobjects/Trees/oak.bmp"));
}

TEST_CASE("restore refuses a folder it did not write, rather than guessing") {
    const TempDir holding;
    const fs::path out = holding.path() / "not-ours";
    fs::create_directories(out);
    {
        std::ofstream stray(out / "something.bmp");
        stray << "put here by something else";
    }

    const auto restored = omsi::content::applyRestore(out, 0);

    CHECK(restored.moved.empty());
    REQUIRE_FALSE(restored.skipped.empty());
    CHECK(fs::exists(out / "something.bmp"));  // untouched
}

TEST_CASE("a clean never overwrites a file that is already in the holding folder") {
    const TempDir dir;
    const TempDir holding;
    dir.write("envir.cfg", "[general]\n");
    dir.write("Sceneryobjects/Trees/oak.bmp", std::string(500, 'x'));
    dir.write("Sceneryobjects/Trees/oak.dds", std::string(100, 'x'));

    const fs::path install = dir.path();
    const fs::path out = holding.path() / "quarantine";
    // Something of the same name is already parked there, from an earlier run.
    fs::create_directories(out / "Sceneryobjects/Trees");
    {
        std::ofstream existing(out / "Sceneryobjects/Trees/oak.bmp");
        existing << "from the first clean";
    }

    const auto a = omsi::content::audit(install);
    const auto cleaned = omsi::content::applyClean(a, install, out, false, 0);

    REQUIRE(cleaned.moved.size() == 1);
    // The parked file keeps its contents: the new one went alongside it.
    std::ifstream parked(out / "Sceneryobjects/Trees/oak.bmp", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(parked)), std::istreambuf_iterator<char>());
    CHECK(text == "from the first clean");
    CHECK(fs::exists(out / "Sceneryobjects/Trees/oak.bmp.1"));
}

TEST_CASE("restore leaves a file alone when one is already back in the installation") {
    const TempDir dir;
    const TempDir holding;
    const fs::path out = holding.path() / "quarantine";
    fs::create_directories(out / "Sceneryobjects/Trees");
    {
        std::ofstream parked(out / "Sceneryobjects/Trees/oak.bmp");
        parked << "the parked copy";
    }
    // And the original has been put back by hand.
    fs::create_directories(dir.path() / "Sceneryobjects/Trees");
    {
        std::ofstream back(dir.path() / "Sceneryobjects/Trees/oak.bmp");
        back << "the restored copy";
    }

    const auto restored = omsi::content::applyRestore(out, 0);

    CHECK(restored.moved.empty());
    CHECK_FALSE(restored.skipped.empty());
    std::ifstream kept(dir.path() / "Sceneryobjects/Trees/oak.bmp", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(kept)), std::istreambuf_iterator<char>());
    CHECK(text == "the restored copy");
    CHECK(fs::exists(out / "Sceneryobjects/Trees/oak.bmp"));  // still parked
}

TEST_CASE("--limit stops after the given number of files") {
    const TempDir dir;
    dir.write("envir.cfg", "[general]\n");
    dir.write("Sceneryobjects/A/oak.bmp", std::string(100, 'x'));
    dir.write("Sceneryobjects/A/oak.dds", std::string(10, 'x'));
    dir.write("Sceneryobjects/B/ash.bmp", std::string(100, 'x'));
    dir.write("Sceneryobjects/B/ash.dds", std::string(10, 'x'));

    const omsi::content::Audit a = omsi::content::audit(dir.path());
    CHECK(a.redundant.size() == 2);

    const auto plan = omsi::content::planClean(a, false, 1);
    CHECK(plan.moved.size() == 1);
    CHECK_FALSE(plan.skipped.empty());
}

TEST_CASE("the profile sums files, textures and per-map weight") {
    const TempDir dir;
    dir.write("envir.cfg", "[general]\n");

    // Textures are counted separately because they dominate the memory budget.
    dir.write("Maps/Berlin/tex/a.dds", std::string(3000, 'x'));
    dir.write("Maps/Berlin/tex/b.png", std::string(1000, 'x'));
    dir.write("Maps/Berlin/Objects/tree.o3d", "x");
    dir.write("Maps/Berlin/Objects/bench.o3d", "x");
    // A Wavefront source mesh is not what the game loads, so it must not count.
    dir.write("Maps/Berlin/Objects/tree.obj", "x");
    dir.write("Maps/Hamburg/tex/c.dds", std::string(2000, 'x'));

    const omsi::content::Profile p = omsi::content::profile(dir.path());

    // Six files above: three textures, two .o3d models and one .obj source in Berlin, one
    // texture in Hamburg, plus envir.cfg. TempDir writes in binary mode, so there is no
    // CRLF translation and each one-character file is one byte.
    CHECK(p.totalFiles == 7);
    CHECK(p.totalBytes == 3000 + 1000 + 2000 + 3 + 10);  // the 10 is envir.cfg
    CHECK(p.textureFiles == 3);
    CHECK(p.textureBytes == 6000);

    REQUIRE(p.maps.size() == 2);
    // Berlin is the heavier of the two, so it sorts first.
    CHECK(p.maps[0].folderName == "Berlin");
    // The two .o3d models count; the .obj source mesh does not.
    CHECK(p.maps[0].objects == 2);
    CHECK(p.maps[1].folderName == "Hamburg");

    // Extensions are sorted by size, so the textures lead.
    REQUIRE_FALSE(p.extensions.empty());
    CHECK(p.extensions[0].extension == "dds");
    CHECK(p.problems.empty());
}

TEST_CASE("the texture budget is read from options.cfg") {
    const TempDir dir;
    dir.write("envir.cfg", "[general]\n");
    dir.write("options.cfg", "[general]\n\n[maxFPS]\n200\n\n[texmemlimit]\n1001.0\n");

    const auto limit = omsi::content::textureMemoryLimit(dir.path());
    REQUIRE(limit.has_value());
    CHECK(*limit == 1001.0);
}

TEST_CASE("a missing or unreadable texture budget is not an error") {
    const TempDir dir;
    dir.write("envir.cfg", "[general]\n");
    CHECK_FALSE(omsi::content::textureMemoryLimit(dir.path()).has_value());

    // The key is present but its value is not a number.
    dir.write("options.cfg", "[texmemlimit]\nnot a number\n");
    CHECK_FALSE(omsi::content::textureMemoryLimit(dir.path()).has_value());
}

TEST_CASE("the profile reports a folder it cannot read rather than throwing") {
    const TempDir dir;
    const auto p = omsi::content::profile(dir.path() / "does-not-exist");

    CHECK(p.totalFiles == 0);
    REQUIRE_FALSE(p.problems.empty());
}

TEST_CASE("the profile's verdict names the ceiling it thinks it found") {
    omsi::content::Profile empty;
    CHECK(empty.verdict() == "nothing to measure");

    omsi::content::Profile light;
    light.totalBytes = 1024ull * 1024 * 1024;  // 1 GiB
    light.textureBytes = light.totalBytes / 2;
    CHECK(light.verdict() == "not obviously content-bound. A renderer change is worth measuring.");

    omsi::content::Profile heavy;
    heavy.totalBytes = 40ull * 1024 * 1024 * 1024;  // 40 GiB
    heavy.textureBytes = 35ull * 1024 * 1024 * 1024;
    CHECK(heavy.verdict().find("memory-bound") == 0);

    // Heavy but not texture-dominated reads as load-bound instead.
    omsi::content::Profile wide;
    wide.totalBytes = 40ull * 1024 * 1024 * 1024;
    wide.textureBytes = 1024ull * 1024 * 1024;
    CHECK(wide.verdict().find("load-bound") == 0);
}