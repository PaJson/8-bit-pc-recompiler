# Golvellius Plus (our own ROM patch, work in progress)

A patch for **Golvellius - Valley of Doom (USA, Europe, Brazil)** (No-Intro, 256 KB,
CRC32 `A51376FE`) that works on real hardware (a cartridge board with battery RAM), in other
emulators and in our recompiled PC build (`Games/golvellius-plus`). Everything new is optional and
starts at the original game's behaviour.

This folder holds only our own source (`golvellius_plus.asm`). No ROM data is stored here; the
patch is always built from your own ROM.

## Build

```
git submodule update --init External/wla-dx          # once: the WLA-DX assembler (the user's fork)
python -I Tools/gv_build_patch.py [<ROM>]            # ROM found in ROMs/ by CRC32 when not given
```

Needs the MSYS2 tool chain (`C:\msys64\mingw64`: gcc, cmake, ninja); WLA-DX is built on first use
into `External/build/wla-dx`. Output (git-ignored):

- `Patches/Golvellius/build/Golvellius Plus.ips` and `.bps` (the BPS refuses a wrong source ROM)
- `ROMs/Patched/Golvellius - Valley of Doom (USA, Europe, Brazil) (En) [Golvellius Plus].sms`
  (used by `Games/golvellius-plus`; rebuild that game after a new patch: `Games/golvellius-plus/build.ps1`)

Apply the patch to the **original, unpatched** ROM (one patch, no order to follow). Check any ROM's
header checksum with `python -I Tools/sms_checksum.py <rom>`.

## Status

| Step | What | State |
|---|---|---|
| 1 | ROM grows to 512 KB (banks 16-31 new, `$FF`), header size code `$1` = 512 KB, checksum over the whole ROM, ID block at bank 16 `$8000` ("GOLVELLIUS PLUS", format 1) | done |
| 2 | Research: menus / window and text routines, Pause loop, free RAM, bank selects, save state | next |
| 3 | Title menus: START / OPTIONS; START -> original NEW GAME / CONTINUE; CONTINUE -> PASSWORD / SAVE | |
| 4 | Save slots (AUTO + 5) in battery RAM, SAVE screen (load / delete), AUTO at Winkle | |
| 5 | Save from Pause (status screen + button 1); Game Over asks to save after the password | |
| 6 | OPTIONS (stored in battery RAM, RESET TO DEFAULTS): text / map / sword hit fixes, cave down speed 2 / 3 / 4, multipliers | |

## Notes

- The header checksum is the export BIOS rule (SMS Power "ROM header"): 16-bit sum of `$0000-$7FEF`
  plus `$8000` up to the size the size code names. The original ROM says 128 KB (`$4F`), so its
  BIOS check covers only half of it; ours says 512 KB (`$41`) and covers everything. All other
  header bytes stay the original ones.
- Memory map: banks 0 and 1 are fixed in slots 0/1, slot 2 (`$8000-$BFFF`) is switched by the
  game and also shows the battery RAM when `$FFFC` bit 3 is set. New code lives in banks 16+
  (run in slot 2); code that reads or writes the battery RAM must run from banks 0/1. The only
  free space there is bank 1 `$7E99-$7FEF` (343 bytes, kept for such stubs).
