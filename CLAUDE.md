# CLAUDE.md — 8-bit PC Recompiler

Native PC builds of 8-bit Sega games (Master System; Game Gear later) by static recompilation of the
Z80 ROM to C (engine `Recomp/smsggrecomp`, fork of mstan/smsggrecomp), plus optional QoL features in
C/SDL2. Main games: Phantasy Star (base = Rev A; 16 more builds incl. PSRP 2.6.1), Golvellius (+ our
ROM patch Golvellius Plus), Miracle Warriors; Sonic 1/2/Chaos only as engine regression tests.
The engine has its own `Recomp/smsggrecomp/CLAUDE.md` + `PRINCIPLES.md`: read them before touching
the recompiler or runner.

## Rules for every task
- **Never commit or push without the user's explicit go-ahead.** Engine first: commit + push in
  `Recomp/smsggrecomp` (branch `sms-pc-recompiler`), then `git add Recomp/smsggrecomp` + commit here.
- Nothing ROM-derived or SEGA artwork in git or in a release: no ROMs, `generated/`, game exes,
  captured `(title)` pictures, `Images/<platform>/`. Only `Images/*.png|ico` (the app's own art).
- Every game option defaults to the original game; features are our own C hooks (`[insn_hooks]`,
  frame hooks), not third-party patch files at run time.
- Never edit generated C: fix the recompiler, regenerate.
- After engine changes: `sh tests/run_selftests.sh` (engine root); regenerate all games into a scratch
  folder and diff (`sh Tools/sms/regen_all.sh <dir>`); lockstep the affected games.
- Claim "verified" only with a test (lockstep / soak / screenshot); the user verifies by playing.
- Discuss big features with the user before building them.

## Toolchain
MSYS2 `C:\msys64` (mingw64 gcc, cmake, ninja, SDL2), not on the normal PATH: bash
`export PATH=/c/msys64/mingw64/bin:$PATH`, PowerShell `$env:PATH="C:\msys64\mingw64\bin;$env:PATH"`.
Python scripts: `python -I`.

## Layout
| Path | What |
|---|---|
| `Games/<platform>/<slug>/` | one folder per ROM (made by `Tools/mkgame.py`): game.toml, CMakeLists, build/check/play.ps1 |
| `ROMs/<platform>/` (+ `Patches/`, `Patched/`) | the user's ROMs and patch downloads (git-ignored) |
| `Common/<platform>/<Game>/` | our per-game hook code (PS, Golvellius, MW) |
| `Patches/<platform>/<Game>/` | our own ROM patches (Golvellius Plus; built files in `dist/`) |
| `Recomp/smsggrecomp/` | engine submodule (recompiler + runner); `External/wla-dx` assembler submodule |
| `Launcher/` | the launcher (`launcher.c`, `release_build.c`) |
| `Tools/` shared, `Tools/<platform>/` game-specific | scripts; `Tools/test_input/rand{1,2,3}.txt` soak inputs |
| `ThirdParty/` | files passed on unchanged (PSRP 2.6.1 zips) |
| `Images/` | app art at top level (committed); `Images/<platform>/` = user's art (never commit) |
| `Release/` | output of `Tools/make_release.py` (git-ignored) |
| `Docs/notes/` | the detailed notes (below) |

Paths from a game folder: ROM `../../../ROMs/sms/<file>`, engine `../../../Recomp/smsggrecomp`.

## Everyday commands
```
python -I Tools/mkgame.py <slug> "<ROM file>" "<title>" [--platform sms] [--template ...] [--hooks ps|none]
Games/sms/<slug>/build.ps1        # recompile + build (cmake --build alone does NOT regenerate)
Games/sms/<slug>/play.ps1         # or Launcher.cmd
sh Tools/lockstep.sh <slug> <outdir> <frames> Tools/test_input/rand1.txt   # recomp vs --interp
sh Tools/sms/ps_suite.sh lockstep|qol <outdir> [slug] [ini]                # 56 PS scenarios
sh Tools/verify_game.sh <slug>
python -I Tools/make_release.py [--zip]                                     # release zip (no ROMs needed)
Tools/sandbox_test.ps1 [-Zip <zip>]                                        # test a release zip in Windows Sandbox
```

## Testing essentials (details: Docs/notes/testing.md)
- Lockstep (recomp vs `--interp`, `SMS_FIXED_R=1`) with "final SAME" is the correctness metric;
  isolated one-checkpoint blips = interrupt timing. AGES En: lockstep with `PS_SOUND_CHIP=fm`.
- `SMS_NO_SAVE=1` for reproducible runs; run the exe directly (check.ps1 aborts on stderr).
- Any `[stack] call at ...` line or dispatch miss (`dispatch_misses.log`) is a bug to analyse
  statically (no hand-added entries from the log alone). Check "stopped after N frames" = requested.

## Gotchas
- Bash tool: heredocs turn `\\` into `\` (and mangle `\n` in strings): write scripts with the Write tool.
- MSYS mangles `[ ]` in paths: pass ROM paths as `cygpath -m` from bash.
- One ninja per build dir at a time. Golvellius builds at -O1 (21 MB generated file).
- `dispatch_manifest.txt` seeds the recompiler: delete stale/junk seeds when static analysis is fixed.
- `[jump_tables] bank`: -1 for fixed-slot targets, the slot-2 bank otherwise.

## Notes by area (read the matching file before working there)
- `Docs/notes/phantasy-star.md`: every PS build, ROM facts, all PS options and their hooks, PSRP.
- `Docs/notes/golvellius.md`: Golvellius ROM facts, options, maps, caves, panel.
- `Docs/notes/golvellius-plus.md`: our Golvellius ROM patch (steps, options, RAM / bank 1 use), save-slot preview.
- `Docs/notes/miracle-warriors.md`: MW ROM facts, options, map, Plus plan.
- `Docs/notes/engine.md`: our recompiler / runner changes, idioms, reviews, Sonic regression.
- `Docs/notes/testing.md`: lockstep, scenarios, coverage, diagnostic build, test env vars.
- `Docs/notes/launcher-and-release.md`: launcher, art / titles, release zip + GitHub workflow.
- `Docs/notes/history.md`: renames, moves, repo / license / going-public decisions, old plans.
- `Docs/notes/references.md`: links (tools, disassemblies, audio references).

## Current state (2026-11-25; update or delete when done)
- Latest release **v0.2.0** (tag on `6789164`; PRs #6 + #7): launcher Build all (A / pad X), Esc / pad B
  cancel a build (the whole process tree, job object), key help per device; Miracle Warriors save-slot
  preview in the panel. Not tested yet: a patched game in the Build all queue.
  v0.1.0-v0.1.4 = PRs #1-#5 (first release, corrected credits,
  public repo + README.txt wording, "license" spelling, clearer README). The repo `PaJson/8-bit-pc-recompiler`
  is PUBLIC since 2026-11-24 (history checked first: `Docs/notes/history.md`). Every release: the flow in
  `Docs/notes/launcher-and-release.md` "Release flow" (release PR -> Check -> user merges -> tag on the
  user's word). Small repo-only fixes go straight to `main` (on the user's go-ahead).
- Verified by the user (2026-11-24): the release zip in Windows Sandbox (launcher, Golvellius Plus, PS dark
  dungeon refused + nothing on the map); by play: PS cave map, launcher text fitting, build progress bar,
  launcher message cleared on moving, cover changes only after releasing a held direction. Nothing open.
- Moved 2026-11-25: project is now `D:\Users\PaJson\Development\VSC\Projects\8-bit-PC-Recompiler` (was
  `VS2026\Projects`; the user works in VS Code). All 28 build folders repaired with `Tools/rename_project.ps1
  -RepairOnly`, Launcher rebuilt OK; each game recompiles fully on its next build. Claude Code data copied
  to the new path's folder.
- Decided: all profiles (Sonic too) stay in the release; American spelling in project texts.
- Next (user's plan): new profiles Golden Axe Warrior, Ultima IV, Wonder Boy, Ys (their ROMs are in
  `ROMs/sms/`; discuss each before building). Later: MSYS2 MINGW64 -> UCRT64 (setup-msys2 deprecation
  warning; dev PC, workflows, bundled compiler).
