# Credits

This project recompiles Sega Master System games into native PC programs and adds optional
quality-of-life features on top. It stands on other people's work. This file names everyone whose
patches, research, code or ideas we use, and says what we use them for.

**No ROMs and no patch files are part of this project.** You supply your own ROMs and, for the options
below that need one, the author's own patch file. Download the patches from their official pages and
put the downloads, as they are, anywhere in `ROMs/Patches/` (a zip does not need to be unpacked: the
game finds a patch by its content, loose or inside the author's zip, whatever the folder or file is
called). `ROMs/` is never committed. `Tools/apply_patch.py` makes the patched ROMs for the translation
builds, also straight from the zip (`"<archive.zip>::<patch file>"`).

## Game patches and hacks

| Work | Author(s) | Official page | How we use it |
|---|---|---|---|
| Golvellius SMS Bugfix v1.1 (missing room C5, wrong cave graphic H5) | Eric R. Quinn (released on SMS Power!, 2014-05-11) | https://www.smspower.org/Hacks/Golvellius-SMS-Bugfix | Golvellius option **MAP FIXES**: the same two room-table bytes, written by our own code. No file needed. |
| Golvellius Improvement Hack v1.0 / v1.1 | Alianger | https://www.romhacking.net/hacks/10309/ | **CAVE WALL TWEAKS**: applies the author's own IPS file (`Golvellius v1.1 top down cave wall tweaks.ips`) at run time. **FREE CRYSTALS** (a cheat, off by default), **TEXT FIXES** and **NEW HINTS**: the crystal prices / seller text, his typo and fact fixes and his rewritten hints, taken at run time from his `Golvellius free crystals+NPC text+typos.ips` (FREE CRYSTALS also from `Golvellius free crystals.ips`) and laid out by our own code so each can be switched on alone. **SWORD HIT FIX**: our own code, same fix as his "sword hit detection fix". **DINA'S PAYOUT** 2600 gold: the amount is his (his `Dina 2600 gold` patch), the code is our own. |
| Golvellius SRAM save hack | Ichigobankai | https://www.romhacking.net/hacks/3631/ (first posted at https://www.smspower.org/forums/16779-GolvelliusSramSaveHack) | The idea behind **REMEMBER PASSWORD** (our own code; the password system stays as it is). |
| Golvellius SRAM hack, 6-enemies addendum | LemFliggity | https://www.romhacking.net/hacks/4933/ | Reference only (= Ichigobankai's hack with the original 6 enemies per screen instead of 4). |
| Golvellius overworld map | SMS Power! Sega notebook | http://www.smspower.org/seganotebook/golvlius/golvmap.html | Screen naming (A-M, 1-14) for the MAP tab. |
| Phantasy Star Retranslation (PSRP), 2.6.1 | Maxim, Paul Jensen, Z80 Gaiden and the SMS Power! team, with the translators of each language (named in each release's readme) | https://github.com/maxim-zhao/psrp , https://www.smspower.org/Translations/PhantasyStar-SMS-EN (also `-FR`, `-DE`, `-ES`, `-CA`, `-PT-BR`) | The retranslation builds recompile the retranslated ROM you make yourself from the official release zip with `Tools/apply_patch.py`. Its notes on bugs in the original game are a reference for future fixes. |
| Phantasy Star disassembly (`ps1disasm`) | its authors (see `Disasm/`) | - | Names of RAM variables, routines and tables used throughout our Phantasy Star code. |
| Phantasy Star town maps | pscave.com | https://www.pscave.com/ps1/towns/ | Only to name the towns on our runtime-rendered maps (nothing copied). |

The Phantasy Star SEGA AGES release (SEGA) inspired the side panel, the FM / PSG choice and several
options; everything is our own implementation.

## Code in the engine (`Recomp/smsggrecomp`, its own repository)

| Code | Author / licence | Used for |
|---|---|---|
| smsggrecomp (upstream) | Matthew Stanley (mstan); licence "not yet declared" in its README | The recompiler and runner this project is built on. |
| z80-recomp-core (engine submodule) | Matthew Stanley; PolyForm Noncommercial 1.0.0 + `PERMISSIONS.md` (donations and advertising allowed; everything must stay free to get and play) | Shared Z80 state / instruction semantics used by the generated code. |
| superzazu/z80 | Nicolas Allemand (superzazu), MIT | Z80 interpreter (fallback path and `--interp` reference). |
| emu2413 | Mitsutaka Okazaki, MIT (`runner/audio/LICENSE.emu2413`) | YM2413 FM sound. |
| stb_image | Sean Barrett, public domain / MIT | Loading the cover and logo images. |

## Tools used during development (not part of the project)

Emulicious and MasterlatorSDL as debuggers; FLIPS (Alcaro) to apply patches until our own
`Tools/apply_patch.py` replaced it.
