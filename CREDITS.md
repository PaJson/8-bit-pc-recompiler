# Credits

This project recompiles Sega Master System games into native PC programs and adds optional
quality-of-life features on top. It stands on other people's work. This file names everyone whose
patches, research, code or ideas we use, and says what we use them for.

**No ROMs and no patch files are part of this project.** You supply your own ROMs. No game option
needs a patch file (every option is our own code). Only the translation builds are made from a patched
ROM: download the patch from its official page and make the ROM with `Tools/apply_patch.py`, also
straight from the author's zip (`"<archive.zip>::<patch file>"`). `ROMs/` is never committed.
`Tools/gv_make_ips.py` writes our own Golvellius fixes (map fixes + 5 typos) as an IPS for other
emulators / real hardware, made from your own ROM.

## Game patches and hacks

| Work | Author(s) | Official page | How we use it |
|---|---|---|---|
| Golvellius SMS Bugfix v1.1 (missing room C5, wrong cave graphic H5) | Eric R. Quinn (released on SMS Power!, 2014-05-11) | https://www.smspower.org/Hacks/Golvellius-SMS-Bugfix | Golvellius option **MAP FIXES**: the same two room-table bytes, written by our own code. No file needed. |
| Golvellius Improvement Hack v1.0 / v1.1 | Alianger | https://www.romhacking.net/hacks/10309/ | **SWORD HIT FIX**: our own code, same fix as his "sword hit detection fix". **TEXT FIXES**: his text patch pointed us to the typos; the 5 fixes (and 2 more "GOLD" lines he did not change) are our own byte changes. **DINA'S PAYOUT** 2600 gold: the amount is his (his `Dina 2600 gold` patch), the code is our own. **CAVE DOWN SPEED**: his idea ("faster downward speed in top-down view caves"), our own code. |
| Golvellius SRAM save hack | Ichigobankai | https://www.romhacking.net/hacks/3631/ (first posted at https://www.smspower.org/forums/16779-GolvelliusSramSaveHack) | The idea behind **PASSWORD SAVE SLOTS** (our own code; the password system stays as it is). For playing elsewhere we recommend his hack (with LemFliggity's addendum); `Tools/gv_make_ips.py --base` builds our text fixes on top of it. |
| Golvellius SRAM hack, 6-enemies addendum | LemFliggity | https://www.romhacking.net/hacks/4933/ | = Ichigobankai's hack with the original 6 enemies per screen instead of 4 (and the SMS Power map fixes). The recommended save hack for other emulators. |
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
