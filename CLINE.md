# CLINE.md

Working notes for this project. Read this first in a new session: the project state is on
disk, but these are the things that are *not* written down anywhere else.

## Start here, in this order

1. **This file** — how to work in this environment, and what the traps are.
2. **`roadmap.md`** — the plan, ordered easiest first, with the reasoning for each phase.
   Read it before proposing work. It holds the four hard constraints and the honest scoping
   of every phase, including which items are deliberately *not* scheduled.
3. **`git log --oneline`, then the commit bodies** — 9 commits, each recording what broke
   and why. Most of the design rationale lives here rather than in the code.

Nothing else needs reading before starting. Do not re-derive the state from the source.

## How Cline loads this file

Cline reads a **`.clinerules/` directory** automatically at session start; it does not load
a file named `CLINE.md` on its own. `.clinerules/CLINE.md` is a pointer to this file so that
the notes are picked up without anyone having to remember to ask for them.

If that pointer is ever missing, ask for this file by name before doing anything else — the
operational notes in it are worth more than any single task.

## Status

Last updated at the end of Phase 1 task 1.1.

### Complete

| Item | Verified by |
|---|---|
| **1.1 CMake presets and CI** | `cmake --preset`, `--build --preset`, `ctest --preset` green locally; both CI runs green |
| **1.9 Installation profile** | `evigit --profile` → 190.92 GiB, verdict: memory-bound |
| **1.8 Audit and clean** | `evigit --audit`, `--clean`, `--restore`; round-trip tested on a synthetic install |
| **PE inspection** (part of 1.7) | `evigit --image`; 12 tests including malformed input |
| Timetable scanning | `.ttp`/`.ttl`/`.ttr` in `maps/<Map>/` — 2022 found on the real install |
| Web front end | 3 cascading pickers, 2022 timetables served over loopback |

**Tests: 91 cases, 317 assertions, all passing.** Run with `ctest --preset test` or
`out\build\x64-local\bin\Release\omsi_tests.exe`.

### Not complete, and worth knowing why

- **1.2 Duty engine** — the duty list is still filename-matched, so it shows wrong duties
  and misses real ones. This is the biggest functional gap in the launcher.
- **1.3 Station names** — station IDs (`382211`) are shown, not names.
- **1.4 Native window** — the launcher still opens in a browser. WebView2 is installed on
  this machine (v154.0.4258.53), and `WebView2Loader.dll` still needs fetching.
- **1.5 Launching** — **Start does nothing.** It is a placeholder.
- **1.7 in the window** — the PE report and the `texmemlimit` advisory exist in the CLI
  only; `/api/library` does not expose them.
- **1.6 Remember selection** — not started.

### Next

**Roadmap 3.1, the `.o3d` mesh reader.** First milestone is **silhouette + orbit camera**:
parse the meshes, render untextured, drag to rotate and wheel to zoom. It needs no GPU for
the parsing half, so it is testable the way everything else here has been.

Before that, Phase 1's unfinished items are worth finishing first — 1.2 and 1.5 in
particular, since they are what makes the launcher usable rather than merely correct.

### CI status — green

Both runs after the 1.1 fixes passed:

```
success  Add CLINE.md: the operational notes that are not in the code
success  Split the Vulkan renderer out of the blocking CI job
```

Three bugs were in the way, and only the first was visible in the code:

1. `CMakePresets.json` named `Visual Studio 18 2026`, a preview generator absent from most
   machines, so every preset failed.
2. CI pinned `humbletim/install-vulkan-sdk@v1.4.1`; the action's newest tag is **v1.2**, so
   every run died during setup before CMake ran. The action version and the SDK version it
   installs had been written as one number.
3. `find_package(Vulkan)` still failed on the runner afterwards, so the renderer moved into
   its own `continue-on-error` job. The main job builds and tests what has tests.

Two lessons from that: **a 7-second CI failure is a setup failure, not a build failure** —
read the log rather than guessing; and the Vulkan renderer is the one part of the project
with no tests, so it should not be able to block every push.

---

## The environment lies about command results

**`run_commands` here routinely returns "error" / "exit code 1" for commands that actually
succeeded**, and occasionally the reverse. Never report a result you have not read.

- Verify against the filesystem: check the output file, check the build artefact exists,
  check the flag file. Do not trust `$LASTEXITCODE`.
- When a long command "fails", its real output is often in the terminal-content dump in the
  same result. Read it before concluding anything.

## Costly mistakes made here, so they are not repeated

**`insert_line` with a guessed line number lands code inside a function.** This happened four
times: `library.cpp`, `audit.cpp`, `profile.cpp`, `test_library.cpp` each ended up with an
unclosed brace, an anonymous namespace closed too early, or code after the file's closing
brace. Each cost a full build-and-poll cycle (~90 s).

Use the editor with explicit `old_text` / `new_text` replacements. Read the surrounding
lines first if the position is not certain.

**Command output is expensive.** `gh run view --log-failed` dumps whole `PATH` environment
variables — hundreds of lines. Write to a file and read only a slice:

```powershell
cmd /c "some-command > C:\Users\weesc\Downloads\out.log 2>&1"
# then read C:\Users\weesc\Downloads\out.log with start_line / end_line
# or: Get-Content ... | Select-String 'pattern'
```

**Builds take 60–120 s.** Run them detached and poll a flag file once, rather than starting
one and blocking:

```powershell
Remove-Item C:\Users\weesc\done.flag -ErrorAction SilentlyContinue
Start-Process cmd.exe -WindowStyle Hidden -ArgumentList '/c','cmake --build out\build\x64-local --config Release > C:\Users\weesc\b.log 2>&1 & echo OK > C:\Users\weesc\done.flag'
```

**Nested quoting breaks.** `Start-Process -ArgumentList '"E:\...\OMSI 2"'` mangles paths
with spaces. Write a `.cmd` wrapper and run that instead — it works every time.

## Traps in this codebase's own subject matter

- **`std::filesystem::path::string()` throws** on a character the console code page cannot
  represent. This installation has `Citea_LLE_120_Voith_€6`, so it throws on real data.
  Narrow from `wstring()` by hand. See `displayName` in `library.cpp`.
- **doctest cannot stringify a scoped `enum class` or a `std::string_view`.** A `CHECK` on
  either fails to compile. Compare the enum's text form, or return `const char*`.
- `.ttf` and `.oft` in an OMSI install are **fonts**, not timetables. Timetables are
  `.ttp` / `.ttl` / `.ttr`, and they live in `maps/<Map>/`, not beside the vehicle.
- Scenery models are `.o3d`, and they live in the shared `Sceneryobjects/` library — never
  in a map folder. Counting `.obj` (a Blender *source* mesh, 25 in the whole install)
  reports zero objects for every map.

## This machine

| | |
|---|---|
| OMSI install | `E:\SteamLibrary\steamapps\common\OMSI 2` (Steam, 16 maps, 589 vehicles, 2022 timetables) |
| Content | 190 GiB, 163 GiB of it textures — **memory-bound** |
| Disk | SATA SSD |
| Compiler | VS 2022 BuildTools. **VS 2026 is not installed** — the presets use `Visual Studio 17 2022` |
| Working build | `out/build/x64-local` (configured by hand, `-DOMSI_WERROR=ON`) |
| Tests | 91 cases / 317 assertions, all passing |

Build and test:

```powershell
cmake --preset x64-debug
cmake --build --preset x64-debug
ctest --preset test
```

## Constraints that shape every design decision

These were measured, not assumed. Do not re-litigate them without new evidence.

1. **The game is 32-bit and cannot be otherwise.** `Omsi.exe` is `machine=0x014C` and loads
   `d3dx9.dll` / `steam_api.dll`, which exist only as 32-bit DLLs. A 64-bit process cannot
   load them. **Do not modify the game.**
2. **Stock OMSI 2 has no Lua.** `omsi.position()` is an *openOMSI* addition, so an
   out-of-process tool cannot learn the player's position. A live GPS needs a minimal 32-bit
   plugin DLL (roadmap 4.2).
3. **`[texmemlimit]` and the large-address-aware flag are the same setting.** The flag gives
   the process 4 GB instead of 2; the budget is texture memory in MB. Stock is 401 MB, this
   install asks for 1001 MB. The loader reads the flag from the file, so it cannot be set at
   run time and a game update clears it. **Report it, do not patch it.**
4. Nothing is written into the installation. `--clean` *moves* files to a folder the user
   names and `--restore` moves them back; it never deletes.

## Where the state of the project is

- **`roadmap.md`** — the plan, ordered easiest first, with the reasoning for each phase.
- **`git log`** — 8 commits, each recording what broke and why. Read this before proposing
  a change; most of the design rationale lives here, not in the code.

## Next task

Roadmap **3.1, the `.o3d` mesh reader** — pure parsing, no GPU, so it is testable:

```
84 19  magic
ver    u8   (1,3,4,5,7 seen)
[flags] u8  (ver >= 3)  bit0: 32-bit triangle indices
[key]  u32  (ver >= 4)
sections, one tag byte each, until EOF:
  0x17 vertices  count × {x y z nx ny nz u v} f32
  0x49 triangles count × {i0 i1 i2, material u16}
  0x26 materials count × {diffuse rgba, specular rgb, emissive rgb, power, name}
  0x79 matrix    16 × f32
  0x54 bones     count × {name, weights}
```

Goal for the first milestone: **silhouette + orbit camera** — parse the meshes, render them
untextured, drag to rotate and wheel to zoom. Textures come after, and doors/wheels/steering
much later.