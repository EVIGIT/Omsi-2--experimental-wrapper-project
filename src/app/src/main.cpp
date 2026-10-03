// SPDX-License-Identifier: MIT
//
// evigit - a command line front end for the launcher.
//
// Until the window exists this is how the installation is inspected: it finds an OMSI 2
// installation, lists what is in it, and reports anything that could not be read. Every
// screen of the eventual launcher runs on the same Library, so this doubles as a way to
// check the parsers against a real installation.

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "omsi/content/audit.hpp"
#include "omsi/content/library.hpp"
#include "omsi/content/profile.hpp"
#include "omsi/core/log.hpp"
#include "omsi/pe/pe.hpp"

namespace {

// A real OMSI 2 installation has file names the console's code page cannot represent - this
// one has a euro sign in a vehicle folder. std::filesystem::path::string() *throws* on those
// rather than substituting, so a name must never be converted first and printed after: it
// has to be walked as wide characters and narrowed one at a time here.
void writeSafely(std::ostream& out, const std::filesystem::path& path) {
    const auto native = static_cast<char>(out.widen('?'));
    for (const wchar_t w : path.wstring()) {
        const auto c = static_cast<std::uint32_t>(w);
        if (c < 0x80 || (c >= 0xA0 && c <= 0xFF)) {
            out.put(static_cast<char>(c));
        } else {
            out.put(native);
        }
    }
}

// The same, for text that is already narrow. Named differently rather than overloaded:
// a std::string would convert to both this and the path overload, and the call would be
// ambiguous - and the ambiguity would only show up at a call site carrying a file name.
void writeTextSafely(std::ostream& out, std::string_view text) {
    const auto native = static_cast<char>(out.widen('?'));
    for (const char c : text) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x80 || (byte >= 0xA0 && byte <= 0xFF)) {
            out.put(static_cast<char>(byte));
        } else {
            out.put(native);
        }
    }
}

constexpr std::string_view kUsage =
    R"(evigit - inspect an OMSI 2 installation

usage:
  evigit [options] [root]

  root              OMSI 2 installation folder; found automatically when omitted
                    (honours $OMSI_ROOT, then a folder beside the program, then Steam)

options:
  -h, --help        show this text
  -v, --verbose     log every folder that is scanned
  --candidates      list the folders that would be searched, and stop
  --image <file>    report the PE headers of any executable and stop
  --profile         measure the content weight and say what the ceiling probably is
  --audit          list files the game can never open, and the space they waste
  --clean          move redundant files out of the installation (never deletes)
  --move-to <dir>  where --clean puts them. Required: nothing is picked for you
  --restore        move everything back out of the holding folder
  --include-design also move .psd/.blend/.pdn - a mod author's source files
  --limit <n>      move at most n files, for a cautious first run

)";

struct Options {
    std::filesystem::path root;
    bool help = false;
    bool verbose = false;
    bool candidates = false;
    bool profile = false;
    bool audit = false;
    bool clean = false;
    bool restore = false;
    bool includeDesign = false;
    std::uint32_t limit = 0;
    std::optional<std::filesystem::path> image;
    std::optional<std::filesystem::path> moveTo;
};

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            o.help = true;
        } else if (arg == "-v" || arg == "--verbose") {
            o.verbose = true;
        } else if (arg == "--candidates") {
            o.candidates = true;
        } else if (arg == "--profile") {
            o.profile = true;
        } else if (arg == "--audit") {
            o.audit = true;
        } else if (arg == "--clean") {
            o.clean = true;
        } else if (arg == "--restore") {
            o.restore = true;
        } else if (arg == "--include-design") {
            o.includeDesign = true;
        } else if (arg == "--limit" && i + 1 < argc) {
            o.limit = static_cast<std::uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--move-to" && i + 1 < argc) {
            o.moveTo = std::filesystem::path(argv[++i]);
        } else if (arg == "--image") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--image needs a file to look at");
            }
            o.image = std::filesystem::path(argv[++i]);
        } else if (!arg.empty() && arg.front() == '-') {
            throw std::runtime_error("unknown option: " + std::string(arg));
        } else {
            o.root = std::filesystem::path(arg);
        }
    }
    return o;
}

// Prints one executable's headers. This only ever reads.
void reportImage(const omsi::pe::ImageInfo& image) {
    // A real installation has file names that are not in the console's code page (this one
    // has a euro sign in a vehicle folder). std::cout throws on those rather than printing
    // a placeholder, so names are written through this instead.
    auto put = [](const std::filesystem::path& p) { writeSafely(std::cout, p.filename()); };
    if (!image.valid) {
        put(image.path);
        std::cout << ": cannot be read (" << image.error << ")\n";
        return;
    }

    put(image.path);
    std::cout << '\n'
              << "  format            " << omsi::pe::formatName(image.pe32Plus) << '\n'
              << "  machine           " << omsi::pe::machineName(image.machine)
              << (image.is64Bit() ? " (64-bit)" : " (32-bit)") << '\n'
              << "  subsystem         " << omsi::pe::subsystemName(image.subsystem) << '\n'
              << "  characteristics   0x" << std::hex << std::uppercase << image.characteristics
              << std::dec << std::nouppercase << '\n'
              << "  large address aware  " << (image.largeAddressAware() ? "yes" : "no");

    if (image.largeAddressAware()) {
        std::cout << "  (2 GB -> 4 GB of address space)";
    } else if (!image.is64Bit()) {
        std::cout << "  (a 32-bit image: 2 GB of address space)";
    }
    std::cout << '\n';
}

std::string gib(std::uint64_t bytes) {
    return std::format("{:.2f} GiB",
                       static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
}

// Prints the content survey. Read-only: it walks folders and sums sizes, and never writes
// anything or starts the game.
void reportProfile(const omsi::content::Profile& p) {
    std::cout << "Content weight\n"
              << "  total           " << p.totalFiles << " files, " << gib(p.totalBytes)
              << '\n'
              << "  textures        " << p.textureFiles << " files, " << gib(p.textureBytes);

    if (p.volume) {
        std::cout << "\n  volume          " << p.volume->label;
        if (!p.volume->fileSystem.empty()) {
            std::cout << " (" << p.volume->fileSystem << ')';
        }
        if (p.volume->volumeBytes != 0) {
            std::cout << ", " << gib(p.volume->freeBytes) << " free of "
                      << gib(p.volume->volumeBytes);
        }
    }

    std::cout << "\n\nLargest file types\n";
    for (const auto& e : p.extensions) {
        if (e.bytes == 0 || e.files == 0) {
            continue;
        }
        std::cout << "  ." << e.extension << std::string(12 > e.extension.size() ? 12 -
                                                                                  e.extension.size()
                                                                            : 1,
                                                          ' ')
                  << std::setw(8) << e.files << "  " << gib(e.bytes) << '\n';
    }

    if (!p.maps.empty()) {
        std::cout << "\nMaps by weight\n";
        std::cout << "  (scenery models live in the shared Sceneryobjects/ library, not in\n"
                     "   the map folders, so a map's own folder does not count them)\n";
        for (const auto& m : p.maps) {
            std::cout << "  " << m.folderName << std::string(26 > m.folderName.size()
                                                                 ? 26 - m.folderName.size()
                                                                 : 1,
                                                             ' ')
                      << gib(m.bytes) << "  " << m.files << " files\n";
        }
    }

    if (!p.problems.empty()) {
        std::cout << "\nCould not read (" << p.problems.size() << ")\n";
        for (const auto& problem : p.problems) {
            std::cout << "  " << problem << '\n';
        }
    }

    std::cout << "\nVerdict: " << p.verdict() << "\n\n";
}

// Prints what the game can never open. This only reads and lists: it removes nothing.
// Deleting files inside a game install is not something a tool should do unattended, and
// "redundant" here means "unreachable by the loader", not "safe to destroy" - a mod
// author may well want their own sources back.
void reportAudit(const omsi::content::Audit& a, std::size_t limit) {
    std::cout << "Audit\n"
              << "  scanned         " << a.filesScanned << " files\n"
              << "  reclaimable     " << a.reclaimableSummary() << '\n';

    if (a.redundant.empty()) {
        std::cout << "\nNothing redundant found. Every file is either reachable by the\n"
                     "loader or a format it does not read as content.\n\n";
        return;
    }

    std::cout << "\nRedundant files (";
    const auto shown = std::min(limit, a.redundant.size());
    std::cout << "showing " << shown << " of " << a.redundant.size() << ")\n";
    for (std::size_t i = 0; i < shown; ++i) {
        const auto& r = a.redundant[i];
        std::cout << "  " << gib(r.bytes) << "  " << omsi::content::toString(r.reason) << "\n          ";
        writeSafely(std::cout, r.file.filename());
        std::cout << '\n';
        if (!r.shadowedBy.empty()) {
            std::cout << "          used instead: ";
            writeSafely(std::cout, r.shadowedBy.filename());
            std::cout << '\n';
        }
    }
    if (shown < a.redundant.size()) {
        std::cout << "  ... and " << (a.redundant.size() - shown) << " more\n";
    }

    if (!a.parseFailures.empty()) {
        std::cout << "\nFailed to parse (" << a.parseFailures.size() << ")\n";
        for (const auto& f : a.parseFailures) {
            writeSafely(std::cout, f.file);
            writeTextSafely(std::cout, f.reason);
            std::cout << '\n';
        }
    }
    if (!a.unreadable.empty()) {
        std::cout << "\nCould not read (" << a.unreadable.size() << ")\n";
        for (const auto& u : a.unreadable) {
            writeTextSafely(std::cout, u);
            std::cout << '\n';
        }
    }
    std::cout << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse(argc, argv);
        if (options.help) {
            std::cout << kUsage;
            return 0;
        }

        omsi::core::setConsoleLevel(options.verbose ? omsi::core::LogLevel::Debug
                                                     : omsi::core::LogLevel::Info);
        omsi::core::logToFile(std::filesystem::temp_directory_path() / "evigit.log");

        // The program folder is where the executable sits, so a portable install next to
        // an "OMSI 2" folder is found without configuration.
        std::error_code ec;
        std::filesystem::path programDir = std::filesystem::current_path(ec);
        if (const char* exe = std::getenv("EXECUTABLE_PATH"); exe != nullptr) {
            programDir = std::filesystem::path(exe).parent_path();
        }

        if (options.candidates) {
            std::cout << "Folders that would be searched, in order:\n";
            for (const auto& c : omsi::content::rootCandidates(programDir)) {
                std::cout << "  [" << (omsi::content::looksLikeInstall(c) ? "yes" : " no")
                          << "]  " << c.string() << '\n';
            }
            return 0;
        }

        if (options.image) {
            reportImage(omsi::pe::inspect(*options.image));
            return 0;
        }

        if (options.profile) {
            // The profile does not need an installation - it weighs whatever folder it is
            // pointed at - so it runs before the Library is built and does not throw when
            // the folder turns out to be something else entirely.
            std::filesystem::path target = options.root;
            if (target.empty()) {
                const auto found = omsi::content::findInstall(programDir);
                if (!found) {
                    std::cerr << "No OMSI 2 installation found. Pass the folder as an "
                                 "argument.\n";
                    return 2;
                }
                target = *found;
            }
            reportProfile(omsi::content::profile(target));
            return 0;
        }

        if (options.restore) {
            if (!options.moveTo) {
                std::cerr << "--restore needs --move-to <folder>, the same folder you cleaned into.\n";
                return 2;
            }
            const auto done = omsi::content::applyRestore(*options.moveTo, options.limit);
            std::cout << "Restored " << done.moved.size() << " files, " << gib(done.bytesMoved)
                      << ".\n";
            for (const auto& s : done.skipped) {
                std::cout << "  left alone: " << s << '\n';
            }
            std::cout << '\n';
            return 0;
        }

        if (options.clean) {
            // Nothing is chosen on the user's behalf. A clean that guessed its own
            // destination could put files somewhere they would never be found again.
            if (!options.moveTo) {
                std::cerr << "--clean needs --move-to <folder>. Nothing is deleted, and\n"
                             "nothing is picked for you: choose a folder outside the\n"
                             "installation, and --restore will bring everything back.\n\n"
                          << "For example:\n"
                          << "  evigit --clean --move-to E:\\OMSI-Quarantine \\\n"
                          << "          \"E:\\SteamLibrary\\steamapps\\common\\OMSI 2\"\n";
                return 2;
            }
            if (options.includeDesign) {
                std::cout << "Note: --include-design moves .psd/.blend/.pdn files too. Those\n"
                             "are a mod author's working files and exist nowhere else.\n\n";
            }
        }

        if (options.clean || options.audit) {
            std::filesystem::path target = options.root;
            if (target.empty()) {
                const auto found = omsi::content::findInstall(programDir);
                if (!found) {
                    std::cerr << "No OMSI 2 installation found. Pass the folder as an "
                                 "argument.\n";
                    return 2;
                }
                target = *found;
            }

            if (!options.clean) {
                reportAudit(omsi::content::audit(target), 40);
                return 0;
            }

            // The audit runs first so what will move is known before anything does.
            const auto found = omsi::content::audit(target);
            const auto plan = omsi::content::planClean(found, options.includeDesign,
                                                       options.limit);
            std::cout << "About to move " << plan.moved.size() << " files ("
                      << gib(plan.bytesMoved) << ") out of\n  " << target.string() << "\nto\n  "
                      << options.moveTo->string() << "\n\n";
            if (plan.moved.empty()) {
                std::cout << "Nothing is eligible. Run --audit to see why files were skipped.\n\n";
                return 0;
            }

            const auto done = omsi::content::applyClean(found, target, *options.moveTo,
                                                        options.includeDesign, options.limit);
            std::cout << "Moved " << done.moved.size() << " files, " << gib(done.bytesMoved)
                      << ".\n";
            if (!done.skipped.empty()) {
                std::cout << "  " << done.skipped.size() << " not moved (design sources need "
                          << "--include-design, or the reason is below)\n";
            }
            for (std::size_t i = 0; i < done.skipped.size() && i < 10; ++i) {
                std::cout << "    " << done.skipped[i] << '\n';
            }
            std::cout << "\nTo put everything back:\n  evigit --restore --move-to "
                      << options.moveTo->string() << "\n\n"
                      << "Nothing was deleted. Every byte moved is still on disk.\n\n";
            return 0;
        }

        std::filesystem::path root = options.root;
        if (root.empty()) {
            const auto found = omsi::content::findInstall(programDir);
            if (!found) {
                std::cerr << "No OMSI 2 installation found.\n\n"
                          << "Pass the folder as an argument, set OMSI_ROOT, or put an "
                             "\"OMSI 2\" folder beside evigit.exe.\n\n"
                          << "Run with --candidates to see every folder that was tried.\n";
                return 2;
            }
            root = *found;
        }

        std::cout << "Installation: " << root.string() << "\n\n";

        const omsi::content::Library library(root);

        // The game executable, read but never touched. It is reported before the content
        // because it is the one thing about an installation the user cannot fix by
        // installing a mod.
        if (const auto& image = library.gameImage()) {
            std::cout << "Game\n";
            reportImage(*image);

            const auto texLimit = omsi::content::textureMemoryLimit(root);

            if (image->valid && !image->is64Bit() && !image->largeAddressAware()) {
                std::cout << "  This build is 32-bit and is limited to 2 GB of address space.\n";
                if (texLimit) {
                    std::cout << std::format(
                        "  options.cfg asks for {:.0f} MB of texture memory ({:.1f} GB). With\n"
                        "  2 GB to hold it that budget is never met, so the game keeps\n"
                        "  lowering distant textures and streaming them back in.\n"
                        "  Setting the large-address-aware flag raises the process to 4 GB,\n"
                        "  which is what makes a larger budget usable - the two are the same\n"
                        "  setting, not two problems.\n",
                        *texLimit, *texLimit / 1024.0);
                }
                std::cout << "  evigit does not change the file, and a game update would clear\n"
                             "  it again. The patch that does it ships with the game as\n"
                             "  4gb_patch.exe; this only tells you where you stand.\n\n";
            } else if (texLimit) {
                std::cout << std::format(
                    "  texture budget  {:.0f} MB ({:.1f} GB) from options.cfg\n",
                    *texLimit, *texLimit / 1024.0);
            }
        }

        std::cout << "Maps (" << library.maps().size() << ")\n";
        for (const auto& m : library.maps()) {
            std::cout << "  " << m.folderName;
            if (!m.title.empty() && m.title != m.folderName) {
                std::cout << "  -  " << m.title;
            }
            std::cout << '\n';
        }

        std::cout << "\nVehicles (" << library.vehicles().size() << ")\n";
        for (const auto& v : library.vehicles()) {
            std::cout << "  [" << v.kind << "]  ";
            writeTextSafely(std::cout, v.displayName());
            std::cout << '\n';
        }

        std::cout << "\nTimetables (" << library.timetables().size() << ")\n";
        for (const auto& t : library.timetables()) {
            std::cout << "  [" << t.kind << "]  ";
            writeTextSafely(std::cout, t.displayName());
            std::cout << '\n';
        }

        if (!library.problems().empty()) {
            std::cout << "\nProblems (" << library.problems().size() << ")\n";
            for (const auto& p : library.problems()) {
                writeTextSafely(std::cout, p);
                std::cout << '\n';
            }
        }

        std::cout << '\n';
        return 0;
    } catch (const std::exception& e) {
        omsi::core::error("evigit: {}", e.what());
        std::cerr << "evigit: " << e.what() << '\n';
        return 1;
    }
}