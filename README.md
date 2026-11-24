# 8-bit PC Recompiler

A static recompiler for 8-bit Sega games (Master System, Game Gear).

Native Windows builds of Master System games, made by **static recompilation** of the Z80 ROM
to C with the [smsggrecomp](https://github.com/PaJson/smsggrecomp) engine (a fork of
mstan/smsggrecomp, included as a git submodule), plus optional quality-of-life features written
in C: maps, side panel, speed and reward options, FM / PSG sound, an in-game options overlay
(F1 / pad Start) and a game launcher.

Games: Phantasy Star (Rev A and 6 other official versions, plus the PSRP 2.6.1 retranslations),
Golvellius (+ our own *Golvellius Plus* ROM patch), Miracle Warriors, and Sonic 1 / 2 / Chaos
(those only to test the engine).

**No ROMs are included.** Every build reads its own ROM at run time (CRC32-checked); you supply it.

Not affiliated with or endorsed by Sega. Game names are trademarks of their owners and are used only to
say which game a profile is for.

## Just playing: the release zip

Download `8-bit PC Recompiler v<version>.zip` from the Releases page, unzip it anywhere, put your own
ROMs into `ROMs\sms\` (see `ROMs\README.txt`) and start `Launcher.exe`. Nothing else is needed: the zip
holds the launcher, the recompiler, a C compiler (complete MSYS2 gcc packages; versions and sources in
`Engine\Compiler\SOURCES.txt`) and the prebuilt engine runner. The launcher shows each game's title
screen taken from your ROM, and the first time you play a game it builds it on your PC from your ROM
(1-2 minutes, Golvellius about 5). The zip contains no game programs and nothing taken from any ROM.

## Setup on a new PC (developing)

1. **MSYS2** in `C:\msys64`, then in an MSYS2 shell:
   `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2`
   (`cmake` is not on the normal PATH; the build scripts add `C:\msys64\mingw64\bin` themselves.)
2. **Python 3** (for the scripts in `Tools/`; run them with `python -I`).
3. **Long paths for git** (one wla-dx test file has a very long name):
   `git config --global core.longpaths true`
4. **Clone with the submodules:**
   `git clone --recursive https://github.com/PaJson/8-bit-pc-recompiler.git`
   (after a plain clone: `git submodule update --init --recursive`)
5. **Build the recompiler** once (MSYS2 `bin` first on the PATH), in `Recomp/smsggrecomp/recompiler`:
   ```
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/ninja.exe
   cmake --build build
   ```
6. **Copy your ROMs into `ROMs/sms/` (or `ROMs/gg/`)**, named exactly as the `rom = "..."` line in each
   `Games/<platform>/<slug>/game.toml` (No-Intro names, e.g. `Phantasy Star (USA, Europe) (Rev A).sms`).

## Playing

- Double-click **`Launcher.cmd`**: builds the launcher on first use, lists every game whose ROM is
  present, builds a game the first time you play it (a few minutes), then starts it.
- Or per game: `Games/<platform>/<slug>/build.ps1`, then `Games/<platform>/<slug>/play.ps1`.
- Keys: arrows, Z / X = buttons 1 / 2, Enter = Pause, F1 = options overlay, F11 = fullscreen,
  Tab = fast forward. Gamepads work too (Start = options overlay).
- Saves go to `Games/<platform>/<slug>/saves/`.

## Patched ROMs (optional)

(The launcher can also make them itself: it finds the patch by CRC32 in `ROMs/sms/Patches/`. The PSRP
2.6.1 zips are also in `ThirdParty/`, unchanged, because their terms allow passing them on; the release
ships them.)
Put the patch authors' original downloads into `ROMs/sms/Patches/` (links in `CREDITS.md`), then:
- PSRP 2.6.1 retranslations (need the original **Japan** ROM; the zips go into
  `ROMs/sms/Patches/Phantasy Star/Retranslation 2.6.1 (PSRP, SMS Power)/`): `sh Tools/sms/regen_patched.sh`
  (English) and `sh Tools/sms/regen_patched_langs.sh` (other languages). They write `ROMs/sms/Patched/`.
- Golvellius Plus (our own patch, source in `Patches/sms/Golvellius/`, the built .ips / .bps committed in
  `Patches/sms/Golvellius/dist/`): `python -I Tools/sms/gv_build_patch.py`.

## Making a release (maintainers)

1. Branch `release/v<version>`: set `version` in `app_info.toml`, add a `## v<version>` section to
   `CHANGELOG.md` (it becomes the release text).
2. Open a pull request to `main`. The `Check` workflow (`.github/workflows/check.yml`) runs the engine
   self-tests, builds the release zip with `Tools/make_release.py` (ends with a smoke test: a tiny ROM of
   our own built and run with only the release on the PATH) and attaches the zip to the run. Test that
   zip, e.g. in Windows Sandbox: `Tools\sandbox_test.ps1 -Zip <zip>`.
3. Merge, then push the tag `v<version>`: `.github/workflows/release.yml` builds the zip again and
   publishes the GitHub Release.

Locally: `python -I Tools/make_release.py --zip` -> `Release/<name> v<version>/` + `.zip`. Details:
`Docs/notes/launcher-and-release.md`.

## Not in this repository (git-ignored, copy by hand when moving to another PC)

| What | Where |
|---|---|
| ROMs, patched ROMs, the patch authors' downloads | `ROMs/`, `ROMs/sms/Patched/`, `ROMs/sms/Patches/` |
| Saves (battery saves, map files, passwords) | `Games/*/*/saves/` |
| Personal settings | `Games/*/*/ps_qol.ini`, `gv_qol.ini`, `mw_qol.ini`, `launcher.ini` |
| Launcher / cover artwork (SEGA material: never commit) | `Images/` |
| Phantasy Star disassembly (only `Tools/sms/ps_scenario.py` reads it) | `Disasm/sms/ps1disasm-master/` |
| Build output, generated C, release folder | `build*/`, `Games/*/*/generated/`, `Release/` |

## Licence

- Our own code (`Common/`, `Launcher/`, `Tools/`, `Patches/`, the build scripts): MIT, see `LICENSE`.
- The engine `Recomp/smsggrecomp` and its core `external/z80-recomp-core` (git submodules) have their
  own licences (PolyForm Noncommercial 1.0.0, plus the additional permissions in each `PERMISSIONS.md`:
  voluntary donations and advertising are allowed, payment must never buy access), so every game build made
  with them may only be used and shared non-commercially. Third-party parts keep theirs (`CREDITS.md`).

## More

- `Docs/notes/`: full project notes (ROM research, every option, test methods, release flow);
  `CLAUDE.md` lists them and holds the rules for working on the project.
- `CREDITS.md`: authors of the patches, tools and research this project uses.
- Engine changes: commit and push in `Recomp/smsggrecomp` first (branch `sms-pc-recompiler`),
  then commit the new submodule pointer here (`git add Recomp/smsggrecomp`).
