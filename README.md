# Omsi-EVIGIT

A launcher and companion tool for **OMSI 2**, written in C++20 for Windows.

It reads an OMSI 2 installation and tells you what is in it. It contains no OMSI content
of its own and never writes to the original installation.

> **Status: early.** The parsers and the install scan work; there is no native window yet.

See **[roadmap.md](roadmap.md)** for what is done, what is next, and the order things get
built in.

## What is here

| Target | What it does |
|---|---|
| `evigit-web` | **The launcher window.** HTML, CSS and JavaScript served by a loopback HTTP server |
| `evigit` | Command line front end: finds an installation, lists its maps, vehicles and timetables |
| `evigit-vk` | Reports what this machine's Vulkan driver supports, before any renderer is written |
| `omsi_tests` | The test suite (74 cases, 250 assertions) |

## Running

```powershell
# The launcher window.
.\out\build\x64-debug\bin\Debug\evigit-web.exe
.\out\build\x64-debug\bin\Debug\evigit-web.exe --no-browser "D:\Games\OMSI 2"

# What is installed?
.\out\build\x64-debug\bin\Debug\evigit.exe "D:\Games\OMSI 2"

# What can this machine draw?
.\out\build\x64-debug\bin\Debug\evigit-vk.exe
```

`evigit-web` honours `$OMSI_ROOT`, then an "OMSI 2" folder beside the program, then the
usual Steam locations. `--port` picks a port, `--no-browser` only prints the address.

## The window

The front end is three files in [`web/`](web) - `index.html`, `app.css`, `app.js` - with no
framework and no build step. The C++ side serves them and one JSON endpoint:

```
GET /api/library   ->  { root, maps[], vehicles[], timetables[], problems[] }
```

Everything else is a static file.

**Why a web page rather than a native UI?** A launcher is lists, filters and a detail pane;
that is what a document is good at, and the styling, the accessibility and the iteration
speed all come for free. The cost is an IPC boundary, which is why the boundary is one
endpoint returning one payload: the page cannot show a map list from one scan beside a
vehicle list from another.

It currently opens in the **default browser**. Embedding it in a real window is WebView2,
which needs only a different way of pointing the same URL at a window - the front end does
not have to change.

**Security**: the server binds `127.0.0.1` only, serves GET and HEAD only, refuses any path
containing `..`, and sets `X-Content-Type-Options: nosniff`. Nothing is written yet; when
selection is remembered, the state has to be checked rather than assumed.

Needs **Visual Studio 2022 or newer** with the C++ build tools, **CMake 3.28+** (the one
that ships with Visual Studio is fine) and, for the renderer, the **Vulkan SDK**.

```powershell
cmake -S . -B out/build/x64-debug -G "Visual Studio 17 2022" -A x64
cmake --build out/build/x64-debug --config Debug
ctest --test-dir out/build/x64-debug -C Debug --output-on-failure
```

There are presets for this too: `cmake --preset x64-debug`, `cmake --build --preset x64-debug`,
`ctest --preset test`.

The presets use the **Visual Studio 17 2022** generator, which is what CI uses and what is
present on a machine with Visual Studio 2022. A newer Visual Studio works with it. Override
with `-G "Visual Studio 18 2026"` if you have a preview that needs it.

### Options

| Option | Default | Meaning |
|---|---|---|
| `OMSI_BUILD_TESTS` | on for a top-level build | Build and register the test suite |
| `OMSI_BUILD_RENDER` | **off** | Build the Vulkan target; needs the Vulkan SDK |
| `OMSI_BUILD_GUI` | off | Build the ImGui front end |
| `OMSI_WERROR` | off | Treat warnings as errors |

The parsers deliberately do **not** depend on Vulkan, so the project configures, builds and
tests on a machine with no graphics toolchain.

## Running

```powershell
# What is installed?
.\out\build\x64-debug\bin\Debug\evigit.exe --candidates
.\out\build\x64-debug\bin\Debug\evigit.exe "D:\Games\OMSI 2"

# What can this machine draw?
.\out\build\x64-debug\bin\Debug\evigit-vk.exe
```

`evigit` honours `$OMSI_ROOT`, then an "OMSI 2" folder beside the program, then the usual
Steam locations.

## Layout

```
Omsi-EVIGIT/
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/warnings.cmake
├── src/
│   ├── core/       logging
│   ├── cfg/        OMSI's text formats: the [keyword] block parser, code pages, numbers
│   ├── content/    reading an installation: maps, vehicles, timetables
│   ├── web/        the loopback HTTP server, the JSON writer, the library API
│   ├── render/     Vulkan device survey (optional)
│   └── app/        the evigit CLI
├── web/            index.html, app.css, app.js - the front end, served as written
├── tests/          61 cases
└── .github/workflows/ci.yml
```

## Notes on the parsers

OMSI's formats are not ordinary INI files. The rules below were read off the loaders of
`Omsi.exe` 2.2.032 and are the reason the code looks the way it does:

* A keyword is recognised only when the **whole line** is `[name]`. An indented `[mesh]` is
  free text - that is how the stock files carry their help texts and disabled blocks.
* A keyword the original knows is spelled its way (`[matl_noZwrite]`, not `[matl_nozWrite]`).
  A keyword it does not know is matched without regard to case, which is what lets a mod
  spell it however it likes.
* Parameters are read **verbatim**, one per line. An empty line is a legal empty parameter,
  so use `nextNonEmpty()` where the stock files put blank separators.
* Text is decoded in the code page it was written in: Windows-1252 for the stock content,
  1251 for Russian mods, 1250 for Polish and Czech ones, and UTF-8 for newer files.
* Numbers are parsed the way Delphi's `StrToFloat` does: garbage is `0`, not an error.

**No file from a real OMSI 2 installation is ever copied into this repository.** The tests
build a synthetic installation in a temporary folder.

## Adding to it

The parsers are the valuable part and the part you can test without any content, so they
come first. A useful next step is a format-coverage tool in the spirit of openOMSI's
`omsi-check`: point it at an installation and have it read every content file and report
what failed. That gives an objective correctness target before a renderer exists.

## Licence

MIT. OMSI and OMSI 2 are trademarks of their respective owners; this is an independent
project and is not affiliated with them.