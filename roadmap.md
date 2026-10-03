# Roadmap

Ordered **easiest first**. The early phases are small, self-contained and verifiable
without a graphics stack or a running game; the later ones need a renderer and reach into
the simulation. Pick an item, work it, tick it off.

Every item states *how you'll know it's done*. That matters more than the order: most of
the work here is parsers, and a parser without an accuracy target is guesswork.

## Current state

| | |
|---|---|
| Tests | **74 passing / 250 assertions** |
| Build | Release clean under `OMSI_WERROR=ON` |
| Reads | 16 maps, 589 vehicles, 2022 timetables, 0 problems on a real Steam install |
| Writes | **nothing, anywhere** |

Three constraints shape everything below. They were established by measurement, not
assumption, and are worth keeping in mind before proposing a design:

1. **The game is 32-bit and cannot be otherwise.** `Omsi.exe` is `machine=0x014C`. It
   loads `d3dx9.dll` and `steam_api.dll`, which exist only as 32-bit DLLs — a 64-bit
   process cannot load them at all. So we do not modify the game, and never will.
2. **A 64-bit companion cannot feed the game.** The only channels into it are `.opl`+DLL
   and Lua plugins, and both run *inside* the 32-bit process. Heavy work can move to a
   64-bit companion **for our own tooling only** — not to speed up the game.
3. **`LARGE_ADDRESS_AWARE` is a report, not a switch.** The loader reads the flag from the
   file, so it cannot be set at runtime; and a game update overwrites it. `evigit` reports
   it, and nothing in this project writes to the installation.

---

## Phase 1 — Finishing the launcher

Small, no risk, none of it touches the game. Each item is independently useful.

### 1.1 Fix the CMake presets
- [ ] `CMakePresets.json` names "Visual Studio 18 2026", which is not installed here.
      Presets currently fail; builds only work via a hand-rolled VS 2022 configure.
- [ ] Point presets at a generator that exists, or detect and fall back.
- [ ] README quotes the same dead generator and a stale test count (61/205, now 74/250).

**Done when:** `cmake --preset x64-debug && cmake --build --preset x64-debug` works on a
machine with only VS 2022.

### 1.2 Duty engine — read real timetables
- [ ] Parse `.ttp` `.ttl` `.ttr` into trips, stops and times. Format confirmed parseable:
      plain ASCII, Delphi blocks, ~1.3 KB per trip file.
- [ ] Replace filename-based duty filtering with parsed data. Today it matches map name +
      model substring, so it shows wrong duties and misses real ones.
- [ ] Expose duties through `/api/library`. *(parked earlier — fold it in here so the
      picker stops lying)*

**Done when:** the duty list for a bus is derived from the file, and every listed duty
traces back to a line a human can find in the map folder.

### 1.3 Station names
- [ ] Resolve `[station_typ2]` IDs to names via `global.cfg`.
- [ ] Show real stop names in the duty detail pane.

**Done when:** a duty shows `Spandau, Rathaus, …` instead of `382211, 381305, …`.

### 1.4 A native window (WebView2)
- [ ] Embed the existing page instead of opening a browser. `index.html`/`app.js`/`app.css`
      stay exactly as they are — the front end does not change at all.
- [ ] Window chrome, icon, taskbar entry, proper close behaviour.
- [ ] Decide runtime strategy: machine-installed (**present here**, v154.0.4258.53) vs
      bundled fixed version (~150 MB). *Undecided — pick one.*
- [ ] `WebView2Loader.dll` is in neither the NuGet cache nor VS; needs fetching.

**Done when:** a double-clickable `.exe` opens a real window, no address bar, no browser tab.

### 1.5 Launching the game
- [ ] Make **Start** actually start OMSI 2. Currently a placeholder that does nothing.
- [ ] Pass the chosen map / vehicle / duty. *Undecided whether OMSI accepts these on a
      command line — needs checking before design.*
- [ ] Detect a running instance and focus it instead of starting a second.
- [ ] Offer `steam.exe -applaunch 248860` for the Steam build.

**Done when:** Start launches the game on the chosen map with the chosen bus and duty.

### 1.6 Remember the selection
- [ ] Persist last map / bus / duty, restore on next run.
- [ ] Save next to launcher settings — **never inside the installation.**

**Done when:** a second run comes up pre-selected.

### 1.7 Show the PE report in the window
- [ ] Surface `gameImage()` in `/api/library` and render it. *(parked earlier)*
- [ ] Warn when `LARGE_ADDRESS_AWARE` is clear, and say plainly that evigit will not
      change it and a Steam update will clear it again.

**Done when:** the window states the address-space situation without opening a console.

### 1.8 Format coverage checker
- [ ] A read-only tool that reads every content file in an installation and reports what
      failed. The openOMSI equivalent is `omsi-check`.
- [ ] This is the **accuracy target** for every parser from here on — without it, parser
      work has no way to be proven.

**Done when:** you can point it at an installation and get a number.

### 1.9 Installation profile
- [ ] Read-only survey: content size on disk, file counts by type, per-map weight,
      object and vehicle counts, texture bytes.
- [ ] Report the *disk* the content sits on. An install on a spinning disk is
      load-bound no matter how fast the CPU or GPU is, and that is not visible from the
      files alone.
- [ ] The point is to answer one question with a number instead of a guess: is a heavy
      install **memory-bound, load-bound, or CPU-bound**? Everything in Phase 4 depends
      on getting this right first — a Vulkan D3DX9 buys nothing if the bottleneck is
      reading files.

**Done when:** running it on your install produces a verdict, not just a file listing.

---

## Phase 2 — Content handling

Medium, still no graphics involved.

### 2.1 Virtual file system
- [ ] Content roots, and mounted `.zip` archives with OMSI's precedence rules.
- [ ] `Vehicles/` and `Maps/` become virtual paths; a mod inside a `.zip` resolves like a folder.

**Done when:** a vehicle inside a mounted archive appears in the picker.

### 2.2 Asset index and cache
- [ ] One scan of the installation, served from memory afterwards.
- [ ] Watch the content folders and re-index when a mod is installed.
- [ ] This is where a 64-bit companion earns its place — genuinely, on our side.

**Done when:** a rescan is instant and a new mod appears without a restart.

### 2.3 Vehicle and map metadata
- [ ] Real names from `.bus`/`.tram`/`.zug` instead of folder names. Today's names are
      path-derived, so some are ugly (`AI_Caminhao de Lixo_Ecourbss Lixeiro_da_CAVO...`).
- [ ] Photos, descriptions, author, country.

**Done when:** every vehicle shows its real name.

### 2.4 Mod management
- [ ] Enable/disable mods without reinstalling. Note `Addons/` exists — OMSI's own
      activation folder, which may already be the mechanism.
- [ ] Conflict detection: two mods shipping the same object.

**Done when:** a mod can be switched off and its content leaves the picker.

---

## Phase 3 — Seeing the game

Large. Needs the Vulkan renderer in earnest.

### 3.1 Map preview
- [ ] Load map tiles, terrain and splines for a chosen map.

### 3.2 Route preview
- [ ] Draw the chosen duty's path over the map, stop by stop.

### 3.3 Vehicle showroom
- [ ] Place the chosen bus on the map, under the sky and lighting of the chosen hour and
      weather.

### 3.4 Live navigator
- [ ] The tilted map openOMSI draws into its own texture: road ribbons extruded in the
      vertex shader, per-lane congestion, signals, next stop.
- [ ] Route progress following the bus lane by lane, Dijkstra on leaving the route.
- [ ] Street names recovered from `StreetSign_*` object text — OMSI maps carry none in
      data, so this is how openOMSI gets them too.

---

## Phase 4 — Long game (worked on intermittently)

**This phase is deliberately not scheduled.** It is here so the idea is not lost, not
because it is next. Work on it in small pieces alongside Phases 1–2, never instead of them.
Each item is weeks on its own; together they are the openOMSI-sized problem.

The goal is a **genuinely faster OMSI 2** — the first thing in this roadmap aimed at the
game rather than at our tooling. Nothing here modifies the game or touches DRM.

### 4.0 What the measurements actually say
- [x] **Profile the installation** (Phase 1.9) — establish whether a real install is
      memory-bound, load-bound or CPU-bound *before* building anything. A Vulkan D3DX9 is
      pointless if the bottleneck is loading 589 vehicles off a disk.
- [ ] Re-measure after each change below. Every claim on this page is worth one number.

### 4.1 Vulkan-backed D3DX9 (the DXVK idea)
Replace the 32-bit `d3dx9.dll` with an **x86** DLL that implements the same exported
surface and talks to Vulkan instead. x86, not x64 — see below. This is the highest-ceiling
item in the whole roadmap.

- [ ] Measure a baseline: frame times on one map, one bus, fixed settings.
- [ ] Enumerate the exact `d3dx9` entry points OMSI actually imports (from the import
      table) — the surface is far smaller than the whole library.
- [ ] Implement the subset as a thin x86 shim over Vulkan.
- [ ] Ship it **next to the launcher**, never into the game folder. The user copies it in
      deliberately, and removing it must be a plain file delete.
- [ ] Verify Steam / anti-cheat still starts the game before going further.

**Why x86 and not x64.** `Omsi.exe` is `machine=0x014C`. A 64-bit process cannot load a
64-bit DLL. Any replacement **must** be x86 to be loadable at all — so this buys Vulkan
rendering, *not* 64-bit execution. Those are different problems, and only the first one is
reachable. (openOMSI is the reference: ~175k lines to reach it.)

**What it can and cannot do.** It can change how the game *draws*. It cannot change how the
game *thinks*, cannot make the game 64-bit, and cannot touch `Omsi.exe`.

### 4.2 The 32-bit / 64-bit bridge (`omsi-plugin-host32`)

The one genuinely useful thing openOMSI does that we could copy. Its
`omsi-plugin-host32.exe` is a **separate 32-bit process** that loads a `.opl`+DLL and answers
over stdin/stdout, so a 64-bit program can use a 32-bit plugin. That is a real solution to a
real problem — we just have to be honest that it solves *architecture interoperability*, not
speed.

- [ ] A 32-bit host executable that `LoadLibrary`s a plugin and answers over stdio.
- [ ] One round trip per frame. Never block the game's frame; a stalled host is left out for
      the rest of the session rather than retried.
- [ ] Build it x86 from the same tree. The project is currently x64-only, so this is the
      first thing that needs a second architecture.
- [ ] Keep the two builds in separate folders so neither ships the other's `.lib` files.

**What it will and will not do.** It lets 64-bit code do work the game delegates, outside the
game's 2 GB. It does **not** make the game 64-bit, and it does not speed up the game's own
frame loop — the game keeps running 32-bit either way.

**Only build this if 4.0 says the CPU is the ceiling.** On the profile taken so far
(163 GiB of textures) memory is the ceiling, and no amount of IPC changes that. This is the
item most likely to be the wrong call, which is why it is written down rather than started.

### 4.3 Realistic performance levers, cheapest first
- [ ] **Content tuning** — thinning what loads. Often beats every graphics idea combined on
      a heavy install, and it is supported: `Addons/` already exists in the install.
- [ ] **LOD and texture budget** — the engine already has `_LOW` texture variants.
- [ ] **32-bit shim → 64-bit helper** for anything genuinely CPU-bound. The pattern
      openOMSI uses in `omsi-plugin-host32.exe`: an x86 shim the game loads, forwarding to
      a 64-bit process over IPC. Only worth building once 4.0 says the CPU is the ceiling.

**Note on 4.3's third item:** a 64-bit helper does real work outside the game's 2 GB, but
it cannot make the *game's own loop* faster — the game still runs 32-bit. It pays off for
compute the game delegates, not for work it keeps doing itself.

---

## Deliberately not doing

| | Why |
|---|---|
| Port OMSI 2 to 64-bit | The binary is x86 machine code; the 32-bit-only DLLs (`d3dx9.dll`, `steam_api.dll`) cannot be loaded by a 64-bit process. Not a hard task — an impossible one. |
| Rewrite the renderer | DX9 → Vulkan is what openOMSI spent ~175k lines on. Reimplementing it here buys nothing over using it. |
| Touch DRM | No. Reimplementing `steam_api` calls to launch a purchased copy is circumvention, regardless of where you live. |
| Patch `Omsi.exe` | The 4 GB patch is one bit and `4gb_patch.exe` already ships with the game. We report the flag; the user decides. |
| Copy OMSI content in | No file from a real installation ever enters this repository. Tests build synthetic content. |

---

## Sizing

openOMSI is ~175,000 lines of Rust across 19 crates, 9+ development rounds. It is the
honest measure of what Phase 3–4 cost. Phases 1–2 are a fraction of that, because they
are parsers and a window — the parts testable without the game.

Roughly: **Phase 1 is days, Phase 2 is weeks, Phase 3–4 is the openOMSI-sized problem.**
Exact figures would be invention, so the item boundaries above are the real unit of choice.