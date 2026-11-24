# Changelog

One section per release, newest first. The section `## v<version>` becomes the text of the GitHub
Release for the tag `v<version>` (`.github/workflows/release.yml`); a release branch is checked for it
(`.github/workflows/check.yml`).

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
