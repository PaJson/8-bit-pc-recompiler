# SEGA Master System PC Re-compiler

Native Windows builds of Master System games, made by **static recompilation** of the Z80 ROM
to C with the [smsggrecomp](https://github.com/PaJson/smsggrecomp) engine (a fork of
mstan/smsggrecomp, included as a git submodule), plus optional quality-of-life features written
in C: maps, side panel, speed and reward options, FM / PSG sound, an in-game options overlay
(F1 / pad Start) and a game launcher.

Games: Phantasy Star (Rev A and 6 other official versions, plus the PSRP 2.6.1 retranslations),
Golvellius (+ our own *Golvellius Plus* ROM patch), Miracle Warriors, and Sonic 1 / 2 / Chaos
(those only to test the engine).

**No ROMs are included.** Every build reads its own ROM at run time (CRC32-checked); you supply it.

## Setup on a new PC

1. **MSYS2** in `C:\msys64`, then in an MSYS2 shell:
   `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2`
   (`cmake` is not on the normal PATH; the build scripts add `C:\msys64\mingw64\bin` themselves.)
2. **Python 3** (for the scripts in `Tools/`; run them with `python -I`).
3. **Long paths for git** (one wla-dx test file has a very long name):
   `git config --global core.longpaths true`
4. **Clone with the submodules:**
   `git clone --recursive https://github.com/PaJson/sms-pc-recompiler.git`
   (after a plain clone: `git submodule update --init --recursive`)
5. **Build the recompiler** once (MSYS2 `bin` first on the PATH), in `Recomp/smsggrecomp/recompiler`:
   ```
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/ninja.exe
   cmake --build build
   ```
6. **Copy your ROMs into `ROMs/`**, named exactly as the `rom = "..."` line in each
   `Games/<slug>/game.toml` (No-Intro names, e.g. `Phantasy Star (USA, Europe) (Rev A).sms`).

## Playing

- Double-click **`Launcher.cmd`**: builds the launcher on first use, lists every game whose ROM is
  present, builds a game the first time you play it (a few minutes), then starts it.
- Or per game: `Games/<slug>/build.ps1`, then `Games/<slug>/play.ps1`.
- Keys: arrows, Z / X = buttons 1 / 2, Enter = Pause, F1 = options overlay, F11 = fullscreen,
  Tab = fast forward. Gamepads work too (Start = options overlay).
- Saves go to `Games/<slug>/saves/`.

## Patched ROMs (optional)

Put the patch authors' original downloads into `ROMs/Patches/` (links in `CREDITS.md`), then:
- PSRP 2.6.1 retranslations (need the original **Japan** ROM; the zips go into
  `ROMs/Patches/Phantasy Star/Retranslation 2.6.1 (PSRP, SMS Power)/`): `sh Tools/regen_patched.sh`
  (English) and `sh Tools/regen_patched_langs.sh` (other languages). They write `ROMs/Patched/`.
- Golvellius Plus (our own patch, source in `Patches/Golvellius/`): `python -I Tools/gv_build_patch.py`.

## Not in this repository (git-ignored, copy by hand when moving to another PC)

| What | Where |
|---|---|
| ROMs, patched ROMs, the patch authors' downloads | `ROMs/`, `ROMs/Patched/`, `ROMs/Patches/` |
| Saves (battery saves, map files, passwords) | `Games/*/saves/` |
| Personal settings | `Games/*/ps_qol.ini`, `gv_qol.ini`, `mw_qol.ini`, `launcher.ini` |
| Launcher / cover artwork (SEGA material: never commit) | `Images/` |
| Phantasy Star disassembly (only `Tools/ps_scenario.py` reads it) | `Disasm/ps1disasm-master/` |
| Build output, generated C, release folder | `build*/`, `Games/*/generated/`, `Release/` |

## More

- `CLAUDE.md`: full project notes (status, ROM research, every option, test methods).
- `CREDITS.md`: authors of the patches, tools and research this project uses.
- Engine changes: commit and push in `Recomp/smsggrecomp` first (branch `sms-pc-recompiler`),
  then commit the new submodule pointer here (`git add Recomp/smsggrecomp`).
