# 8-bit PC Recompiler

A static recompiler for 8-bit console games (currently only Sega Master System and Sega Game Gear).

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
  present, builds a game the first time you play it (a few minutes), then starts it. A (pad X) builds
  every game that is not built yet, one after the other. Esc (pad B) cancels a build.
- Or per game: `Games/<platform>/<slug>/build.ps1`, then `Games/<platform>/<slug>/play.ps1`.
- Keys: arrows, Z / X = buttons 1 / 2, Enter = Pause, F1 = options overlay, F11 = fullscreen,
  Tab = fast forward. Gamepads work too (Start = options overlay).
- Saves go to `Games/<platform>/<slug>/saves/`.

## Patched games (optional)

The translations and Golvellius Plus run a patched ROM, made from your own original ROM. The patch files
come with the project:
- **Phantasy Star Retranslation 2.6.1** (SMS Power!, 7 languages, 9 versions): the official zips in
  `ThirdParty/sms/Patches/`, unchanged, as their terms require for passing them on. Needs the first
  Japanese ROM, "Phantasy Star (Japan)" (not Rev 1).
- **Golvellius Plus** (our own patch): `Patches/sms/Golvellius/dist/` (source in `Patches/sms/Golvellius/`).
  Needs the original "Golvellius - Valley of Doom" ROM.

Put the patch files into `ROMs/sms/Patches/` (the zips as they are, any subfolder) and the original ROM into
`ROMs/sms/`. The launcher then lists the patched game; the first time you play it, the launcher finds the
patch by its CRC32, writes the patched ROM to `ROMs/sms/Patched/` and builds the game. The release zip
already has the patch files in place.

Without the launcher (developing): `sh Tools/sms/regen_patched.sh` (PSRP English) and
`sh Tools/sms/regen_patched_langs.sh` (the other languages) make the patched ROMs and regenerate their game
folders; they expect the zips in `ROMs/sms/Patches/Phantasy Star/Retranslation 2.6.1 (PSRP, SMS Power)/`.
`python -I Tools/sms/gv_build_patch.py` rebuilds Golvellius Plus from its source and writes the patched ROM.

## Making a release

Releases are built by GitHub Actions from a `release/v<version>` pull request and a `v<version>` tag; the
steps are in `Docs/notes/launcher-and-release.md` ("Release flow"). A local build of the release folder and
zip: `python -I Tools/make_release.py --zip`.

## License

- Our own code (`Common/`, `Launcher/`, `Tools/`, `Patches/`, the build scripts): MIT, see `LICENSE`.
- The engine `Recomp/smsggrecomp` and its core `external/z80-recomp-core` (git submodules) have their
  own licenses (PolyForm Noncommercial 1.0.0, plus the additional permissions in each `PERMISSIONS.md`:
  voluntary donations and advertising are allowed, payment must never buy access), so every game build made
  with them may only be used and shared non-commercially. Third-party parts keep theirs (`CREDITS.md`).

## More

- `Docs/notes/`: full project notes (ROM research, every option, test methods, release flow);
  `CLAUDE.md` lists them and holds the rules for working on the project.
- `CREDITS.md`: authors of the patches, tools and research this project uses.
- Engine changes: commit and push in `Recomp/smsggrecomp` first (branch `sms-pc-recompiler`),
  then commit the new submodule pointer here (`git add Recomp/smsggrecomp`).
