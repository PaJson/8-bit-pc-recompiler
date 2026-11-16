# Golvellius Plus - ROM research (step 2, 2026-11-16)

Addresses in the original ROM (No-Intro USA/Europe/Brazil, CRC32 A51376FE). Bank n = file offset
n * $4000; code in banks 0/1 at its address, bank-7 code at `$8000-$BFFF` with bank 7 in slot 2.
Found with `Tools/z80dis.c`, runner dumps (`--dump-frame`: `.vram/.cram/.ram`) and the recompiled
build's generated C. Facts only; the plan is in `README.md`.

## Memory, banks, interrupts

- Slots: banks 0/1 fixed (the game never writes `$FFFD/$FFFE`), slot 2 = `$FFFF`. Bank writes in the
  code: constants 2 / `$0F`, plus the asset loader below.
- **Asset loader `$0E0B`**: A = asset code; bank = code >> 4 (`& $0F`: only banks 0-15), pointer =
  word at `$8000 + (code & 15) * 2` of that bank -> HL. So our banks 16+ need their own far calls.
- **VBlank IRQ (`$0038` -> `$05F6`)** pushes all registers and the current `$FFFF` (reads the RAM
  mirror `$DFFF`), restores both at the end: code running from bank 16+ in slot 2 survives
  interrupts. Reset button (port `$DD` bit 4) -> `$0405`.
- **NMI (Pause) `$0066`**: only `$C005 = $FF`.
- Stack: `ld sp,$DFF0`; RAM `$DFFC-$DFFF` mirror the mapper registers.
- Battery RAM: `$FFFC` bit 3 maps it over slot 2 (`$8000-$BFFF`), so code that touches it must run
  from banks 0/1 or RAM; clearing the bit brings back the ROM bank of `$FFFF` (code in bank 16 can
  call a bank-1 copy routine and gets its own bank back afterwards). The game never sets the bit.
- **Free space**: bank 1 `$7E99-$7FEF` (343 bytes of `$FF`) is the only free ROM in banks 0-15.

## Free RAM

Recorded with test env `GV_RAM_USE=<file>` (gv_qol.c: OR of the whole RAM every frame): 3 x 30000
frames random input from the title, 3 x 30000 from a password start (with Pause presses), 3 x 30000
with damage received x10 (Game Overs), Despa's dungeon to the boss and out, 15000 frames of the
New Game side-view area. Never non-zero AND not named by any real (non data-as-code) instruction:

| Range | Bytes | Use |
|---|---|---|
| `$C0A1-$C0FF` | 95 | **our variables** (the SRAM hack uses `$C0A1/$C0A2/$C0B0-$C0CF` too) |
| `$CB18-$CBFF` | 232 | buffer |
| `$D178-$DAE9` | 2418 | buffer, only while the game is not running (dungeon tables nearby: other dungeons not tested) |
| `$C00D-$C046`, `$C080-$C09F`, `$C2FF-$C5FF`, `$C750-$C77A`, `$C799-$C7FF`, `$CADC-$CAFF`, `$D0D0-$D0FF` | | unused in the tests, but next to game data (objects, sound driver `$C600+`, room data): avoid |

`$C0A0` IS used (pause palette flag). `$C87A-$C8FF` is named by real code (`$1425`, `$15D0`, ...): not free.

## Title menu (bank 0)

- `$27CB`: title waits `$50` x (frame + pad); button -> `$27F8`, timeout -> demo (`$27DE`).
- `$27F8`: `$C047 = 1` (text tiles from the upper set: tile 256 + ASCII), clear name table
  `$3D00`, `$100` bytes (rows 20-23), draw `$28AC` "NEW@GAME" (col 12, row 20) and `$28B7`
  "CONTINUE" (12, 22). Selection `$C100` = 0 / 1 (1 when `$C817` != 0 = a game in RAM).
- Loop `$281D`: `$07C3` (frame), `$2899` (cursor: tile `$61` blinking with 0 at name table `$3D14`
  + `$80` * selection = col 10), `$0DBB` (pad), button 1/2 (`$30`) -> `$2844`, up/down toggles.
- `$2844`: 0 -> New Game `$12C8`; 1 -> draw `$28C2` "LAST@GAME@CONTINUE" (8, 20) and `$28D7`
  "PASSWORD@CONTINUE" (8, 22), loop `$2863` (cursor `$2894`, col 6), `$288A`: 0 -> `$131B`, 1 -> `$133F`.
  No way back from the second menu.
- **String format** (`$02FC`): column, row, ASCII bytes, 0. `@` = space. `$030F` draws a 0-ended
  list of string pointers. Characters: `$02CC` writes the byte as tile low byte, `$C047` as high byte.
- **Title font** (upper set, tile 256 + code): space, A-Z, `. ' ! , ?` (`$5B-$5F`), arrows `$60-$62`
  (`$61` = the menu cursor), kana. **No digits** on the title.

## Font asset `$C0` (letters + digits)

Unpacked with `ld a,$C0 ; call $0E0B ; ld de,$0800 ; call $038F` (password screen `$133F`, Game Over
`$2120`, SRAM hack): tiles `$40+` = `@ A-Z . ' ! , ?`, arrows, COMPILE logo, kana, **digits 0-9 =
`$DF-$E8`** (same codes as the cave texts). With this font loaded into VRAM `$0800` a text byte is
its own tile number in the low set.

## Helpers (bank 0)

| Addr | What |
|---|---|
| `$07C3` | wait one frame (clears `$C005`; no pause check) |
| `$07C0` | pause check (`$28EB`) + wait one frame |
| `$0DBB` | read pad: `$C806` held, `$C807` newly pressed (bits 0 up, 1 down, 2 left, 3 right, 4 button 1, 5 button 2) |
| `$01B9` | set VRAM write address HL |
| `$018D` | write one name-table word |
| `$01CF` | fill VRAM: HL address, BC count, A value |
| `$02FC` / `$030F` | draw a string / a list of strings (format above) |
| `$038F` | unpack a 4-plane RLE tile asset (HL from `$0E0B`) to VRAM DE |
| `$0D1A` / `$0D20` | screen off / on (used around full redraws) |
| `$1C89`, `$7774` | used before mode changes (fade / sound stop) |

## Pause / status screen

`$07C0` -> `$28EB`: if `$C005` and mode `$C802` is 6-9: 3 frames, mute (bank 2 `$8000`); mode 6
(overworld) draws the status screen (`$2911-$293F`: `$0D1A`, palette `$C060` -> `$C84D`, asset `$83`
palette, `$038F`, `$0F65`, `$2996($71)`, `$0D20`); **wait loop `$2942-$294A`** (`$07C3` clears
`$C005`, the next NMI sets it); then sound back (`$8006`), mode 6 redraws the overworld (`$2962-$2985`).
Modes 7-9 (side view, top-down caves, ...) only freeze. Status screen tiles: low set `$00-$73`, its
own word tiles (POWER, CRYSTAL, GOLD, ITEM, MAX) and digits `$36-$3F`; no general font.
Asset `$71` (bank 7 `$8049`) sets scroll regs 8 / 9 to 0, writes `$D0` at sprite row `$3F00`, copies the
sprite Y buffer RAM `$C980` (64 B) to VRAM `$1F00`, fills `$C980` with `$D0`, copies the overworld name
table `$3800` (`$380` words) to VRAM `$1800`, then draws the status screen. Asset `$72` (`$82C2`, on
unpause) puts scroll back (reg 8 = -`$C809`, reg 9 = `$C808`), `$1F00` -> `$C980`, `$1800` -> `$3800`;
then asset `$B0` reloads the low tiles `$0000-$13BF` (status tiles are `$0000-$07FF`). So during the
pause VRAM `$1800-$1F3F` must stay untouched (the whole font at `$0800` reaches `$1D1F`).
VDP registers: the game writes them through `$0048` (B = value, C = register), shadow at `$C04F + reg`
(reg 8 = `$C057`; the overworld scrolls by `$F8` or `$08`, the status screen 0).

## Game start / continue / progress RAM

- New Game `$12C8` clears `$C83D-$C847`, `$C849`, `$C84B`, `$C86E-$C878`, sets sword `$C848` = 1,
  `$C843` = 1, mode 5, screen `$B6`, terrain `$C817` = 4, power max `$C839` = 1, -> `$1377`.
- LAST GAME CONTINUE `$131B`: `$C817` = 0 -> New Game; `$13DE` (mode 6, screen `$B4` = L14); `$C845`
  bit 7 -> New Game; `$C86E` = 1 -> `$13E9` (mode 5, screen `$B6`); asset `$79` -> `$1377`.
- `$1377` (every start): `ld sp,$DFF0`, `$C86D = $C84C = 0`, hero object `$C100 = 1`, `$C80B = 2`,
  **hero x `$C102 = $88`, y `$C101 = $98`**, `$C80A = $FE`, `$C81B..` from `$13C9`, `$C81A = 6`,
  power `$C836` = max `$C839`, -> `$042F`.
- So a **load = copy the saved progress block into RAM + run LAST GAME CONTINUE**, with the start
  screen (`$13E3`) and start position (`$1391-$1398`) replaced; no password decoding needed (the
  game keeps its progress in exactly this RAM between a Game Over and LAST GAME CONTINUE).
- Password generator: bank 7 `$83D6` (collects `$C86F-$C878`, gold, `$C845-$C847`, `$C839`,
  `$C84A/B`, `$C849`, `$C83E`, `$C848`, `$C817` + salt) -> `$852B` -> ret `$8553`. Run by Winkle
  (text command `$FA`, `$90EF`, mode `$11`) and by Game Over (mode `$13`).

## Game Over (bank 0)

`$20FF` (mode `$13`): 60 frames, asset `$C0` (font), asset `$73` = bank 7 `$82FA` draws "GAME OVER"
and (unless `$CAD7` = 0) the password; **`$2149` waits for button 1/2**, then `$7774`, `$1C89`,
`jp $0429` (back to the title). The "save?" question goes into that wait.

## Cave texts (bank 7)

Interpreter `$8EA0` (A = text code -> table `$967C`), **character fetch `$8EC8` `ld a,(hl) ; cp $F0`**
(HL = text pointer; `>= $F0` = command), output `$8ED8` (byte = tile number, low set). A TEXT FIXES
check at `$8EC8` (13 changed letters of the 4 bank-7 typos) needs no bank copies.

## Overworld music, damage, gold (same sites as the PC options)

`$19FE` song table read, `$33EE` damage dealt, `$37D8` damage received, `$35EA` gold drop,
`$3F45 / $43AC / $4678` sword swings, bank 7 `$914C` Dina, bank 1 `$4646` top-down cave speed.

## Not checked yet

Credits printer (bank 15, for "DESGINED"), room-record copy `$1918` (for MAP FIXES: patch `$CAD0` /
`$CAD6` after the copy, or the reads), where in Winkle's text box a "GAME SAVED" line fits, the hero
position while in a cave room (for resuming at Winkle's screen), play-time counter site (IRQ `$062F`).
