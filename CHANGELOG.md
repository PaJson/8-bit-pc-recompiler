# Changelog

One section per release, newest first. The section `## v<version>` becomes the text of the GitHub
Release for the tag `v<version>` (`.github/workflows/release.yml`); a release branch is checked for it
(`.github/workflows/check.yml`).

## v0.4.0

Golvellius Plus: change the options while you play.

- Golvellius Plus: Pause on the overworld, press button 1 (SAVE GAME), and move down past slot 5 to the new
  OPTIONS entry. Button 1 opens the same OPTIONS pages as the title screen, so every option (fixes, sword
  reach, easy cave entry, speeds, keep gold, multipliers) can be changed during the game. Button 2 saves
  the options and returns to the SAVE GAME page. The Game Over save page is unchanged.
- Golvellius Plus patch: the SAVE GAME page on the Pause screen sits two rows higher to make room for the
  entry. The launcher makes the new patched ROM the first time you play it; saves from the earlier patch
  still work.

## v0.3.0

A save-slot preview in Golvellius Plus, and a fix for its SAVE GAME page after a Game Over.

- Golvellius Plus: when loading (SAVE on the title screen's CONTINUE) or saving (Pause + button 1, or
  YES after a Game Over), the side panel shows the highlighted save slot: play time, crystals, where you
  saved (the area's name and the screens around it), gold, maximum gold, power, and the part of the
  world you had explored in that save. When loading, the gold shown is what you will have after loading
  (half, unless KEEP GOLD is on). Empty slots say so.
- Golvellius Plus patch: after a Game Over, the SAVE GAME page was shifted up by the overworld's
  scrolling (its heading off the top of the screen, part of the overworld still at the bottom). It is
  now drawn in place, and the "SAVE THIS GAME?" question always shows at the bottom of the screen. The
  launcher makes the new patched ROM the first time you play it; saves from the earlier patch still work.

## v0.2.0

Two new features: Build all in the launcher, and a save-slot preview in Miracle Warriors.

- Launcher: A (pad X) builds every game that is not built yet, one after the other (patched games get
  their patched ROM first). Esc (pad B) stops after the current game, a second press cancels that one
  too. If a build fails, the others still run, and at the end the first failed game is selected (see
  its `build_log.txt`).
- Launcher: Esc (pad B) during a build now cancels it and keeps the launcher open (before, Esc closed
  the launcher). Cancelling or closing the launcher during a build now also ends the C compiler, which
  before kept running in the background.
- Launcher: the key help shows the keys of what you used last, keyboard or pad (A is Build all on the
  keyboard but Play on a pad).
- Miracle Warriors: when loading (CONTINUE on the title screen) or saving (QUIT, SAVE in the game), the
  side panel shows the highlighted save slot: guilders, fangs, character points, the party, and where
  you saved on that save's own map, plus the world overview. Empty slots say so.

## v0.1.4

A clearer README on GitHub; the zip is the same as v0.1.3 apart from the version number.

- `README.md` (GitHub page): the "Patched games" section explains the included patches, which original
  ROM each needs and how the launcher applies them; maintainer-only text moved to the notes.

## v0.1.3

American spelling in the texts of the zip; the launcher and the games work as in v0.1.2.

- `README.txt`, `CREDITS.md` and `Engine\Compiler\SOURCES.txt`: "license" instead of "licence".

## v0.1.2

The project is now public on GitHub. The launcher and the games work as in v0.1.1.

- `README.txt`: describes the project as a recompiler for 8-bit console games (so far Sega Master
  System games).

## v0.1.1

Corrected credits; the launcher and the games work as in v0.1.0.

- `CREDITS.md`: the license of the recompiler engine (PolyForm Noncommercial 1.0.0 with additional
  permissions: donations and advertising are allowed, payment must never buy access), the patch files
  that come with the zip (Golvellius Plus, the Phantasy Star Retranslation 2.6.1 zips), the bundled C
  compiler (MSYS2 MinGW-w64 packages) and SDL2 with their licenses, and the original smssum repository.

## v0.1.0

First release (private test release).

**How it works:** unzip, put your own ROMs into `ROMs\sms\` (see `ROMs\README.txt`) and start
`Launcher.exe`. The launcher recognises each ROM by its checksum, shows its title screen, and builds the
game on your PC the first time you play it (1-2 minutes, Golvellius about 5). Everything needed is in
the zip, including the C compiler. No games or ROMs are included.

**Games**
- Phantasy Star: Rev A (English), the first USA / Europe version, Japan, Japan Rev 1, Brazil, Korea,
  Sega Ages (English and Japanese), and the Phantasy Star Retranslation 2.6.1 by SMS Power! (Catalan,
  German, English, English literal, Spanish, French, French extended, Latin, Brazilian Portuguese).
  The retranslation patches are included, unchanged; they need the first Japanese ROM.
- Golvellius - Valley of Doom, and Golvellius Plus (our own ROM patch, included).
- Miracle Warriors - Seal of the Dark Lord.
- Sonic the Hedgehog, Sonic the Hedgehog 2 (both versions) and Sonic Chaos, used to test the engine.

**Extras** (all off or at the original game's setting unless you change them in the F1 overlay)
- Phantasy Star: maps of the overworlds, towns and dungeons (filled in as you explore), side panel,
  walk speed, EXP / meseta / encounter options, FM or PSG sound, typo fixes.
- Golvellius: overworld and cave maps, side panel, options. Golvellius Plus adds its own options page.
- Miracle Warriors: map and panel.
- All games: F1 (pad Start) options overlay, Tab fast forward, F11 fullscreen, Esc asks before quitting.
