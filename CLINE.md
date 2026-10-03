# CLINE.md

Working notes for this project. Read this first in a new session: the project state is on
disk, but these are the things that are *not* written down anywhere else.

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