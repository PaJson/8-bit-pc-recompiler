# Launcher and release notes

*Moved here word for word from CLAUDE.md on 2026-11-24 (the dates in the text are when each note was written; later notes can correct earlier ones). CLAUDE.md keeps the rules for every task and lists all notes files.*

## App art, icon, captured title screens (2026-11-24)
- **`Images/` top level = the app's own art, committed** (user's pictures, made 2026-11-23/24, same set in the 16-bit project): `Background.png`
  (launcher background, 2730x1536, contains the logo; kept PNG by the user's choice: JPG q92 was 6x smaller but rings at hard pixel edges),
  `Logo.png`, `Icon.png` ("8 BIT"), `IconLarge.png` ("8-BIT PC RECOMP"), `Icon.ico`. The user saved them with a yellow `#FFFF00` colour key
  (tRNS); converted to real alpha 2026-11-24 with the edge pixels un-blended from the old WHITE background (else a 1-px light halo on dark
  backgrounds); yellow-key originals only in that session's scratchpad. `.gitignore`: `Images/*/` (platform folders = the user's SEGA covers
  + captured titles, never committed) and `!Images/*.png` (the general `*.png` rule is for test dumps).
- **`python -I Tools/make_icon.py [<Images dir>]`** (stdlib only, shared with the 16-bit project) -> `Images/Icon.ico`: 16-96 px from
  `Icon.png`, 128 + 256 from `IconLarge.png` (square crop around the visible pixels + 2 %, premultiplied area average, BMP entries + 256 PNG).
  Re-run after changing the pictures. Embedded by the launcher (`Launcher/CMakeLists.txt`) and every game exe (engine `cmake/SmsGame.cmake`:
  `SMSGG_ICON` or `Images/Icon.ico` in the game folder or up to 4 parents, RC via `check_language(RC)`; no per-game CMakeLists edits).
  SDL2 uses the exe's first icon for the window / taskbar.
- **Title screens from the player's ROM** (no SEGA art shipped): runner `--title-shot <png> [--title-frames N] [--title-frame F]`
  (engine `runner/main.c`): headless, forces `SMS_NO_SAVE` / `SMS_NO_ART`, picks the longest stretch of a near-still (< 10 % pixels
  changed vs its first frame) non-blank (>= 5 % lit, >= 4 colours) picture in the first 3600 frames, and within it the frame with the
  most pixels outside the main background colour ("most lit" failed on Sonic Chaos' cyan title: lettering missing). ~2 s per game. Verified
  on all 24 builds (all titles correct incl. 7 PSRP languages, Korea). The launcher (`title_capture_tick`) runs it in the background for
  every built game without the user's `(front)` cover and without `Images/<platform>/<display name> (title).png` (exact name, no prefix
  match), one at a time, never during a game / build; failures not retried per session; game.toml `title_frame = N` overrides (carried into
  Release by `make_release.py`). Shown in the cover box pixel-sharp (whole-pixel enlarge + area average). Delete a file to recapture.
  Test env: `LAUNCHER_CAPTURE_ONLY=1` (all missing titles, no window, prints a table; 24 games = 51 s), `LAUNCHER_NO_TITLES=1`.
- Launcher background: `launcher.ini background=` > `Images/Background.*` > another picture in `Images/` (not Logo / Icon / game art) >
  generated one. Over any background but the app's own, the PRESS START screen shows `Logo.png` (generated bg: under its ring, replacing
  its drawn title text). Transparent borders of every loaded picture are trimmed. `make_release.py`: top-level `Images/` always copied,
  the user's own covers / logos in `Images/<platform>/` only with `--with-images`; captured `(title)` files NEVER (a game's artwork,
  user decision 2026-11-24: the launcher makes them from the ROMs wherever it runs; checked in Release with a renamed ROM).
  Reminder given to the user: the Release game exes are ROM-derived too (translated program code), so Release is for this PC;
  sharing = the project itself (recompiler + build on the recipient's PC from their own ROMs).
- Not done (discussed): the PS side panel's pre-game screen could show the `(title)` picture (panel is 256 px wide) instead of the text.

## Quit prompt, PS RUN AWAY (2026-11-24)
- **Esc in a game window asks "QUIT THE GAME?"** (user: accidental presses closed the game). Engine `host_sdl.c` (`g_quit_ask`,
  `host_quit_asking()`), `hud.c hud_draw_quit_prompt` (solid box, fits the GG picture), `main.c` pauses the emulation like the HUD.
  Enter / Space / Z / pad A = quit; Esc / X / Backspace / F1 / pad B / Start / Back = keep playing (a double Esc never quits). The
  window's X button / Alt+F4 still close at once (user's choice). Not in the GPU `--internal-scale` path (no pause loop there; Enter
  still quits). Engine test env `SMS_TEST_KEYS="<frame>:<SDL key name>,..."` injects key presses (verified: Esc+Esc = plays on,
  Esc+Return = quits at that frame, arrows ignored).
- **Launcher focus (user report: the game window came up without the focus, intermittently; a terminal had it):** the game exes
  are CONSOLE programs, so starting one plainly opened a console window that took the focus. Now the launcher starts games with
  `CREATE_NO_WINDOW` (their output -> `Games/<platform>/<slug>/run.log`, git-ignored by `*.log`), calls
  `AllowSetForegroundWindow(game pid)`, stays visible until the game's window exists (EnumWindows by pid, console window classes
  skipped), `SetForegroundWindow`s it and only then hides (15 s timeout). Back: the launcher passes `SMS_FRONTEND_PID`; the runner
  (`main.c`, after `glue_run`, window still focused) calls `AllowSetForegroundWindow(that pid)`; the launcher wakes on the
  process handle and `SetForegroundWindow`s itself. Verified with `scratchpad`-style test: launcher brought to front, then
  `LAUNCHER_AUTO=play:<slug>` + new `LAUNCHER_AUTO_MS=<ms>` (delayed auto action) + `SMS_TEST_KEYS="400:Escape,400:Return"`
  (the game quits itself), polling `GetForegroundWindow`: 3 / 3 runs game focused, launcher focused again (a 0.1-0.2 s gap while
  the game process shuts down). The launcher logs `[launcher] started / game window found; foreground = game: yes / game ended;
  foreground = launcher: yes` on stderr.
## Release zip: games built on the player's PC (2026-11-24, user request; NOT committed / pushed yet)
- **Goal (user):** a GitHub action makes `8-bit PC Recompiler v<version>.zip`; the player unzips, puts ROMs into `ROMs\sms\`, starts
  `Launcher.exe`; titles + games are made on the player's PC from the player's ROMs. **No game exes, no generated C, no title
  screens, nothing ROM-derived in the zip.** User decisions: ship **whole MSYS2 compiler packages** (not a trimmed copy: "no
  shortcuts"; 553 MB unpacked, zip 164 MB), version **semver 0.1.0** in `app_info.toml` (name, version, repository; read by
  make_release.py, the workflow (tag must be `v<version>`), Launcher CMake: `APP_NAME` / `APP_VERSION` + a Windows VERSIONINFO;
  the launcher shows `v0.1.0` bottom right on PRESS START and uses APP_NAME as window title).
- **`python -I Tools/make_release.py [<out>] [--zip] [--msys C:/msys64]`** (rewritten; the old "Release with built exes for this
  PC" mode is gone; the old flat files in `Release/` are the user's, left alone): builds the recompiler, the launcher and
  **`Tools/release_kit/`** (CMake project -> `Release/_kit_build`: `libsmsgg_runner.a` = engine `sms_add_runner_library`,
  `libhooks_<Family>.a` per `Common/sms/<Family>/`, `SmsInterp.exe`, `app_icon.o`, `kit_flags.txt`), runs
  **`Tools/make_toolchain.py`** (every file of the pacman dependency closure of `mingw-w64-x86_64-gcc` + `-SDL2`, 21 packages,
  copied to `Engine/Compiler/mingw64/` (MSYS2 gcc finds headers via sysroot `<bin>/../../mingw64`: the folder MUST be named
  mingw64), `SOURCES.txt` = versions + where the sources are (GPL); self test builds + runs an SDL2 probe with PATH = Windows only;
  read-only flags dropped), and writes `Release/<name> v<version>/`: `Launcher.exe`, `SDL2.dll`, README.txt, LICENSE.txt,
  CREDITS.md, `Images/*.png|ico` (app art only), `ROMs/README.txt` (one folder per console: sms, gg, later nes, gb),
  `ROMs/sms/Patches/{Golvellius Plus, Phantasy Star/Retranslation 2.6.1 (PSRP, SMS Power)}` + `THIRD-PARTY.md`,
  `Games/sms/<slug>/game.toml` (the dev file, `rom` = file name only, + `[release]`: `crc32` (from a `crc32` key or the header
  comment "CRC32 XXXXXXXX"; checked against a local ROM if there), `exe` (CMakeLists `project()` + .exe), `hooks` (the one
  `Common/sms/<Family>/` in CMakeLists), `optimization` (build.ps1 `-Optimization O1` for both Golvellius), `rom_name`, patch
  recipe from the BPS footers of the SHIPPED patch files) + the family's `.txt` data files (ps_item_names.txt),
  `Engine/SmsRecomp.exe`, `Engine/Compiler/`, `Engine/sms/{build.txt (recompiler, cflags = -DNDEBUG + the runner's definitions,
  libs), include/ (core + runner headers, flat), lib/, SmsInterp.exe}`, `Engine/LICENSES/` (smsggrecomp + core PolyForm NC +
  PERMISSIONS, core LICENSES/, emu2413, superzazu, stb_image), `release_info.txt` (version, commit; only a folder with it is ever
  deleted by the script). Ends with **`smoke_test()`**: a 32 KB test ROM of our own (`smoke_rom()`: di/im1/ei + `jr $`, valid
  header) as `Games/sms/_smoketest`, built by `Launcher.exe --build` with PATH = Windows only, run 120 frames + SmsInterp 120
  frames, removed again. Whole run ~1 min here.
- **Launcher in a release** (`Launcher/release_build.c`): no `build.ps1` in the game folder + `Engine\<platform>\build.txt` there
  -> `Launcher.exe --build "<game dir>" "<rom>"` (hidden child, output to `build_log.txt`): [1/4] `SmsRecomp <rom> --game
  game.toml`, [2/4] gcc -O<opt> on the three generated files in parallel -> `build\obj`, [3/4] link with `-lhooks_<Family>
  -lsmsgg_runner` + libs + app_icon.o -> `build\<exe>` + SDL2.dll, [4/4]; compiler bin first in PATH; each step also written to
  `<game>\build_step.txt` (the log's last line is often a gcc command). `find_exe`: `exe` key = `build\<exe>`. Titles of games
  not built yet: `Engine\<platform>\SmsInterp.exe --title-shot` (patched games once Play made their ROM). "No games" text: put
  ROMs into the ROMs folder (see ROMs\README.txt). **Build progress (user wish):** status line = the step + elapsed m:ss, a bar
  under it (release: step 1 0-5 %, step 2 5-95 % estimated from `<prefix>_full.c` size at ~12 s/MB (measured: PS 4.5 MB 55 s
  O2, Golvellius 21 MB ~240 s O1), stops at 97 % of the step; dev builds: ninja [n/m]) with a light sweeping over it.
  Test env `LAUNCHER_SHOT_MS=<ms>[,<ms>]` + `LAUNCHER_SHOT_OUT=<file with %d>` = window pictures while running.
- **Engine (`Recomp/smsggrecomp`, uncommitted):** `cmake/SmsGame.cmake` options at file scope, `sms_runner_sources()` /
  `sms_runner_settings()` shared by `sms_add_game` (compile commands byte-identical, only the icon object moved in the link
  order), `sms_add_runner_library()`, `sms_add_interp()`; `runner/interp_only.c` (call_by_address stub) + `main.c` (no game
  layout -> `--interp` forced, generic window title). Self tests: all 11 pass.
- **Committed copies for GitHub (no ROMs there):** `Patches/sms/Golvellius/dist/Golvellius Plus.{ips,bps,txt}` (written by
  `gv_build_patch.py`, which also keeps `# ROM CRC32 XXXXXXXX` in `Games/sms/golvellius-plus/game.toml`; rebuild gave the same
  9D195ED1) and `ThirdParty/sms/Patches/Phantasy Star/Retranslation 2.6.1 (PSRP, SMS Power)/*.zip` (byte-identical, SHA-256 +
  terms in `ThirdParty/README.md`: PSRP README "Disclaimer" allows passing them on unchanged, free of charge).
- **`.github/workflows/release.yml`:** windows-latest, checkout with submodules (all public), `msys2/setup-msys2` (gcc, cmake,
  ninja, SDL2), Python 3.12, tag vs app_info version check, `make_release.py out --zip --msys <msys2-location>`; tag push ->
  `gh release create` with the zip, manual run -> workflow artifact. **Not run yet** (needs the commits pushed; the project repo
  is private, so its Releases are visible to the owner only until it goes public).
- **Verified (2026-11-24):** release folder + zip from scratch; titles captured by SmsInterp with PATH = Windows only; launcher
  builds (ps-reva 59 s, MW 52 s) with PATH = Windows only; from the unzipped zip: Play of PSRP EN = patched ROM from the shipped
  zip, built, 1800 frames 0 misses; release-built ps-reva vs the dev exe, `rand1` 20000 frames, `SMS_FIXED_R`: state identical at
  all 20 checkpoints; dev launcher unchanged (35 entries). Not done: a run on a PC without MSYS2 (Windows Sandbox), the workflow.


## QoL / feature ToDo (collected 2026-11-13; discuss each big one with the user before building)  *(part; the rest of this section is in another notes file)*

- **Launcher v1 (built 2026-11-14 night):** `Launcher/launcher.c` (SDL2 + GDI+ for JPG/PNG, the engine HUD font `runner/hud.c`), `Launcher/CMakeLists.txt`, `Launcher/build.ps1`, root `Launcher.cmd` (double-click: builds the launcher on first use, then starts `Launcher/build/Launcher.exe`). Finds the project root from the exe path (dir with Games/ and ROMs/). Lists every `Games/<slug>/game.toml` whose `rom` file exists (name = `[game] display_name` or the ROM file name; green marker = built (`build/*.exe`), yellow = not built) plus `.sms` files in `ROMs/` (one subfolder level) that no game folder uses (grey, "no build profile"); sorted by name. Play = like play.ps1 (cwd = game folder, `SMS_SAVE_DIR = <game>/saves`, `--window <window_scale>`); the launcher hides while the game runs. Not built / R / pad Y = `powershell build.ps1` hidden, output to `Games/<slug>/build_log.txt`, last log line shown, then plays. Background: `launcher.ini` `background =` or the first image in `Images/` (the user's `Images/Backround.jpg`, git-ignored), cropped to fill ("cover"); else a generated generic one (sky, pixel skyline, horizon glow, grid floor, gold/cyan ring with circuit traces around a pixel game pad, title "PC RECOMPILER / 8-BIT CONSOLE CLASSICS"; no SEGA names or logos). Panel 40 % of the window, right (or `panel = left`). Keys: Up/Down/PgUp/PgDn/Home/End, Enter/Space play, R rebuild, F11/Alt+Enter fullscreen, Esc quit; pad D-pad/stick with repeat, A/B/Start play, Y rebuild; mouse wheel, click, double click. `launcher.ini` (root, git-ignored; sample `Launcher/launcher.ini.sample`). Test env: `LAUNCHER_SHOT=<ppm>` (+`LAUNCHER_SEL=n`), `LAUNCHER_NO_IMAGE=1`, `LAUNCHER_AUTO=play:<slug>|build:<slug>`, `LAUNCHER_EXIT_AFTER=1`, `LAUNCHER_GAME_EXTRA="--hidden --frames 300"`. Open: unknown ROMs could get a profile via mkgame (template choice), sort / group by series.
  **PRESS START screen + names (2026-11-15, user request):** the launcher starts on the background with a blinking "PRESS START" (yellow, dark outline, centre; `press_start_y = <percent>` in launcher.ini moves it, `splash = 0` skips it); Enter / click / pad Start shows the list (UI fades in 250 ms, cover fades in); Esc quits, F11 works there. In the list (user feedback 2026-11-15: "any button moved on", no way back): only Enter / pad A / double click play, R / pad Y rebuild, Esc / pad B / pad Back go back to the PRESS START screen (Esc still quits while a build runs, its hint says so); hint line "ENTER/A PLAY   R/Y REBUILD   ESC/B BACK". Paging (user wish 2026-11-15 night): Left / Right, D-pad left / right, LB / RB and the stick's x axis = one page (the visible rows) up / down, held = repeat. Build status line = the stage ("recompiling the ROM to C", "compiling (n/m)", "linking") instead of the build log's last line (that is console output wrapped at 120 columns by the hidden PowerShell: it showed "Building: ists them)"). Test env `LAUNCHER_SPLASH=1` with `LAUNCHER_SHOT`. `Launcher.cmd` now runs a quick `cmake --build Launcher\build` before starting (new sources picked up; a failed build, e.g. while the launcher runs, just starts the old exe). **Window titles** come from `[game] display_name` (recompiler -> `g_game_spec.display_name` -> "<name> - recompiled"); without it the recompiler used the output prefix ("golvellius"). Golvellius, Miracle Warriors and the Sonic folders now have the ROM file name as display_name; `mkgame.py` adds one when the template has none (sms_generic).
  **ROMs by CRC32, patching in the launcher, Release folder (2026-11-18, user request; user away at work):** (1) the launcher indexes every `.sms` / `.gg` in `ROMs/` (3 subfolder levels, never `ROMs/Patches`) with its CRC32 and matches game folders by CRC32, so ROM file names no longer matter. A game folder's CRC32 = `crc32 = "XXXXXXXX"` in its game.toml (Release) or the CRC32 of the file its `rom = "..."` names (developer tree: unchanged behaviour, still 30 entries). (2) **Patched ROMs:** game.toml keys `patch_base_crc32`, `patch_crc32`, `patch_name`, `patch_from`, `rom_name`: ROM missing + base ROM present -> listed cyan "Play makes the patched ROM first" (patch found in `ROMs/Patches`, any name, loose or inside a .zip) or orange "Needs the patch file in ROMs\Patches: <zip name>"; Play = apply (own C IPS + BPS, zip reader + stb_image's zlib `stbi_zlib_decode_noheader_buffer` from art.c), result checked against `crc32`, written to `ROMs/Patched/<rom_name>` (.tmp + MoveFileEx), rescan, play. **Patch identity gotcha: every BPS file has the same whole-file CRC32 `2144DF1C`** (a BPS ends with the CRC32 of everything before it; data + its own CRC32 = the CRC residue), so a BPS is identified by the CRC32 in its last 4 bytes (IPS: CRC32 of the file); zip entries that are .bps are unpacked to read it. (3) **`python -I Tools/make_release.py [<out>] [--with-images]`** -> `Release/` (git-ignored): `Launcher.exe` + `SDL2.dll` + `launcher.ini.sample` + `README.txt` + `CREDITS.md` at the top, `Games/<slug>/` = the built exe + `SDL2.dll` + a minimal game.toml (display_name, crc32, exe, rom_name; patched games: the recipe, found from the BPS footers (source / target CRC32) in `ROMs/Patches`, no hand-made list), `ROMs/` + `ROMs/Patches/` (our Golvellius Plus .bps / .ips / .txt, plus the 7 PSRP 2.6.1 zips byte-identical: their README's Disclaimer says "Any enclosed files are offered at no charge and must be distributed together in original condition" + no commercial sale; `REDISTRIBUTABLE` in the script lists such folders; nothing else third-party) + `ROMs/Patched/`, `Images/` (empty; `--with-images` copies the user's own). Never copied: ROMs, saves, ini settings. Kept on re-runs (saves, ROMs, settings); exes replaced when changed. All 26 built games, 153 MB; all 10 patch recipes found. The game exes are derived from the ROMs (translated program): Release is for this PC, not for sharing. In Release the launcher has no build.ps1: R says "Rebuilding needs the developer project folder". Tested in Release with renamed ROMs (Rev A as `my_ps.sms`, Japan in a subfolder, a random file = grey "unknown"): list shot; EN / EN literal / Golvellius Plus patched by the launcher (CRCs 2CD659D4 / C5D7BEB0 / 9D195ED1) and played 600 frames (0 misses); the IPS path via a temporary entry (same ROM as the BPS); the other languages listed as "needs the patch" until their zip is there; a game exe given the wrong ROM refuses it. Test files removed again (Release/ROMs holds only the README and our patch).
  **Game art (2026-11-15, user's images in `Images/`, git-ignored, SEGA artwork: never commit/ship):** files `<title> (front).jpg` (cover, ~1520 x 2100), `<title> (full).jpg` (wraparound, unused), `<title> (logo).png` (600 px wide transparent title logo, black/red: invisible on navy -> a white outline is generated, user's choice "keep the colours"). Titles: Golvellius, Miracle Warriors, Phantasy Star, Sonic the Hedgehog. Matching (engine `runner/art.c`, shared by launcher and runner): case-insensitive prefix of the display name or ROM file name followed by end / " (" / " [" / " -" / ":", longest title wins (so every PS variant gets the PS art, Sonic 2 / Chaos get none); `art.<slug> = <title>` in launcher.ini overrides. Launcher: window title "SMS PC Recompiler" (since 2026-11-22; was "SEGA Master System PC Re-compiler"; the generated fallback background stays neutral); cover centred beside the panel (86 % of the area width, 84 % of the height, soft shadow), background dimmed behind it, 180 ms cross-fade; logo box (40 * sc) above the details (reserved only when any game has a logo); textures scaled once in software (area average, alpha-weighted) per size, LRU 24, one picture prepared per idle frame (`art_warm`); background search skips `(front|full|logo|back)` files. Runner: `art_setup_game_logo()` (main.c, after loading the ROM; `Images/` found by walking up from the ROM folder or `$SMS_IMAGES_DIR`; `SMS_NO_ART=1` disables; `SMS_ART_LOG=1` prints the file) -> `hud_set_logo()`: the HUD header shows the logo (18 frame px tall, header row +8 px) and the title's part after " - " ("OPTIONS"); PS side panel title screen shows it instead of the "PHANTASY STAR" text. Engine: stb_image v2.30 vendored (`runner/external/stb/`, public domain / MIT; PNG + JPEG only), `hud_image_alpha()` (hi-res layer with transparency: alpha-weighted shrink, blended; other layers unchanged), `hud_draw_logo()` (hi-res or software blend), layer texture cache 4 -> 12 (PS's 4 portraits + 2 maps already exceeded 4 = re-shrunk every frame).
- **Launcher** (user's goal, updated 2026-11-14; discuss the design before building): the user wants to start "the app" as a launcher window that lists the games found in `ROMs/` and launches the chosen one. Pieces we have: every build is tied to one ROM CRC32; `Tools/rom_inventory.py` / `mkgame.py` / `Games/<slug>/build.ps1` / `play.ps1`; per-game saves in `Games/<slug>/saves/`. Plan sketch: scan `ROMs/` (and `ROMs/Patched/`), CRC32 -> known slug (table from the `Games/*/game.toml` CRCs + the variant list above), show title + status (built / not built / unknown ROM), build on first use (mkgame + build.ps1, can take minutes: show progress; Golvellius builds at -O1), then run the game exe (separate process; the launcher waits or hides). One entry per ROM / build (user decision 2026-11-14: Rev A, USA, Brazil, AGES En, Japan, ... each listed separately, no grouping); FM / PSG stays a per-game option (see FM / PSG above: AGES En can redirect its sound shims to the PSG driver still in bank 12; the Japanese ROMs pick PSG themselves when our port $F2 reports no FM unit; Rev A has no FM data, so FM = the AGES build). Pad + keyboard navigation (same SDL pad code as the runner). One exe with all games would need per-game symbol prefixes in the generated C; separate exes per game are fine for a launcher.
  **Background artwork: `Logos/Logo 01.jpg`, `Logo 02.jpg` (2752 x 1536), `Logo 03.jpg` (1376 x 768)**, the user's own 16:9 images: "SEGA Master System PC Re-compiler" title with the console, Sonic in Green Hill and a blue tech skyline (dark navy, cyan, gold); 01 and 02 differ only in details. The launcher scales one to the window as background; the game list should sit where it does not cover the title (e.g. a translucent panel at a side or the lower third, HUD-like colours: navy `#101830`, cyan `#60D0FF`, yellow highlight `#FFE040`). Note: they show the SEGA logo and Sonic (SEGA trademarks/characters): fine for this local project, keep them out of anything distributed publicly.
- Distribution model (user decision 2026-11-13): the repo ships no ROM-derived files; users supply their own ROM, which the build recompiles and the runtime reads (graphics, maps, text, music data, interpreter fallback; CRC-checked). A single EXE with the ROM inside is NOT wanted.

## Launcher fixes (2026-11-24, user report; NOT committed yet)
- A message (error / "Build finished." ...) is cleared when the selection changes (any way: keys, pad,
  mouse), unless a build or game is running (main loop compares g_sel with the last one).
- Holding a direction flickered through every cover: the shown cover / logo now belongs to `g_art_sel`,
  which follows `g_sel` only when no navigation key (arrows, PgUp / PgDn) or pad direction (`nav_dir`) is
  held and the selection has rested 120 ms. Details text still follows the selection at once.
- Enter on a ROM without a profile: "Cannot play this one yet." (the old text ran past the panel; the line
  above already says "No build profile for this ROM yet.").
- Not verified by a test (needs held keys; `LAUNCHER_SHOT` sets g_art_sel = g_sel): user to try.

## Testing a release zip in Windows Sandbox (2026-11-24)
- `Tools/sandbox_test.ps1 [-Zip <file>] [-MemoryMB 8192]` (default: newest `Release/*.zip`) writes
  `Release/_sandbox/test.wsb` + `setup.ps1` and starts the sandbox: no network, the zip's folder and
  `ROMs/sms` mapped read-only (`C:\Host\Release`, `C:\Host\ROMs`); at logon the zip is unpacked with
  `tar.exe` to the sandbox Desktop (`setup.log` there) and both folders open in Explorer. Then play it as a
  new player: copy ROMs into the release's `ROMs\sms\`, start `Launcher.exe`.
- A workflow zip: `gh run download <run id> -D Release/_gh`, then `-Zip` that file.
- The sandbox has no Notepad (it is a Store app on Windows 11); a normal PC has it.
- First test (user, workflow run 45450461783): launcher, title shots, Golvellius Plus patched + built +
  played. PSRP not listed with only the Japanese Rev 1 ROM: correct, the patches are for CRC32 6605D36A
  (README now says so).

## Release flow (2026-11-24, user decision: release PR + check workflow)
- `.github/workflows/check.yml` ("Check"): every pull request to `main` (+ by hand). Release-branch checks
  (`release/v<version>` = `app_info.toml` version, `CHANGELOG.md` has `## v<version>`, tag `v<version>`
  free), engine self-tests (`run_selftests.sh`; the runner's own `C:\msys64` via `setup-msys2 release:
  false`, Mesa's software `opengl32.dll` copied next to the GPU tests), `make_release.py out --zip` (smoke
  test), the zip attached to the run for 14 days. No ROMs on GitHub: lockstep + playing stay local.
- `.github/workflows/release.yml`: on a tag `v<version>`; release notes = that `CHANGELOG.md` section
  (+ a fixed "unzip, put your ROMs ..." line); fails without the section. Same MSYS2 setup as Check.
- Steps for a release (Claude does 1-3 and 6 only when the user says so):
  1. `git switch -c release/v<version>`; set `version` in `app_info.toml`; add `## v<version>` to
     `CHANGELOG.md` (written for players). Engine changes: committed + pushed in the engine first.
  2. Locally: engine self-tests, lockstep / soaks of what changed, `python -I Tools/make_release.py --zip`.
  3. Commit, push the branch, `gh pr create --base main` (title "Release v<version>").
  4. Check passes -> `gh run download <run id> -D Release/_gh` -> `Tools/sandbox_test.ps1 -Zip <zip>`,
     play-test.
  5. The user merges the PR (squash or merge commit) on GitHub.
  6. `git switch main && git pull`, `git tag v<version> && git push origin v<version>` -> Release
     workflow -> GitHub Release with the zip (private repo: visible to its collaborators only).
- Other work can use PRs too (any branch -> Check runs); small fixes may still go straight to `main`.

## Launcher text fitting (2026-11-24, user report from the Sandbox test)
- `fit_text()`: long text is cut with "..": list names and the 2nd line of the selected name in the MIDDLE
  (both ends readable: "Golvellius - Valle..[Golvellius Plus]", "Phantasy Star (Ret..2.6.1 EN literal)"),
  status lines at the end. Before: list names were cut at the end (Golvellius = Golvellius Plus, PSRP EN =
  EN literal, FR = FR extended looked the same), status lines cut without a mark.
- State line "Not built yet: Play builds it first." (was "... (minutes).", cut at 40 chars); the game
  being built shows "This takes a few minutes." instead. Key help falls back to shorter wordings in a
  narrow panel ("ENTER PLAY R REBUILD ESC BACK").
- Test env `LAUNCHER_SIZE=<w>x<h>` (window size, for `LAUNCHER_SHOT` pictures). Checked by screenshots at
  960x540 / 1280x720 / 1920x1080 and during a real build (LAUNCHER_AUTO=build:sonic1, LAUNCHER_SHOT_MS).
- First Check runs (PR #1): run 1 `gpu_jobs` failed (Mesa's default D3D12 path on the Microsoft Basic Render
  Driver), run 2 hung > 12 min with `GALLIUM_DRIVER=llvmpipe` (cancelled). Now: engine `run_selftests.sh`
  `SMS_SELFTEST_SKIP_GPU=1` (engine `8f18b70`) skips only `gpu_jobs` on GitHub (it runs locally, release
  step 2); `reference_present` passes there on Mesa D3D12. Job time limit 45 min (self-tests 10, build 30).
  Actions updated to checkout / setup-python / upload-artifact v7 (Node 20 deprecation warning).
- Open (later release): setup-msys2 warns "MINGW64 is deprecated, migrate to UCRT64": a toolchain move
  (dev PC, workflows, the release's bundled compiler); left visible as a reminder.
- v0.1.0 published 2026-11-24 (tag on `701acca`). Two fixes after it, for the next release:
  `Tools/release_notes.py [<version>] [<out>]` makes the release text (the CHANGELOG section with wrapped
  lines joined: GitHub shows every line break as `<br>`, checked on the v0.1.0 page) + the "unzip, put
  your ROMs" line; release.yml publishes it, check.yml prints it on release branches. The zip is uploaded
  as `8-bit-PC-Recompiler-v<version>.zip` (GitHub turns spaces in asset names into dots) with the label
  `8-bit PC Recompiler v<version>.zip` (`gh release create ... "file#label"`).

## Build all (built 2026-11-24, user request; user's choices: only games not built yet, key A / pad X)
- `build_all()` in launcher.c: queues the slugs of every entry in state NOT BUILT or PATCH (patched ROM made
  first, as Play does), builds them one at a time through the normal `start_build` (developer build.ps1 or the
  release `--build`); READY, NO PROFILE and NEEDS PATCH are left alone. Slugs, not indexes: each build ends with
  `scan_games()`, which can reorder the list. A failure does not stop the queue; the end message: "Build all
  finished: N built." / "Build all stopped: N built." / "Built N, failed M (selected; see its build_log.txt)":
  the first failed game is selected, so its name shows in the details (a name in the message was cut at
  960x540). Nothing to do: "Nothing to build: every game here is built."
- The game being built shows "Build all: game i of n." on its state line. Key help is two lines (the
  narrowest panel, 960x540, has 29 characters); the details block moved up 12 * sc (one list row fewer).
- Fixes after the user's first try (2026-11-24, user's choices):
  - The help said "ENTER/A PLAY" (Enter / pad A) and "A/X BUILD ALL" (key A / pad X): A meant two things.
    Now the help shows only the keys of the device used last (`g_pad_ui`: a pad button / stick / a pad
    connected = pad, a key / mouse = keyboard): "ENTER PLAY   R REBUILD" / "A BUILD ALL   ESC BACK" or
    "A PLAY   Y REBUILD" / "X BUILD ALL   B BACK". The keys did not change.
  - Esc during a build closed the launcher, and the compiler kept running: `TerminateProcess` ended only
    powershell / the release build process, not cmake / ninja / gcc. Now every build runs in a job object
    (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, the process created suspended and assigned before it starts);
    cancelling (`TerminateJobObject`) or closing the launcher ends the whole tree. Esc / pad B during a
    build = cancel ("Build cancelled.", the launcher stays); during Build all the first press = stop after
    this game ("ESC STOP AFTER THIS GAME"), a second = cancel it too ("Build all cancelled: N built.", or
    "Cancelled. Built N, failed M ..."). Quitting = the window's close button only.
  - After a cancel (and on quit during a build) an exe in `build\` written since the build started is
    deleted: the linker may have been stopped while writing it (a rebuild writes it anew anyway).
  - Test env `LAUNCHER_KEYS=<ms>:<input>[,...]` (esc, enter, a, r, d, pada, padb, padx, padd, quit) pushes
    input through the normal event handling. Verified in the scratch tree (sonic1 built from scratch, the
    input at 25 s, cc1 / gcc / ninja / cmake / build.ps1 listed just before it): single Esc, Build all pad B
    twice, quit: no build process left afterwards, no sonic1 exe; screenshots of all help / message states
    at 960x540.
- The message of a finished build now stays when the build moved the selection to its game (`g_msg_sel`
  replaces the main loop's private last selection; before, browsing during a build and the build's jump back
  cleared "Build finished." at once).
- Test env `LAUNCHER_AUTO=buildall`. Verified in a scratch copy of the tree (junctions to ROMs / Recomp /
  Common, three game folders, one build.ps1 failing on purpose): real builds of Sonic 1 / Chaos / 2, the
  failure counted and selected, screenshots at 960x540 / 1280x720 / 1920x1080. Not tested: a PATCH game in
  the queue, the release `--build` path in a queue (and cancelling it).
