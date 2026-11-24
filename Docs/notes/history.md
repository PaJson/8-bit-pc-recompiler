# Project history: folders, repos, licences, old plans

*Moved here word for word from CLAUDE.md on 2026-11-24 (the dates in the text are when each note was written; later notes can correct earlier ones). CLAUDE.md keeps the rules for every task and lists all notes files.*

# CLAUDE.md — Phantasy Star (SMS) PC Remake with SEGA AGES-style QoL

Project-level notes. The engine repo has its own `smsggrecomp/CLAUDE.md` + `PRINCIPLES.md`
(read those before touching the recompiler). **Never commit without explicit user instruction.**

## Goal

Native PC build of the 1987 Phantasy Star (Sega Master System) made by **static recompilation**
of the Z80 ROM to C (mstan's `smsggrecomp`), then add SEGA-AGES-style quality-of-life features
in C/SDL2 on top (first: a real-time 2D dungeon auto-map overlay; later: speed/encounter/XP
options, FM audio).

Base ROM = **original Rev A**. The SEGA AGES ROM is only a comparison/diff target.

## Platform layout (2026-11-23, user: the project is "8-bit": more consoles later)

One folder level per console (`sms`, `gg`, later `nes`, `gb` ...; lower case = the `platform = "sms"` key of `game.toml`):
`Games/<platform>/<slug>/`, `ROMs/<platform>/` (with `Patched/` + `Patches/` inside), `Common/<platform>/<Game>/`,
`Patches/<platform>/<Game>/` (our own ROM patches). Engines live in `Recomp/<engine>/` (submodules; today only `smsggrecomp` = Z80:
SMS + GG; `Tools/mkgame.py` `ENGINES` maps platform -> engine, its templates are Z80 / SMS ones). `Tools/` and `Launcher/` stay shared and flat.
Same level for the rest (later the same day, user's request): `Disasm/<platform>/ps1disasm-master`, `Bugs/<platform>/`, `Images/<platform>/` (the
`<title> (front|full|logo)` art; `Images/Backround.jpg`, the launcher background, stays in `Images/`; engine `art_find` searches `Images/` and one level
of subfolders), `Tools/<platform>/` (everything Phantasy Star / Golvellius / Z80 specific: `ps_*`, `gv_*`, `regen_*`, `find_ptr_tables`, `sms_checksum`,
`unrolled_runs`, `z80dis.c`, `rom_inventory`, `stack_shapes`, `templates/` = the `mkgame` game.toml templates + `generic_press.txt`); shared tools stay in
`Tools/` (`mkgame.py`, `make_release.py`, `lockstep.sh`, `verify_game.sh`, `coverage.py`, `apply_patch.py`, `rename_project.ps1`, `test_input/`).
**NOT split by platform (decision 2026-11-23, user may overrule):** `Recomp/` (one engine folder per CPU family, named after it: `smsggrecomp` serves
sms AND gg, so a platform level above would be wrong or duplicate it; a future NES engine = `Recomp/nesrecomp`) and `External/` (`wla-dx` is a
multi-CPU assembler, not an sms thing).
**State after the move (2026-11-23):** all 24 games rebuilt from scratch in `Games/sms/` (every one OK; 246 / 279 s for Golvellius / Plus, 60-120 s the rest, two at a time); 3000 frames of `rand1` headless on all 24: 0 dispatch misses except Sonic 2 (28), Sonic 2 Rev 1 (75), Sonic Chaos (20) = the known hybrid misses of the never table-analysed Sonic builds (not a regression). Patched-ROM games need a Windows path (`cygpath -m`) when run from bash: MSYS mangles `[ ]` in file names ("cannot open ROM"). `Tools/sms/regen_patched*.sh` + `gv_build_patch.py` + `make_release.py` re-run after the move: game folders byte-identical, Golvellius Plus CRC32 `9D195ED1`, Release = 24 games with the new `Release/Games/sms` / `Release/ROMs/sms` layout (an older flat Release must be deleted). `ROMs/sms/Golden Axe Warrior (USA, Europe, Brazil) (En).sms` (user, 2026-11-23: "for future work"): no game folder yet, the launcher lists it grey ("no build profile"). The 16-bit sister project uses the same convention with `smd` (see its CLAUDE.md).
**Engine licence merged (2026-11-23):** `mstan/smsggrecomp` main got its licence on 2026-11-22 (commit `07e2c28`: PolyForm Noncommercial 1.0.0 + `PERMISSIONS.md` = voluntary donations / advertising allowed, payment must never buy access; `tools/package_game_release.ps1`, README licence text; its `z80-recomp-core` pin `bdd1c30` was already ours). Merged into our branch `sms-pc-recompiler` with a merge commit (`git remote upstream` = `https://github.com/mstan/smsggrecomp.git`; only 1 upstream commit was missing, ours was 43 ahead; auto-merge, no conflicts, docs / licence files / one script only). **The condition the user set on 2026-11-22 for making the project repo public ("his LICENSE file is in `mstan/smsggrecomp`") is now met; the user has not decided to go public yet** (see the public-repo notes above: README disclaimer, title-image capture and art defaults (c)-(d) were never done).
**New forks (user, 2026-11-23), not used yet:** `PaJson/RetroRecomp` (fork of `ArthurReboulSalze/RetroRecomp`, MIT, v1.9.1: one front-end + packager over mstan's / RetroPortingToolKit's recompilers for 20 machines incl. NES, Game Boy, SMS, GG, Mega Drive, with rollback netplay; a reference for the NES / Game Boy engines and a front-end, not a dependency) and `PaJson/SGDK` (fork of Stephane-D's Sega Mega Drive dev kit, MIT; aim: port "our" Phantasy Star to the Mega Drive / SMD = a new 68000 program written with SGDK that would read its data from the user's own SMS ROM; nothing started, not a recompile).
**Third sister project (2026-11-23): `..\Algol-MD`** = the user's idea of porting "our" Phantasy Star to the Mega Drive as a NEW SGDK program (C, 68000; `External/SGDK` = fork `PaJson/SGDK`). It reads this project's knowledge of the Rev A ROM (map / tile / dungeon formats, RAM, text; `Common/sms/PhantasyStar/ps_worldmap.c` etc. are the templates for its extractors). Skeleton builds `out/rom.bin`; see its CLAUDE.md.
**Older notes in this file use the flat paths** (`Games/<slug>`, `ROMs/...`, `Common/PhantasyStar`, `Patches/Golvellius`, `Tools/ps_find_hooks.py`,
`Disasm/ps1disasm-master`): read them as `Games/sms/<slug>`, `ROMs/sms/...`, `Common/sms/PhantasyStar`, `Patches/sms/Golvellius`, `Tools/sms/ps_find_hooks.py`,
`Disasm/sms/ps1disasm-master`. Relative paths from a game folder: ROM
`../../../ROMs/sms/<file>`, engine `../../../Recomp/smsggrecomp`, hooks `../../../Common/sms/<Game>/`.
Done in the move: all 24 game folders (`git mv`; build folders repaired with `rename_project.ps1 -RepairOnly`, each recompiles once on its next
build; `Games/sms/ps-reva` rebuilt + 3000 frames of `rand1` 0 misses, lockstep 3000 final SAME), `mkgame.py --platform` (default sms), `lockstep.sh` /
`ps_suite.sh` / `verify_game.sh` / `regen_all.sh` / `coverage.py` (find `Games/*/<slug>`), `regen_patched*.sh`, `gv_build_patch.py`, `ps_find_tables.py`,
`make_release.py` (per platform: `Release/Games/<platform>/...`, `Release/ROMs/<platform>/...`: an OLD `Release/` folder has the flat layout: delete it and re-run),
the launcher (scans `Games\<platform>\<slug>`, patches in every `ROMs\<platform>\Patches`, patched ROMs to `ROMs\<platform>\Patched`), `.gitignore`, README.
The launcher's ROM index is by CRC32 and does not care about folders. Adding a console: engine submodule in `Recomp/`, an `ENGINES` entry +
templates in `mkgame.py`, then the same folders. Candidate engines (user's forks, 2026-11-23): 8-bit `PaJson/nesrecomp`, `PaJson/gb-recompiled`
(nothing checked yet: licence, CPU coverage, runner API); 16-bit-or-more ones belong to the 16-bit project / elsewhere (`segagenesisrecomp`,
`snesrecomp`, `gbarecomp`).

## Layout (project root = this folder)

**Renaming the project folder** (user wish 2026-11-15: "SEGA Master System PC Re-compiler"; 2026-11-22: "SMS PC Recompiler", renamed by the user the same day; later 2026-11-22: "8-bit PC Recompiler" = the script's default now): run `Tools/rename_project.ps1` from a plain PowerShell window with VS Code, Claude Code, the launcher and all games closed. It renames the folder, copies Claude Code's project data (`%USERPROFILE%\.claude\projects\<path with non-alphanumerics as '-'>`: memory + conversations; old copy kept) and repairs every CMake build folder in place (old path replaced in CMakeCache.txt, CMakeFiles/ + ninja state removed, reconfigured with its own settings; `-Rebuild` also builds all). Tested on a scratch copy (launcher build renamed into a name with `#` and spaces, then built). Nothing else stores the absolute path (sources, scripts, git incl. the submodule, saves, ini files). Done by the user 2026-11-15.
**Movable project (2026-11-15):** the exes (games, launcher, SmsRecomp) contain no absolute paths (checked) and find everything relative to themselves / the cwd the launcher sets, so playing works from any location. Only CMake build folders store the path; engine `tools/repair_moved_build.ps1 -Build <dir> [-Configure]` repairs one in place (old location from `CMAKE_CACHEFILE_DIR`, the changed part = the path minus the trailing folders both share, replaced in the cache, CMakeFiles/ + ninja state removed, settings kept; no-op when not moved). It runs automatically before every game build (`build_game.ps1`) and launcher build (`Launcher/build.ps1`; `Launcher.cmd` falls back to it when the quick `cmake --build` fails), so after a move the project needs no script at all; the first build of each game after a move recompiles it fully (ninja compares compile commands, which contain the paths). `rename_project.ps1` uses the same helper; it is still the way to keep Claude Code's project data and to repair the hand-made build folders (recompiler, diagnostic builds).
**Moved / renamed 2026-11-22 and 2026-11-23 (user):** the development folder was moved and the project folders renamed (no spaces or `#` in the path any more; now `8-bit-PC-Recompiler`). Each time repaired with `rename_project.ps1 -RepairOnly` (+ Claude Code's project data) and checked: every CMakeCache.txt names the new path, recompiler + launcher build, `Games/ps-reva` rebuilt fully, 3000 frames of `rand1`: 0 misses, 100 % static. The app / window name stays "8-bit PC Recompiler" (only folders changed).

| Path | What |
|---|---|
| `Recomp/smsggrecomp/` | **Fork of mstan/smsggrecomp** (PaJson/smsggrecomp). **Git submodule of the project since 2026-11-22** (branch `sms-pc-recompiler`; clone with `git clone --recursive`, or `git submodule update --init --recursive`): each project commit pins the engine commit. After engine changes: commit + push in the engine first, then commit the new pointer (`git add Recomp/smsggrecomp`) in the project. Recompiler + shared runner. Submodule `external/z80-recomp-core` -> PaJson fork. (Moved here from the root 2026-11-12.) |
| (`Recomp/z80-recomp-core/`) | Was a redundant clone of the core fork (the submodule `Recomp/smsggrecomp/external/z80-recomp-core` is what builds); its one extra commit (upstream licence texts, `bdd1c30`) was moved into the submodule 2026-11-15, then the clone could be deleted. |
| `ROMs/<platform>/` (today `ROMs/sms/`) | All ROMs (`*.sms`, gitignore!): 8 Phantasy Star variants, Sonic 1/2/Chaos, Golvellius, Miracle Warriors. `ROMs/Patched/` = ROMs made with `Tools/apply_patch.py` (our own IPS/BPS applier) from `ROMs/Patches/`. |
| `ROMs/sms/Patches/` (git-ignored; moved from `ROMPatches/` and cleaned up with the user 2026-11-15, 350 MB -> 3.4 MB) | **The authors' original downloads only** (zip / rar + their description .txt; links in CREDITS.md, no .url / .pdf), one folder per hack named `<hack> <version> (<author>)`: `Golvellius - Valley of Doom/{Improvement Hacks v1.1 (Alianger), SRAM save with 6 enemies 1.0 (Ichigobankai, LemFliggity)}/` (Eric R. Quinn's Bugfix 1.1 is bundled complete and byte-identical inside Alianger's zip, folder `Golvellius-SMS-Bugfix-1.1/`, its IPS renamed `Golvellius map fix.ips`; so its own folder was dropped. None of Alianger's own patches contain those two bytes; our MAP FIXES writes them itself) and `Phantasy Star/{Retranslation 2.6.1 (PSRP, SMS Power) (PhantasyStar-SMS-{EN,FR,ca,de,es,la,pt-br}-2.6.1.zip: each = ps1jert.<lang>.bps/.ips (+ .literal for EN) + readme / technical notes; base = the original **Japan** ROM)}/` (the sprite hacks Brunette Alis, CSI and Makeover were dropped too: the user only wants dialogue / name changes, not graphics makeovers). Nothing reads fixed paths in it: the Golvellius options find a patch by CRC32 + size, loose or INSIDE a zip (`gv_patch.c`); `Tools/apply_patch.py` takes `"<zip>::<file>"`. Dropped with the user: the PSRP source snapshots (no 2.7 beta: wait for the official release), the Daedalus007 fork (its 2025 snapshot = a pure copy of PSRP; the 2022 one = a "Daedalus Edition" script mod of 2.2), vivify93's "less exaggerated" pre-patched ROM (no patch / source), the old SMS Power releases 0.90-2.5 and the 2.0 / 2.01 New Sprites patches. |
| (`ExternalTools/`) | Held FLIPS 1.31 for a few hours on 2026-11-15; replaced by our own `Tools/apply_patch.py` (IPS + BPS, BPS CRC32s checked, several patches in a row; output byte-identical to FLIPS for all four `ROMs/Patched/` ROMs from both the BPS and IPS files). No third-party tools are needed any more; the user keeps Emulicious / FLIPS outside the project. |
| `Games/<platform>/<slug>/` | **One folder per ROM, generated by `Tools/mkgame.py`** (see "Multi-ROM workflow"): `game.toml`, `CMakeLists.txt`, `build.ps1`, `check.ps1`, `play.ps1`, `build/`, `generated/`, `saves/`. The ROM stays in `ROMs/`. |
| `Common/sms/PhantasyStar/` (+ `Golvellius/`, `MiracleWarriors/`) | Shared PS hooks (`ps_automap.c`, `ps_qol.c`, `ps_qol.ini.sample`) (soak input scripts moved to `Tools/test_input/rand{1,2,3}.txt` 2026-11-15). |
| `Tools/` | `mkgame.py`, `ps_find_tables.py`, `rom_inventory.py`, `regen_patched.sh`, `verify_game.sh`, `templates/`, `test_input/` (random-input scripts `rand{1,2,3}.txt` for soaks / lockstep, used for every game; was `Common/PhantasyStar/cmp/`). |
| `Disasm/ps1disasm-master/` | **The disassembly we use** (has `ps1.constants.asm` with named RAM vars/dungeon IDs, `sound/sound_driver.asm`, `ps1original.sms`; `Tools/ps_scenario.py` reads its `banks/bank03.asm`). The older copies `PS1_Disasm/`, `PS_Disasm/` and the old per-game dirs `PhantasyStarSMSRecomp/`, `PhantasyStarAgesRecomp/`, `_template_sonic/`, `Masterlator/` were deleted by the user (2026-11-15). |
| ROMs `Phantasy Star (USA, Europe) (Rev A).sms` etc. | Rev A MD5 `1110938DF80F4E44C8213D7F85CFB5E6`, CRC32 `00BEF1D7`, 512 KB. AGES En = 540,672 B, AGES Ja = 565,248 B (headers parse; sizes not multiples of 16 KB). |

## Toolchain (installed this session)

MSYS2 at `C:\msys64` with mingw64 gcc 16, cmake, ninja, SDL2. `cmake` is NOT on the normal PATH:
always `export PATH=/c/msys64/mingw64/bin:$PATH` (bash) / `$env:PATH="C:\msys64\mingw64\bin;$env:PATH"`.
The Python used for helper scripts should be run with `-I`.

## Repos, GitHub, fresh clone (2026-11-22)

| Repo | Where | Branch |
|---|---|---|
| Project (this folder) | **private** GitHub `PaJson/8-bit-pc-recompiler` (created 2026-11-22 as `sms-pc-recompiler`, renamed the same day; GitHub redirects the old URL; history rewritten once before the first push to drop `Logos/`) | `main` |
| Engine `Recomp/smsggrecomp` (submodule) | fork `PaJson/smsggrecomp` of `mstan/smsggrecomp` | `sms-pc-recompiler` (our work, was `ps-qol-night2`); the fork's `main` = mstan's upstream, untouched |
| Core `Recomp/smsggrecomp/external/z80-recomp-core` (engine's submodule) | `PaJson/z80-recomp-core` | detached at `bdd1c30` = its `main` |
| `External/wla-dx` (submodule, Golvellius Plus assembler) | `PaJson/wla-dx` | `fceeb84` |

**Commit / push order after engine changes:** commit + push in `Recomp/smsggrecomp` (plain `git push`, the branch tracks origin), then `git add Recomp/smsggrecomp` + commit + push in the project. Never commit or push without the user's go-ahead.

**New PC / fresh clone:**
1. MSYS2 in `C:\msys64` with `mingw-w64-x86_64-gcc cmake ninja SDL2` (here: gcc 16.2, cmake 4.4.5, ninja 1.13.2, SDL2 2.32.10); Python 3 (here 3.14) for `Tools/`; `gh` for GitHub.
2. `git config --global core.longpaths true` (set here 2026-11-22): a wla-dx test file has a 140-character name; without it a clone into a deep folder stops with "Filename too long".
3. `git clone --recursive https://github.com/PaJson/8-bit-pc-recompiler.git` (or `git submodule update --init --recursive` after a plain clone).
4. Recompiler: in `Recomp/smsggrecomp/recompiler`: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/ninja.exe` then `cmake --build build` (PATH with `C:\msys64\mingw64\bin` first).
5. Copy the ROMs into `ROMs/` (names as in each `Games/<slug>/game.toml`; the launcher matches by CRC32), then `Games/<slug>/build.ps1` or `Launcher.cmd` (builds the launcher, then games on first Play).

**Not in the repo (git-ignored; copy by hand if needed):** `ROMs/` (all ROMs, `ROMs/Patched/` regenerable with `Tools/regen_patched.sh` / the launcher), `ROMs/Patches/` (the authors' original downloads), `Games/*/saves/` + `*.sav` etc., personal ini files (`ps_qol.ini`, `gv_qol.ini`, `mw_qol.ini`, `launcher.ini`), `Images/` (the user's SEGA artwork: never commit), `Disasm/ps1disasm-master/` (only `Tools/ps_scenario.py` reads it), `cega/`, `Release/`, every `build*/` and `generated/` folder.

**Clone test 2026-11-22 (passed):** fresh `git clone --recursive` from GitHub into the scratchpad, Rev A ROM copied into `ROMs/`, steps 4-5 above: recompiler built, `Games/ps-reva/build.ps1` regenerated + built `PsRevaRecomp.exe`, 3000 frames of `rand1` headless: 0 dispatch misses, 100 % static; `Launcher/build.ps1` built `Launcher.exe`. Not tested from a clone: other games, patched ROMs, `Tools/` scripts.

**Upstream PR (open question, 2026-11-22):** `mstan/smsggrecomp` has no licence yet (README: "Not yet declared"), only mstan's own PRs (#1-#3), last push 2026-11-08 = the base of our branch (no newer upstream commits). Our branch = 41 commits, 41 files, +15.9k / -3.0k lines, mixing generic fixes (recompiler idioms, discovery order, VDP sprite priority + sprite Y wrap, NMI Pause, SRAM, YM2413) with our features (HUD, side panel, `[insn_hooks]`, art, test env vars). Recommendation given to the user: no single big PR; ask mstan first (issue or the R.A.I.D. Discord named in his README), then small PRs, VDP fixes first. User has not decided.

> Note 2026-11-24: settled: mstan's licence (PolyForm Noncommercial 1.0.0 + PERMISSIONS.md) was merged on 2026-11-23, see "Engine licence merged" under "Platform layout" in this file.

**Making the project repo public (discussion 2026-11-22, NOTHING done yet; continue here).** User asked: public repo, a name without the copyright / trademark problems, what to do with logos / covers. Facts checked:
- Tracked files: no picture / ROM / IPS / BPS / zip / wav in `git ls-files` (`Images/`, `ROMs/` ignored; `Logos/` already purged from history). Top level tracked: `.claude/agents`, `.gitattributes`, `.gitignore`, `.gitmodules`, `CLAUDE.md`, `CREDITS.md`, `Common/`, `External/` (wla-dx submodule), `Games/` (120 files: game.toml, CMakeLists, ps1), `Launcher/`, `Launcher.cmd`, `Patches/`, `README.md`, `Recomp/` (submodule), `Tools/`.
- Licences: engine `mstan/smsggrecomp` = **none** ("Not yet declared"; fork `PaJson/smsggrecomp` is PUBLIC already, as is `PaJson/z80-recomp-core` and `PaJson/wla-dx`); `z80-recomp-core` = **PolyForm Noncommercial 1.0.0** (`LICENSES/SUPERZAZU-MIT.txt` for the vendored Z80); emu2413 MIT, stb public domain / MIT, wla-dx GPL (tool only).

> Note 2026-11-24: outdated: `mstan/smsggrecomp` has had its licence since 2026-11-22 (merged here 2026-11-23).
- "SEGA Master System PC Re-compiler" was hard-coded as the launcher window title (`Launcher/launcher.c` `SDL_CreateWindow`; renamed 2026-11-22); the generated fallback background is neutral ("PC RECOMPILER / 8-BIT CONSOLE CLASSICS").
- Copyright note told the user: a title screen captured from the ROM is still the game's artwork; acceptable only because it is made on the player's PC from their own ROM (like the exe) and never shipped. The recompiler cannot render it: it needs a headless run of the built game + frame dump, frame per game (PS title ~1500).
Recommendation given (user has NOT chosen yet; the user asked to /clear and continue in a few hours):
1. Licence first: ask mstan to declare a licence (same message as the upstream-PR question); own code (`Common/`, `Launcher/`, `Tools/`, `Patches/`) e.g. MIT; the whole stays non-commercial because of the PolyForm core.
2. Name: no "SEGA" in the product name, only descriptive ("a static recompiler for Sega Master System games"); keep repo name `sms-pc-recompiler`, app title e.g. "SMS PC Recompiler" (or a own name: "Recomp8", "Native8"; user's pick). Rename = launcher title, README, project folder (`Tools/rename_project.ps1`).
3. Art: keep the art code (reads only the user's own `Images/`), but default = generated neutral launcher background + a title screen captured from the player's ROM after each build (stored beside the exe), used in the launcher instead of the cover and in the HUD header / PS panel title instead of the logo (or text only).
4. Hygiene: README disclaimer (not affiliated with Sega, no ROMs, own legal copies); optionally git noreply email for future commits (old commits would need a history rewrite in both repos); trim / move `CLAUDE.md` if wanted; drop `.claude/agents` if personal.
Proposed order: (a) draft message to mstan, (b) rename, (c) title-image capture + drop the cover/logo defaults, (d) README disclaimer + cleanup. Ask which parts to do before starting.
**Status 2026-11-22 (later):** (a) DONE: the user asked mstan for a licence for smsggrecomp and offered the general fixes as small PRs. **mstan answered (2026-11-22):** he uses PolyForm Noncommercial for his projects, publishing is fine as long as the games are not sold, and issues / PRs upstream are welcome. His LICENSE file followed (see "Engine licence merged"). **User decisions (2026-11-22):** keep the project repo private until his LICENSE file is in `mstan/smsggrecomp` (no PR from us for it; check `gh api repos/mstan/smsggrecomp/contents` for a LICENSE before going public); app name **"SMS PC Recompiler"**; commit email stays as is (no noreply, no history rewrite); own code = **MIT** (root `LICENSE`, "Copyright (c) 2026 Patrik Jansson"; README "Licence" section: our dirs MIT, engine + core PolyForm NC, so builds are non-commercial). **(b) rename DONE in the files (2026-11-22):** launcher window title, README heading + "A static recompiler for Sega Master System games.", `Tools/make_release.py` README text, `Tools/rename_project.ps1` default `-NewName` = "SMS PC Recompiler"; launcher rebuilt. **Project folder renamed by the user (2026-11-22) to `...\Projects\SMS PC Recompiler`** with `Tools/rename_project.ps1` (Claude Code data copied: this conversation continued in the new folder). **Renamed again (2026-11-22, user): "8-bit PC Recompiler"** (launcher title, README heading + "A static recompiler for 8-bit Sega games (Master System, Game Gear).", Release README, `rename_project.ps1` default, GitHub repo `PaJson/8-bit-pc-recompiler`; the engine branch `sms-pc-recompiler` stays: the engine is SMS / GG only). Reason: a sister project "16-bit PC Recompiler" (Genesis, mstan's segagenesisrecomp fork + the PS2 disassembly) is planned as its own repo / folder next to this one. **Folder renamed by the user to `...\Projects\8-bit PC Recompiler` (2026-11-22). Sister project created the same day:** `...\Projects\16-bit PC Recompiler`, private GitHub `PaJson/16-bit-pc-recompiler` (framework fork `PaJson/segagenesisrecomp` as submodule `Recomp/segagenesisrecomp`; new fork `PaJson/m68k-recomp-core`; mstan's branch `feature/segacd-ordered-z80-bus` copied into `PaJson/z80-recomp-core` because the framework pins `4dfc5fc` from it; `Disasm/ps2disasm` = local clone of innocenthedgehog/ps2disasm, git-ignored; fresh `git clone --recursive` from GitHub tested OK). Its own CLAUDE.md has the details. (c)-(d) not started. What we use of smsggrecomp (checked): the recompiler + the runner (no licence yet = the blocker), vendored `runner/external/superzazu/z80.c` (MIT, Nicolas Allemand, upstream `d64fe10` = still superzazu/z80's latest; linked into every game exe for `--interp` / the hybrid fallback; `VENDORED.txt` now lists our two local changes: block-I/O flags `71f869a` (mstan's) and the `LD A,R` test hook `e94c4b3`; engine commit `63c1045`), `external/z80-recomp-core` headers (PolyForm NC; `runner/include/sms_runtime.h` / `z80_ops.h` only include them).

## Plan (updated 2026-11-11, later)

DONE: dispatch misses (0), window title, SRAM model, auto-map v1, AGES ROM analysis + recompile, FM audio (YM2413) wiring.
TODO, roughly in priority order:
1. **Human verification** (the user): play `PhantasyStarSMSRecomp/build/PhantasyStarSMSRecomp.exe ../phantasystar.sms --window 3` (run from PhantasyStarSMSRecomp/build) and the AGES build; check video, controls, music/FM balance, the auto-map window in a real dungeon, and that CONTINUE/SAVE works with the new SRAM (`<rom>.sav` next to the ROM).
2. Play-through coverage beyond ~11000 frames of "press A": battles, overworld, towns, shops, Road/Ship modes, multi-bank code; resolve any new dispatch misses (`dispatch_misses.log`) via static analysis. A smarter input script / route file helps (`--press-file`, `--poke`).
3. Auto-map v2: persist explored cells to disk, embed in the main window (side panel) instead of a second window, legend/cell types (verify 4-7, `$0C/$0D` stairs), overworld/road modes, toggle key.
4. QoL options (C hooks, not ROM patches; config file or CLI): speed multiplier (frame cap), encounter rate (dungeon: `$C2E3`; overworld rate comes from ROM tables read in `Map_RunRandomBattles` - needs a code-level hook), EXP/Meseta multipliers (`CurrentBattle_EXPReward $C2D0`), text speed. All must stay optional.
5. Audio polish: FM/PSG balance (`FM_GAIN_*`), proper low-pass/resampling of the 223 kHz stream, optional AGES-style reverb? Compare FM against a real YM2413 reference (the `PaJson/banjo`, FigBug links below).
6. Housekeeping: delete `_template_sonic/` and the duplicate `z80-recomp-core/` clone; README/.gitignore per game dir; decide how to commit/PR the engine changes (SEE "Engine changes" below) - **do not commit without the user's go-ahead**.

### Morning play-test checklist (after the 2026-11-13/14 night)
1. `Games/ps-reva/play.ps1`: the window opens 16:9 with the side panel (your `ps_qol.ini` has `side_panel = 1`; the old separate map window was switched off: `automap = 0`). HUD (F1) -> SIDE PANEL OFF/ON resizes the window.
2. Panel: party HP/MP/LV update live (also in battles), Meseta, map box: surroundings map on a planet / in a town, dungeon map in a dungeon.
3. HUD MAP page outside dungeons: 3 x 3 screens; Up/Down -> whole planet (party marker blinks).
4. F11 / Alt+Enter fullscreen (does Controller Companion behave in fullscreen?).
5. Tell me: layout/colours, what to show besides party/meseta/map, portrait images (format/size), town names wanted?

### Weekend play-test checklist for the user (2026-11-13)
1. HUD with the pad: one D-pad press = one row; hold = repeat, getting faster; EXP x1 -> x100 by holding Right (~3 s). Keyboard the same.
2. HUD pages: Up from the first option to the tab row, Left/Right -> MAP; LB/RB or Q/E as shortcuts; in a dungeon F1/Back opens on the MAP page.
3. Auto-map per save: explore a dungeon, SAVE into a slot, quit, CONTINUE that slot -> MAP page shows the explored part ("LAST DUNGEON" outside).
4. Walk x4 on foot; vehicles at x4 setting run at x2 (by design).
5. EXP/Meseta x100: rewards grow, levels still one per battle.
6. Golvellius: overworld idle animation, vanishing player (fixed?), any new `dispatch_misses.log` in `Games/golvellius/` -> send it.
7. Flicker/size change with the pad: still there with Controller Companion off for these windows? (RB/RT = x4 turbo; Back = HUD).
9. MAP page: symbols / colours readable? MAP REVEAL + MAP SECRETS options; secret walls (purple, hatched).
8. The user's `Games/ps-reva/ps_qol.ini` still has `automap = 1` -> the side window stays on until switched off in the HUD ("MAP SIDE WINDOW").

**Made public 2026-11-24 (user).** Checked first: every object of every commit / tag (2,202): no ROM, ROM-derived
file, exe, captured picture, `generated/` or `Images/<platform>/` ever; the only binaries are the app art in
`Images/`, our Golvellius Plus `.ips` / `.bps` and the PSRP 2.6.1 zips; no tokens / keys. The user kept the
launcher art as it is (no Nintendo name on it; the style is the user's choice). Personal details shortened in
the notes (local paths, email line, the Discord message), `.claude/` no longer tracked (the page-reader agent is a
personal helper, kept locally). Repo description: "Static recompilation of 8-bit console games to native PC
builds, with optional quality-of-life features". Releases v0.1.0 / v0.1.1 are public with it.
