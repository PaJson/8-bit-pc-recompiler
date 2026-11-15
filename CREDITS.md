# Credits

This project recompiles Sega Master System games into native PC programs and adds optional
quality-of-life features on top. It stands on other people's work. This file names everyone whose
patches, research, code or ideas we use, and says what we use them for.

**No ROMs and no patch files are part of this project.** You supply your own ROMs and, for the options
below that need one, the author's own patch file. Get the patches from their official pages and put
them in `ROMs/Patches/` (any subfolder; keeping the authors' file names is recommended, but the game
finds a patch by its content, so a renamed file works too). `ROMs/` is never committed.

## Game patches and hacks

| Work | Author(s) | Official page | How we use it |
|---|---|---|---|
| Golvellius SMS Bugfix v1.1 (missing room C5, wrong cave graphic H5) | SMS Power! | https://www.smspower.org/Hacks/Golvellius-SMS-Bugfix | Golvellius option **MAP FIXES**: the same two room-table bytes, written by our own code. No file needed. |
| Golvellius Improvement Hack v1.0 / v1.1 | Alianger | (link to be added) | **CAVE WALL TWEAKS**: applies the author's own IPS file (`Golvellius v1.1 top down cave wall tweaks.ips`) at run time. **SWORD HIT FIX**: our own code, same fix as his "sword hit detection fix". Planned: NPC text and typo fixes and free crystals (his `Golvellius free crystals+NPC text+typos.ips`, applied at run time), Dina's 2600-gold trade (the amount is his; our own code). |
| Golvellius SRAM save hack | Ichigobankai | https://www.romhacking.net/hacks/3631/ | The idea behind **REMEMBER PASSWORD** (our own code; the password system stays as it is). |
| Golvellius SRAM hack, 6-enemies addendum | LemFliggity | https://www.romhacking.net/hacks/4933/ | Reference only. |
| Golvellius overworld map | SMS Power! Sega notebook | http://www.smspower.org/seganotebook/golvlius/golvmap.html | Screen naming (A-M, 1-14) for the MAP tab. |
| Phantasy Star Retranslation (2.5 and later) | Maxim, Paul Jensen, Z80 Gaiden and the SMS Power! team | https://www.smspower.org/Translations/PhantasyStar-SMS-EN | The `ps-ja-en25` builds recompile the retranslated ROM you make yourself with the official patch (`Tools/regen_patched.sh`). Its notes on bugs in the original game are a reference for future fixes. |
| Phantasy Star SMS Power retranslation 2.0 / 2.01 with new sprites | SMS Power! | https://www.smspower.org/ | The `ps-ja-smspower` build (not maintained). |
| Phantasy Star disassembly (`ps1disasm`) | its authors (see `Disasm/`) | - | Names of RAM variables, routines and tables used throughout our Phantasy Star code. |
| Phantasy Star town maps | pscave.com | https://www.pscave.com/ps1/towns/ | Only to name the towns on our runtime-rendered maps (nothing copied). |

The Phantasy Star SEGA AGES release (SEGA) inspired the side panel, the FM / PSG choice and several
options; everything is our own implementation.

## Code in the engine (`Recomp/smsggrecomp`, its own repository)

| Code | Author / licence | Used for |
|---|---|---|
| smsggrecomp (upstream) | mstan | The recompiler and runner this project is built on. |
| superzazu/z80 | Nicolas Allemand (superzazu), MIT | Z80 interpreter (fallback path and `--interp` reference). |
| emu2413 | Mitsutaka Okazaki, MIT (`runner/audio/LICENSE.emu2413`) | YM2413 FM sound. |
| stb_image | Sean Barrett, public domain / MIT | Loading the cover and logo images. |

## Tools used during development (not part of the project)

FLIPS (Alcaro) to apply BPS / IPS patches, Emulicious and MasterlatorSDL as debuggers.
