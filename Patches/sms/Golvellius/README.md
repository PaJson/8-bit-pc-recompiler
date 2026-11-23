# Golvellius Plus (our own ROM patch, work in progress)

A patch for **Golvellius - Valley of Doom (USA, Europe, Brazil)** (No-Intro, 256 KB,
CRC32 `A51376FE`) that works on real hardware (a cartridge board with battery RAM), in other
emulators and in our recompiled PC build (`Games/sms/golvellius-plus`). Everything new is optional and
starts at the original game's behaviour.

This folder holds only our own source (`golvellius_plus.asm`). No ROM data is stored here; the
patch is always built from your own ROM.

## Build

```
git submodule update --init External/wla-dx          # once: the WLA-DX assembler (the user's fork)
python -I Tools/sms/gv_build_patch.py [<ROM>]            # ROM found in ROMs/ by CRC32 when not given
```

Needs the MSYS2 tool chain (`C:\msys64\mingw64`: gcc, cmake, ninja); WLA-DX is built on first use
into `External/build/wla-dx`. Output (git-ignored):

- `Patches/Golvellius/build/Golvellius Plus.ips` and `.bps` (the BPS refuses a wrong source ROM)
- `ROMs/sms/Patched/Golvellius - Valley of Doom (USA, Europe, Brazil) (En) [Golvellius Plus].sms`
  (used by `Games/sms/golvellius-plus`; rebuild that game after a new patch: `Games/sms/golvellius-plus/build.ps1`)
- `ROMs/sms/Patches/Golvellius - Valley of Doom/Golvellius Plus/`: the same `.ips` / `.bps` +
  `Golvellius Plus.txt` (what the patch does, how to apply it), ready for other emulators / flash carts

Apply the patch to the **original, unpatched** ROM (one patch, no order to follow). Check any ROM's
header checksum with `python -I Tools/sms/sms_checksum.py <rom>`.

## Status

| Step | What | State |
|---|---|---|
| 1 | ROM grows to 512 KB (banks 16-31 new, `$FF`), header size code `$1` = 512 KB, checksum over the whole ROM, ID block at bank 16 `$8000` ("GOLVELLIUS PLUS", format 1) | done |
| 2 | Research: menus / window and text routines, Pause loop, free RAM, bank selects, save state (`RESEARCH.md`) | done (a few details left, listed there) |
| 3 | Title menus: START / OPTIONS; START -> original NEW GAME / CONTINUE; CONTINUE -> PASSWORD / SAVE; OPTIONS / SAVE = full-screen pages (placeholders); in our menus button 1 = select, button 2 = back | done |
| 4 | Save slots (AUTO + 5) in battery RAM, SAVE screen (load / delete, slot info: screen, play time, gold, power, crystals), AUTO at Winkle (+ "GAME SAVED TO AUTO."), loads start where saved, play time | done |
| 5 | Save from Pause (status screen + button 1: SAVE GAME, slots 1-5, OVERWRITE? NO / YES, the status screen and the overworld come back unchanged); Game Over asks SAVE THIS GAME? (NO first) under the password, YES = the same menu (the save continues at L14 like LAST GAME CONTINUE) | done |
| 6 | OPTIONS (stored in battery RAM, RESET TO DEFAULTS): text / map / sword hit fixes, cave down speed 2 / 3 / 4, multipliers | stage 1 done: bank-1 stubs compacted (117 bytes free for the option hooks); stage 2 done: OPTIONS page, stored in battery RAM ($8010), loaded at power-on; stage 3 done: every option works (+ SWORD REACH) |
| 7 | PC build (`Games/sms/golvellius-plus`): the HUD shows the ROM's own options (one setting with title -> OPTIONS), the ROM's save slots replace the PC password slots, the world / dungeon maps follow the ROM's slots | done |
| 8 | Follow-ups from the user's play (2026-11-17): Winkle's "GAME SAVED TO AUTO." garbled after a Pause save (font offset left over) fixed; with a save the title's cursors start on CONTINUE / SAVE and the SAVE page on the newest save (save number per slot); the status screen says "1  SAVE GAME" | done |
| 9 | OPTIONS: OVERWORLD SPEED 1X / 1.5X / 2X (10 options; the battery RAM block grows by one byte, older blocks still load, an out-of-range value resets only its option); '*' after the original game's values, RESET TO DEFAULTS removed (user's choice) | done |
| 10 | The map: Pause + button 2 = a 13 x 14 grid of the screens seen (area colours, dot = cave entered, your screen blinks), button 1 = the screen's page (area, the cave's person / item / price / BOUGHT, advice text; only for caves entered), kept per save slot (battery RAM `$8300 + 64 n`) | done |
| 11 | OPTIONS on two pages (PAGE row; page 2 = the multipliers), EASY CAVE ENTRY (enter a cave when the hero is within 8 px of the entrance point on both axes (2026-11-17: values OFF / HALF = in line on one axis / QUARTER = any; a 4-px step is pointless: the hero turns only on 8-px lines) and the step brought him closer; snapped onto the point, then the game's own entry), help / prompt lines centred with our own ':', play time h:mm, multipliers 0X..10X | done |
| 12 | Map page: a half-size picture of the screen (rows 1-12; one 8 x 8 tile per metatile, every other pixel; made at run time from the ROM's room / metatile / tile data, the area's tiles loaded into VRAM `$2000-$37FF` when another area's are there and put back afterwards; hook `$1A52` = the loaded area); text below in the sprite palette | done |

## Notes

- The header checksum is the export BIOS rule (SMS Power "ROM header"): 16-bit sum of `$0000-$7FEF`
  plus `$8000` up to the size the size code names. The original ROM says 128 KB (`$4F`), so its
  BIOS check covers only half of it; ours says 512 KB (`$41`) and covers everything. All other
  header bytes stay the original ones.
- Memory map: banks 0 and 1 are fixed in slots 0/1, slot 2 (`$8000-$BFFF`) is switched by the
  game and also shows the battery RAM when `$FFFC` bit 3 is set. New code lives in banks 16+
  (run in slot 2); code that reads or writes the battery RAM must run from banks 0/1. The only
  free space there is bank 1 `$7E99-$7FEF` (343 bytes, kept for such stubs).
