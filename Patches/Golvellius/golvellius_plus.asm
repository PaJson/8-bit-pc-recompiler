; Golvellius Plus - our own ROM patch for Golvellius - Valley of Doom (USA, Europe, Brazil)
; (No-Intro, 256 KB, CRC32 A51376FE). Built by Tools/gv_build_patch.py with WLA-DX
; (External/wla-dx): the user's ROM, padded to 512 KB with $FF, is the background image;
; this file only says what changes. No ROM content is stored here. ROM facts: RESEARCH.md.
;
; Step 1: the ROM grows to 512 KB (banks 16-31 = new space), the header says 512 KB (size code
;   $1) so the checksum covers the whole ROM, bank 16 starts with an ID block.
; Step 3: title menus. After the title's "press a button": START / OPTIONS (ours);
;   START -> the original NEW GAME / CONTINUE; CONTINUE -> PASSWORD / SAVE (ours);
;   PASSWORD -> the original LAST GAME CONTINUE / PASSWORD CONTINUE. OPTIONS and SAVE open
;   full-screen pages. Button 1 = select, button 2 = back, also in the two original menus, now
;   submenus (originally both buttons selected).
;   Nothing of the title is ever reloaded (the title's unpacking runs with the frame interrupt off,
;   which would freeze the music): the menus only replace the two text rows under the picture
;   (saved to RAM and written back when going back to the title), and a page is a second name
;   table at $3000 in tiles the title does not use (the font's kana), shown by switching VDP
;   register 2; its digits are unpacked from the game's font into unused tiles.
; Step 4: save slots in the cartridge's battery RAM: AUTO (written whenever Winkle makes the
;   password; he then also shows "GAME SAVED TO AUTO.") + SLOT 1-5 (written from the Pause screen
;   in step 5). SAVE page: the slots (screen, play time, gold, power, crystals), button 1 = LOAD /
;   DELETE (DELETE asks), the cursor starts on the newest save. A load restores the progress
;   the game itself keeps in RAM between a Game Over and LAST GAME CONTINUE, then runs the game's
;   LAST GAME CONTINUE with the start screen and position replaced (AUTO: in front of Winkle's
;   cave, where the game puts the hero when he leaves it). Play time: our own counter (the game
;   has none), one tick per frame the game waits ($07C3) while a game runs (mode 5-$12).
; Step 5: saving into SLOT 1-5: on the overworld's Pause (status) screen button 1 opens SAVE GAME
;   (the hero's screen and position; a used slot asks OVERWRITE?), and after Game Over's password the
;   game asks SAVE THIS GAME? (NO first; such a save continues at L14 like LAST GAME CONTINUE).
;   The status screen says "1 SAVE" and "2 MAP" under its frame (letters in tiles the unpause reloads).
;   Once a slot is used, the title's cursors start on CONTINUE and on SAVE.
; Step 6: OPTIONS page (title -> OPTIONS): TEXT FIXES, MAP FIXES, SWORD HIT FIX, CAVE DOWN SPEED,
;   KEEP GOLD, GOLD DROPS / DAMAGE DEALT / DAMAGE RECEIVED x0-x10, OVERWORLD SPEED 1X-4X (the hero's
;   1-pixel overworld step runs up to 2 times per frame, with every check of the game); '*' marks the
;   original game's values.
; The map: the overworld's Pause screen + button 2 = a 13 x 14 grid of the screens seen in this game
;   (area colours, a dot = its cave entered, your screen blinks), button 1 on a square = its page (area,
;   what its cave holds, the advice text; only for caves entered); kept with every save slot. Kept in battery
;   RAM (not shown as a slot), loaded into RAM at the first frame wait after power-on, so everything,
;   the intro demo included, uses them; saved when the page is left. Every default = the original game.
;   What each does (hooks below): TEXT FIXES = the 5 typos of the English ROM (PASSWARD, 3 x GOLD for
;   FIND, DESGINED in the credits), MAP FIXES = the SMS Power bug fix (screen C5's own room, screen H4's
;   cave graphic), SWORD HIT FIX = every swing can hit (an enemy's hit cooldown counts down only
;   while the sword touches it, so what a swing leaves of it used to swallow the next swing's hit:
;   now the first contact of each new swing starts with the cooldown at 0), SWORD REACH = the sword's hit area 4 / 8 pixels wider on every side, CAVE DOWN
;   SPEED = the hero walks down faster in top-down caves, KEEP GOLD = a continue does not halve the
;   gold, multipliers = gold dropped by enemies, sword damage, damage taken (healing unchanged).
;
; Memory: banks 0 and 1 are always in slots 0/1 ($0000-$7FFF; the game never remaps them),
; slot 2 ($8000-$BFFF) is switched by the game (register $FFFF) and also shows the battery RAM
; when $FFFC bit 3 is set. Our code lives in bank 16 (slot 2). Anything that changes slot 2
; (the game's asset loader, going back into the game's code, the battery RAM) goes through the
; stubs in bank 1 ($7E99-$7FEF, the ROM's only free space): code in slot 2 must never switch
; slot 2 itself. Every bank switch uses a constant (ld a,N / ld ($FFFF),a) and every jump has a
; fixed target, so the PC recompiler can follow all of it statically.
;
; Title VRAM (RESEARCH.md): low tiles $00-$F7 = the picture; high tiles 256+$01..$3D = picture +
; copyright lines, 256+$40..$62 = the font's letters / cursor (our menu texts), 256+$63..$BF unused
; (the font's kana); name table $3800. Pages: name table $3000-$35FF (256+$80..$AF), digits
; 256+$B0..$B9 ($3600-$373F).
;
; Battery RAM (8 KB is enough; a real board needs battery RAM, emulators and flash carts have it):
;   $8000 "GVPLUS", format 1, slot loaded last
;   $8010 the options (OptVals, 10 bytes: the index of each chosen value), $801A their check byte
;     (sum xor $5A); missing / damaged / out of range = the defaults; a block saved by level 4 (9
;     options, check byte $8019) is still read (OVERWORLD SPEED then 1X)
;   $8300 + 64 * n  slot n's map: +0 $A5, +1 the screens seen (23 bytes), +24 the caves entered (23),
;     +47 check byte (sum xor $5A); missing = an empty map (a slot saved before the map existed)
;   $8100 + 64 * n  slot n (0 = AUTO, 1-5): +0 $A5 = used, +1 screen, +2 hero y, +3 hero x,
;     +4 play time (frames, seconds, minutes BCD, hours BCD), +8 $C84C (screen tile-set flag),
;     +9 $C817, +10 $C839-$C84B (19), +29 $C86E-$C878 (11), +40 save number (word: the highest
;     = the newest save; 0 = saved before save numbers existed), +42..+62 0, +63 checksum

.MEMORYMAP
DEFAULTSLOT 2
SLOT 0 $0000 $4000
SLOT 1 $4000 $4000
SLOT 2 $8000 $4000
.ENDME

.ROMBANKSIZE $4000
.ROMBANKS 32
.EMPTYFILL $FF                    ; unused bytes stay $FF like the ROM's own padding

.BACKGROUND "background.sms"     ; written by gv_build_patch.py into the build folder
.UNBACKGROUND $40000 $7FFFF      ; banks 16-31: free for our sections
.UNBACKGROUND $07E99 $07FEF      ; bank 1: the original $FF padding before the header

; Header: the original values (product code 7017, version $F, export SMS), only the size code
; changes from $F (128 KB) to $1 (512 KB); WLA writes the checksum over the whole 512 KB.
.SMSHEADER
    PRODUCTCODE $17, $70, $F
    VERSION $F
    REGIONCODE 4
    RESERVEDSPACE $FF, $FF
    ROMSIZE 1
    CHECKSUMSIZE 512*1024
.ENDSMS

; Text: the fonts have the letters at their ASCII codes, '@' ($40) = space, '. ' ! , ?' at
; $5B-$5F, '/' at $6E; our digits sit at $B0-$B9 of the title font (unpacked by UnpackFont).
.ASCIITABLE
MAP " " = $40
MAP "0" TO "9" = $B0
MAP "." = $5B
MAP "'" = $5C
MAP "!" = $5D
MAP "," = $5E
MAP "?" = $5F
MAP "/" = $6E
MAP "*" = $BA                    ; our own tile (title pages only: StarTile)
MAP ":" = $BB                    ; our own tile (GlyphUp ColonBits): see ColonTile
.ENDA

; ---------------------------------------------------------------------------------------------
; RAM ($C0A1-$C0FF is never used by the game; $CB18-$CBFF neither; $D178-$DAE9 not while the
; title runs: RESEARCH.md). The boot code clears all RAM.
.DEFINE MenuSel     $C100        ; menu selection (the game's own title menus use it too; the cursor routine reads it)
.DEFINE TitleRows   $D200        ; 256 bytes: name table rows 20-23 of the title (the copyright lines)
.DEFINE PlayTime    $C0A1        ; 4: frames, seconds, minutes (BCD), hours (BCD, stops at 99)
.DEFINE LoadPending $C0A5        ; 1 = the next game start uses LoadScreen / LoadY / LoadX / LoadC84C
.DEFINE LoadY       $C0A6        ; (LoadY, LoadX in this order: read as one word)
.DEFINE LoadX       $C0A7
.DEFINE LoadScreen  $C0A8
.DEFINE LoadC84C    $C0A9
.DEFINE LastSlot    $C0AA        ; the slot loaded last (also kept in battery RAM)
.DEFINE SaveCur     $C0AB        ; SAVE page: the slot under the cursor
.DEFINE SubSel      $C0AC        ; SAVE page: LOAD / DELETE, NO / YES
.DEFINE NtAddr      $C0AD        ; 2: next name-table address for NtChar
.DEFINE NtAttr      $C0AF        ; high byte for NtChar (1 = title font in the high tile set)
.DEFINE Tmp         $C0B0        ; 16 bytes scratch (battery RAM header)
.DEFINE SlotBuf     $CB18        ; 64 bytes: one slot record
.DEFINE NtBaseHi    $C0C0        ; high byte of the name table NtAt writes to ($30 page, $38 the game's)
.DEFINE NtDigit0    $C0C1        ; tile of digit 0 ($B0 page font, $DF the game's low-set font)
.DEFINE SvScreen    $C0C2        ; SaveMenu: what a save records as start screen, y, x, $C84C
.DEFINE SvY         $C0C3
.DEFINE SvX         $C0C4
.DEFINE SvFlag      $C0C5
.DEFINE HelpText    $C0C6        ; 2: the text DrawHelp puts on row 22
.DEFINE NtColBias   $C0C8        ; added to every NtAt column (mod 32): 0 on our pages, the game's scroll on its screens
.DEFINE NtTileOfs   $C0C9        ; added to every NtChar tile: 0, or $C0 (= -$40) for the Pause screen's font at tile 0
.DEFINE UpLo        $C0CA        ; 2: UnpackFont keeps the plane bytes UpLo .. UpLo+UpCnt-1 (8 per tile) of the font
.DEFINE UpCnt       $C0CC        ; 2
.DEFINE UpDst       $C0CE        ; 2: ... and writes them to VRAM from UpDst
.DEFINE SavedBank   $C0D0        ; Enter16 / Leave16: the slot-2 bank of the game code a hook came from
.DEFINE OptVals     $C0D1        ; 10: the options (index of the chosen value; OptDefaults = the original game)
.DEFINE OptText     OptVals+0    ; TEXT FIXES       0 off, 1 on
.DEFINE OptMap      OptVals+1    ; MAP FIXES        0 off, 1 on
.DEFINE OptSword    OptVals+2    ; SWORD HIT FIX    0 off, 1 on
.DEFINE OptReach    OptVals+3    ; SWORD REACH      0 off, 1 = 4 pixels, 2 = 8 pixels wider
.DEFINE OptCave     OptVals+4    ; CAVE DOWN SPEED  0 = speed 2 (original), 1 = 3, 2 = 4
.DEFINE OptKeepGold OptVals+5    ; KEEP GOLD        0 off (a continue halves the gold), 1 on
.DEFINE OptGold     OptVals+6    ; GOLD DROPS       x0..x10 (1 = original)
.DEFINE OptDealt    OptVals+7    ; DAMAGE DEALT     x0..x10
.DEFINE OptRecv     OptVals+8    ; DAMAGE RECEIVED  x0..x10
.DEFINE OptWalk     OptVals+9    ; OVERWORLD SPEED  0 = 1X (original), 1 = 1.5X, 2 = 2X
.DEFINE OptEasyCave OptVals+10   ; EASY CAVE ENTRY  0 off (original), 1 HALF (half the hero over it), 2 QUARTER
.DEFINE OPT_N       11
.DEFINE OptLoaded   $C0F5        ; 1 = OptVals came from the battery RAM (or the defaults) since power-on
.DEFINE ReachBox    $C0DC        ; 8: SWORD REACH: the box last widened, per sword object ($C120, $C140)
.DEFINE SwingNo     $C0E4        ; SWORD HIT FIX: counts the swings (1..255)
.DEFINE SwingSeen   $C0E5        ; 13: per enemy object ($C160 + 32 n): the swing that touched it last
.DEFINE UpEnd       $C0F2        ; 2: UnpackFont: UpLo + UpCnt
.DEFINE HelpRow     $C0F4        ; the row of DrawHelp / SubDraw (22)
.DEFINE StepLeft    $C0F6        ; OVERWORLD SPEED: extra steps left in this frame
.DEFINE StepFlags   $C0F7        ; bit 0 = an extra step is running, bit 1 = 1.5X: this frame has one
.DEFINE StepFace    $C0FF        ; the hero's facing before the frame's first step (for the tail's turn test)
.DEFINE StarTile    $BA          ; OPTIONS page: our '*' (title tiles 256+$BA, VRAM $3740)
.DEFINE ColonTile   $BB          ; ':' (character $BB): title pages 256+$BB ($3760), Pause pages $7B ($0F60),
                                 ; the Game Over screen $BB ($1760, a kana of the game's font: not shown there)
; the map (Pause + button 2): in the free buffer after SlotBuf
.DEFINE MapVis      $CB58        ; 23: the overworld screens seen in this game (bit n & 7 of byte n / 8)
.DEFINE MapEnt      $CB6F        ; 23: the screens whose cave / dungeon the hero has been inside
.DEFINE MapBytes    46           ; MapVis + MapEnt (a slot's map block: $A5, these, check byte)
.DEFINE MapCur      $CB86        ; the screen under the cursor
.DEFINE MapTick     $CB87        ; frames on the page (blinking)
.DEFINE MapSlot     $CB88        ; SaveSlot: the slot being written
.DEFINE MapHold     $CB89        ; frames a direction is held (repeat)
.DEFINE MapS        $CB8A        ; scratch: a screen
.DEFINE MapS2       $CB8B
.DEFINE MapCid      $CB8C        ; CaveLines: the screen's cave id
.DEFINE MapCode     $CB8D        ; CaveLines: what the cave offers
.DEFINE MapLo       $CB8E        ; CaveLines: its price (BCD, x100)
.DEFINE MapHi       $CB8F
.DEFINE MapRow      $CB90        ; text row
.DEFINE MapCol      $CB91        ; text column count
.DEFINE MapTry      $CB92
.DEFINE MapK        $CB93
; the Pause hint ("1 SAVE", "2 MAP" placed to the pixel)
.DEFINE HintGlyphs  $CB94        ; 88: the glyphs of both texts (8 bytes per character)
.DEFINE CentreCol   $CBEC        ; NtCentre: the column the last centred text started at (SubDraw's cursors)
.DEFINE OptCur      $CBED        ; OPTIONS page: the cursor's position on the page (OPT_PAGEPOS = the PAGE row)
.DEFINE OptPage     $CBEE        ; OPTIONS page: 0 / 1
.DEFINE CaveLastX   $CBEF        ; EASY CAVE ENTRY: the hero's position at the previous step
.DEFINE CaveLastY   $CBF0
.DEFINE OPT_PAGEPOS 9            ; the PAGE row (screen row 20)
; the map page's picture (half size: one 8 x 8 tile per 16 x 16 metatile; HintGlyphs' space is free then)
.DEFINE PicList     $CB94        ; 64: the metatiles drawn so far (index = output tile - PicT0)
.DEFINE Meta        $CBD4        ; 4: a metatile's tiles TL TR BL BR (tile 256 + n)
.DEFINE PicA        $CBD8        ; 4: a source tile row, planes 0-3 (left tile)
.DEFINE PicB        $CBDC        ; 4: (right tile; must follow PicA)
.DEFINE PicT        $CBE0        ; the output tile being made
.DEFINE PicH        $CBE1        ; its half (0 = from TL / TR, 1 = from BL / BR)
.DEFINE PicR        $CBE2        ; its row in that half (0-3)
.DEFINE PicS        $CBE3        ; the screen drawn
.DEFINE GfxId       $CBF1        ; the area id whose tiles the game loaded last ($1A52 hook)
.DEFINE PicCode     $CBF2        ; the area palette asset whose tiles (+1) are in VRAM $2E00 now
.DEFINE PicFlag     $CBF3        ; the $C84C state the common tiles in VRAM $2000 have now
.DEFINE PicN        $CBF4        ; metatiles in PicList
.DEFINE PicLive     $CBF5        ; 1 = the hero's own screen (RAM $CA00 / $CC00 / $C84D)
.DEFINE PicArea     $CBF6        ; another screen: its area id ($1A3D's rule)
.DEFINE PicTFlag    $CBF7        ; ... its $C84C (layout id >= $53)
.DEFINE PicLayB     $CBF8        ; ... its layout's bank (3 / 4)
.DEFINE PicLayP     $CBF9        ; 2: ... and address
.DEFINE PicEnt      $CBFB        ; ... its entrance cell ($FF = none drawn: only caves you have been inside)
.DEFINE PicEntM     $CBFC        ; ... the entrance metatile
.DEFINE PicCell     $CBFD        ; the cell being drawn (0-179)
.DEFINE PicM        $CBFE        ; the metatile being looked up
.DEFINE AutoBlink   $CBFF        ; Winkle's room: frames since the AUTO line appeared (0 = no line; it blinks)
.DEFINE AUTO_COL    6            ; "GAME SAVED TO AUTO." (19 characters) centred on row 12
.DEFINE PicT0       MT0          ; output tiles $3A..$7A (the map squares' tiles: MapTiles makes them again)
.DEFINE PIC_ROW     1            ; the picture: rows 1-12, columns 8-22
.DEFINE PIC_COL     8
.DEFINE HINT_LINE   4            ; their top line inside row 22 (176 + 4 = line 180)
.DEFINE HintTile    $C0F8        ; the next tile to write
.DEFINE HintGP      $C0F9        ; 2: the glyphs of the text being drawn
.DEFINE HintSh      $C0FB        ; its pixel offset inside a column (x & 7)
.DEFINE HintCol     $C0FC        ; its first column
.DEFINE HintN       $C0FD        ; its number of characters
.DEFINE HintMask    $C0FE        ; $FF >> HintSh
.DEFINE MT0         $3A          ; map tiles (Pause screen, low set): per colour k = 0..8: L, L + cave / Winkle / dungeon mark, R (45 tiles, $3A-$66)
.DEFINE MT_CUR      MT0+45       ; the cursor: L, R

; the game's routines and RAM (bank 0)
.DEFINE WaitFrame   $07C3        ; wait one frame
.DEFINE ReadPad     $0DBB        ; -> $C806 held, $C807 newly pressed
.DEFINE PadNew      $C807        ; bit 0-3 up/down/left/right, 4 button 1, 5 button 2
.DEFINE FillVram    $01CF        ; HL address, BC count, A value
.DEFINE SetVramAddr $01B9        ; VRAM write address HL
.DEFINE VdpDelay    $0208        ; a short delay between VRAM accesses while the display is on
.DEFINE DrawString  $02FC        ; HL -> column, row, text, 0 (name table $3800, high tile byte = $C047)
.DEFINE TileHigh    $C047
.DEFINE MenuCursor  $2899        ; A = tile at column 10, row 20 + 2 * (MenuSel); needs carry clear
.DEFINE ScreenOff   $0D1A        ; (display off / on requests, each waits a frame)
.DEFINE ScreenOn    $0D20
.DEFINE VdpReg2     $C051        ; the game's copy of VDP register 2 (name table address), written by $0114 only
.DEFINE LoadAsset   $0E0B        ; A = asset code -> maps its bank in slot 2, HL = data
.DEFINE UnpackTiles $038F        ; HL data -> VRAM DE
.DEFINE SoundStop   $7774        ; all sound channels off
.DEFINE FadeOut     $1C89        ; palette to black (9 steps, frames keep running)
.DEFINE FadeIn      $1C73        ; palette from $C9C0 (the title palette while on the title)
.DEFINE MakePassword $8336       ; bank 7: make the password and draw it at HL (Winkle: $3D0C)

; ---------------------------------------------------------------------------------------------
; Hooks into the game's code (bank 0, and Winkle's text command in bank 7)
.BANK 0 SLOT 0
.ORGA $27F8                      ; title: a button was pressed; was ld hl,$3D00 (NEW GAME / CONTINUE)
.SECTION "hook_title_button" OVERWRITE
    jp Stub_TitleButton
.ENDS
.ORGA $284B                      ; CONTINUE chosen; was ld hl,$28C2 (LAST GAME / PASSWORD CONTINUE)
.SECTION "hook_continue" OVERWRITE
    jp Stub_ContMenu
.ENDS
.ORGA $282B                      ; NEW GAME / CONTINUE buttons; was and $30 / jr nz,$2844 (B = new presses)
.SECTION "hook_newgame_keys" OVERWRITE
    jp Stub_NewGameKeys
    nop
.ENDS
.ORGA $2871                      ; LAST GAME / PASSWORD CONTINUE buttons; was and $30 / jr nz,$288A
.SECTION "hook_lastgame_keys" OVERWRITE
    jp Stub_LastGameKeys
    nop
.ENDS
.ORGA $07C3                      ; wait one frame (every part of the game goes through it): was xor a / ld ($C005),a
.SECTION "hook_wait_frame" OVERWRITE
    jp Stub_Wait
    nop
.ENDS
.ORGA $12C8                      ; New Game: was xor a / ld ($C83E),a
.SECTION "hook_new_game" OVERWRITE
    call Hook_NewGame             ; (returns with A = 0, Z, as the original)
    nop
.ENDS
.ORGA $13E3                      ; continue: was ld a,$B4 / ld ($C80D),a (start screen L14); $13E8 = ret
.SECTION "hook_start_screen" OVERWRITE
    call Hook_StartScreen
    nop
    nop
.ENDS
.ORGA $1391                      ; every game start: was ld a,$88 / ld ($C102),a / ld a,$98 / ld ($C101),a (hero x, y)
.SECTION "hook_start_position" OVERWRITE
    call Hook_StartPos
    .DB 0, 0, 0, 0, 0, 0, 0       ; nop x 7 -> $139B
.ENDS
.ORGA $293F                      ; the overworld's status screen is drawn (display still off): was call $0D20
.SECTION "hook_pause_hint" OVERWRITE
    call Hook_PauseHint
.ENDS
.ORGA $1A52                      ; an area's graphics are loaded ($1A3D): was ld ($C819),a
.SECTION "hook_area" OVERWRITE
    call Hook_Area
.ENDS
.ORGA $3ED3                      ; the overworld hero's step is done (shared tail): was ld a,(ix+$10)
.SECTION "hook_walk_step" OVERWRITE
    call Hook_Step
.ENDS
.ORGA $2942                      ; Pause wait loop: was call $07C3
.SECTION "hook_pause_wait" OVERWRITE
    call Stub_PauseWait
.ENDS
.ORGA $2156                      ; Game Over, a button was pressed: was call $7774 (then back to the title)
.SECTION "hook_game_over" OVERWRITE
    jp Stub_GameOver
.ENDS
.ORGA $1334                      ; LAST GAME CONTINUE: was ld a,$79 / call $0E0B / ld de,$1377 / push de /
.SECTION "keep_gold_last" OVERWRITE  ;   push hl / ret (asset $79 = bank 7 $8014 halves the gold, returns to $1377)
    ld a,(OptKeepGold)            ; KEEP GOLD: the same call, skipped when on ($2996 = call $0E0B / jp (hl))
    or a
    ld a,$79
    call z,$2996
    .DB $18, $1377-$133F          ; jr $1377 (from $133D)
.ENDS
.ORGA $136D                      ; PASSWORD CONTINUE: was ld a,$79 / call $0E0B / ld bc,$1377 / push bc / jp (hl)
.SECTION "keep_gold_password" OVERWRITE
    ld a,(OptKeepGold)
    or a
    ld a,$79
    call z,$2996
    nop                           ; -> $1377
.ENDS
.ORGA $193D                      ; a screen's room record was copied to $CAD0: was ld a,($CAD0)
.SECTION "hook_map_fix" OVERWRITE
    call Hook_Map
.ENDS
.ORGA $33EE                      ; sword damage A to the enemy at IX: was ld e,a / ld a,(ix+$15)
.SECTION "hook_damage_dealt" OVERWRITE
    call Hook_Dealt
    nop
.ENDS
.ORGA $33D1                      ; the sword touches an enemy: was ld a,(ix+$1F) (its hit cooldown; damage only at 0)
.SECTION "hook_sword_contact" OVERWRITE
    call Hook_Contact
.ENDS
.ORGA $35EA                      ; gold drop E (BCD tens): was the 3-byte BCD add $35EA-$3603, then the cap
.SECTION "hook_gold_drop" OVERWRITE
    call Hook_Gold                ; (returns carry = overflow, as the add)
    .DB $18, $3604-$35EF          ; jr $3604 (from $35ED): the cap
.ENDS
.ORGA $37D8                      ; the hero touched something: was ld a,(hl) / ld hl,$C81A (damage < 0, healing > 0)
.SECTION "hook_damage_received" OVERWRITE
    call Hook_Recv
    nop
.ENDS
.ORGA $3509                      ; an enemy tests its box against the sword objects: was ld a,($C120)
.SECTION "hook_sword_reach" OVERWRITE
    call Hook_Reach
.ENDS
.ORGA $3F45                      ; the three sword swing starts: was ld a,4 / ld ($C120),a
.SECTION "hook_swing_1" OVERWRITE
    call Hook_Swing
    nop
    nop
.ENDS
.ORGA $0306                      ; DrawString's character fetch: was ld a,(hl) / inc hl / or a (credits: bank 15)
.SECTION "hook_draw_string" OVERWRITE
    call Hook_Str
.ENDS
.BANK 1 SLOT 1
.ORGA $4646                      ; top-down cave hero: was call $2F34 (velocity = direction x speed (ix+4))
.SECTION "hook_cave_down" OVERWRITE
    call Hook_Cave
.ENDS
.ORGA $43AC
.SECTION "hook_swing_2" OVERWRITE
    call Hook_Swing
    nop
    nop
.ENDS
.ORGA $4678
.SECTION "hook_swing_3" OVERWRITE
    call Hook_Swing
    nop
    nop
.ENDS
.BANK 7 SLOT 2
.ORGA $8EC8                      ; the cave-text interpreter's character fetch: was ld a,(hl) / cp $F0
.SECTION "hook_text" OVERWRITE
    call Hook_Text
.ENDS
.ORGA $9138                      ; Winkle (text command $FA): was call $8336 (make + draw the password)
.SECTION "hook_winkle" OVERWRITE
    call Stub_Winkle
.ENDS

; ---------------------------------------------------------------------------------------------
; Bank 1 stubs (fixed slot): every switch of slot 2 between the game and our bank 16
.BANK 1 SLOT 1
.ORGA $7E99
.SECTION "gvplus_stubs" FORCE
; Pagers. Enter16 / Map16 / Map2 end in `ret` after a constant bank write, so the recompiler knows
; the bank after `call` them. A hook that returns into the game: site `call Hook_X` ->
; `Hook_X: call Enter16 ; jp X` (bank 16), X ends `jp Leave16` (the game's bank back, AF kept).
; SavedBank is one byte: a handler must not pass through another Enter16 hook (Stub_Wait, which
; every frame wait goes through, keeps its bank on the stack instead).
Enter16:                         ; (AF kept: the hooks below run in the middle of the game's code)
    push af
    ld a,($FFFF)
    ld (SavedBank),a
    ld a,16
    ld ($FFFF),a
    pop af
    ret
Map16:
    ld a,16
    ld ($FFFF),a
    ret
Map2:                            ; the title's bank (exits into the title menus)
    ld a,2
    ld ($FFFF),a
    ret
Leave16:
    push af
    ld a,(SavedBank)
    ld ($FFFF),a
    pop af
    ret

Hook_NewGame:                    ; from $12C8
    call Enter16
    jp NewGameH
Hook_StartScreen:                ; from $13E3
    call Enter16
    jp StartScreenH
Hook_StartPos:                   ; from $1391
    call Enter16
    jp StartPosH
Hook_Map:                        ; from $193D
    call Enter16
    jp MapH
Hook_Dealt:                      ; from $33EE
    call Enter16
    jp DealtH
Hook_Gold:                       ; from $35EA
    call Enter16
    jp GoldH
Hook_Recv:                       ; from $37D8
    call Enter16
    jp RecvH
Hook_Reach:                      ; from $3509
    call Enter16
    jp ReachH
Hook_Swing:                      ; from $3F45, $43AC, $4678
    call Enter16
    jp SwingH
Hook_Contact:                    ; from $33D1
    call Enter16
    jp ContactH
Hook_Cave:                       ; from bank 1 $4646: the speed, then the original call in the game's bank
    call Enter16
    call CaveH
    call Leave16
    jp $2F34
Hook_Text:                       ; from bank 7 $8EC8: the character is read in bank 7, checked in bank 16
    ld a,(hl)
    call Enter16
    jp TextH
Hook_Str:                        ; from $0306 (DrawString): the same for the credits (bank 15)
    ld a,(hl)
    inc hl
    call Enter16
    jp StrH

Stub_TitleButton:                ; from $27F8
    call Map16
    jp TitleButton
Stub_ContMenu:                   ; from $284B
    call Map16
    jp ContMenu
Hook_Step:                       ; from $3ED3: another 1-pixel step this frame, or the tail (A = (ix+$10))
    call Enter16
    jp StepH
Step_Again:                      ; StepH: one more step: the game's bank back, drop the hook's return,
    call Leave16                 ; the step dispatch again (same input $C801)
    pop hl
    jp $3D40
Hook_PauseHint:                  ; from $293F (slot 2 may stay bank 16: the pause loop and its end map their own)
    call Map16
    jp PauseHintH
Stub_GameOver:                   ; from $2156
    call Map16
    jp GameOverAsk
Stub_NewGameKeys:                ; button 1 = the original select, button 2 = back to START / OPTIONS
    bit 4,b
    jp nz,$2844
    bit 5,b
    jp z,$282F
    call Map16
    jp TopMenu
Stub_LastGameKeys:               ; button 1 = the original select, button 2 = back to PASSWORD / SAVE
    bit 4,b
    jp nz,$288A
    bit 5,b
    jp z,$2875
    call Map16
    jp ContMenuPw

Exit_TitleWait:                  ; -> the title's wait for a button (music keeps playing, the intro demo after a while)
    call Map2
    jp $27AE
Exit_BackToContinue:             ; -> its menu loop (BackToContinue drew the menu with the cursor on CONTINUE)
    call Map2
    jp $281D
Exit_Password:                   ; -> the original LAST GAME / PASSWORD CONTINUE menu (HL = $28C2)
    call Map2
    jp $284E
Exit_LoadGame:                   ; a slot was loaded into RAM: the game's LAST GAME CONTINUE
    call Map2
    jp $131B

Stub_ReadB12:                    ; A = the byte at HL in bank 12 (the font asset), back to bank 16
    ld a,12
Stub_ReadB:                      ; A = bank: A = the byte at HL there, back to bank 16 (BC, DE, HL kept)
    ld ($FFFF),a
    ld a,(hl)
    push af
    call Map16
    pop af
    ret
Stub_Sram:                       ; copy BC bytes HL -> DE with the battery RAM over slot 2 ($8000-$BFFF)
    di
    ld a,$08
    ld ($FFFC),a
    ldir
    xor a
    ld ($FFFC),a                 ; slot 2 = the ROM bank in $FFFF again (16 for our callers)
    ei
    ret
Stub_Wait:                       ; from $07C3: play time, then the original wait (any slot-2 bank comes back)
    ld a,($FFFF)
    push af
    call Map16
    call PlayTick
    pop af
    ld ($FFFF),a
    xor a
    ld ($C005),a
    jp $07C7
Stub_Unpause72:                  ; asset $72 (bank 7 $82C2: VRAM $1800 -> name table, $1F00 -> sprite rows), back to bank 16
    ld a,$72
    call $2996                   ; the game's `call $0E0B ; jp (hl)`
    jp Map16
Stub_PauseWait:                  ; from $2942: the wait (paused: no play time), button 1 on the status screen = SAVE GAME
    xor a
    ld ($C005),a
    call $07C7
    ld a,($C802)
    cp 6                         ; only the overworld has the status screen
    ret nz
    call ReadPad
    ld a,(PadNew)
    and $30                      ; button 1 = SAVE GAME, button 2 = the map
    ret z
    pop hl                       ; (back into the pause loop later by jp $291F)
    call Map16
    jp PauseButton
Stub_Winkle:                     ; from bank 7 $9138: the password, then the AUTO save
    ld a,7                       ; (bank 7 is in slot 2 already: says so for the recompiler)
    ld ($FFFF),a
    call MakePassword
    call Enter16                 ; (saves bank 7)
    call AutoSave
    jp Leave16
Hook_Area:                       ; from $1A52: the area whose graphics the game loads (the map pictures put
    ld ($C819),a                 ; them back afterwards)
    ld (GfxId),a
    ret
Stub_Unpack:                     ; A = asset, DE = VRAM: the game's tile unpacker ($0E0B maps the asset's
    push de                      ; bank), back to bank 16
    call LoadAsset
    pop de
    call UnpackTiles
    jp Map16
.ENDS

; ---------------------------------------------------------------------------------------------
; Bank 16
.BANK 16 SLOT 2
.ORG $0000
.SECTION "gvplus_id" FORCE
GvPlusId:
    .DB "GOLVELLIUS PLUS", 0     ; 16 bytes
    .DB 1                         ; patch format version
    .DB 7                         ; step / feature level (7 = EASY CAVE ENTRY, 11 options on two pages)
.ENDS

.SECTION "gvplus_title" FREE

; --- title menus (rows 20 / 22 under the title picture, cursor at column 10, like the game's) ---
TitleButton:                     ; from the title: keep its two text rows for going back
    ld hl,$3D00
    di
    ld a,l
    out ($BF),a
    ld a,h
    out ($BF),a                  ; VRAM read address (bit 6 clear)
    ld hl,TitleRows
    ld b,0
@copy:
    call VdpDelay
    in a,($BE)
    ld (hl),a
    inc hl
    djnz @copy
    ei
TopMenu:
    xor a
    ld (MenuSel),a
TopMenuSel:                      ; MenuSel already set
    call ClearMenuRows
    ld hl,StrStart
    call DrawString
    ld hl,StrOptions
    call DrawString
    call Menu2
    cp 2
    jr z,@back
    ld a,(MenuSel)
    or a
    jr nz,@options
    call ClearMenuRows           ; START: the original NEW GAME / CONTINUE ($2806-$280F draw it)
    ld hl,$28AC
    call DrawString
    ld hl,$28B7
    call DrawString
    call FindNewest              ; a save slot -> the cursor on CONTINUE
    ld a,1
    jr nc,@cur
    ld a,($C817)                 ; else the game's own rule: CONTINUE when a game is still in RAM
    or a
    jr z,@cur
    ld a,1
@cur:
    ld (MenuSel),a
    jp Exit_BackToContinue
@options:
    call OptionsPage
    ld a,1
    ld (MenuSel),a
    jr TopMenuSel
@back:                           ; the title's text rows back, then its wait for a button
    ld hl,$3D00
    di
    call SetVramAddr
    ld hl,TitleRows
    ld b,0
@put:
    call VdpDelay
    ld a,(hl)
    out ($BE),a
    inc hl
    djnz @put
    ei
    jp Exit_TitleWait

ContMenu:                        ; the cursor on SAVE when a slot is used, else on PASSWORD
    call FindNewest
    ld a,0
    jr c,ContMenuSet
    inc a
    jr ContMenuSet
ContMenuPw:                      ; back from the LAST GAME / PASSWORD CONTINUE menu: on PASSWORD
    xor a
ContMenuSet:
    ld (MenuSel),a
ContMenuSel:
    call ClearMenuRows
    ld hl,StrPassword
    call DrawString
    ld hl,StrSave
    call DrawString
    call Menu2
    cp 2
    jr z,@back
    ld a,(MenuSel)
    or a
    jr nz,@save
    call ClearMenuRows           ; PASSWORD
    ld hl,$28C2
    jp Exit_Password
@save:
    call SavePage                ; (does not return when a slot is loaded)
    ld a,1
    ld (MenuSel),a
    jr ContMenuSel
@back:
    call ClearMenuRows
    ld hl,$28AC                  ; "NEW GAME" / "CONTINUE" (bank 0), as $2806-$280F
    call DrawString
    ld hl,$28B7
    call DrawString
    ld a,1                       ; the cursor on CONTINUE (the menu's own start $2806 picks NEW GAME
    ld (MenuSel),a               ; when no game is in RAM)
    jp Exit_BackToContinue

; rows 20-23 of the name table = the menu area (as the game's $27F8 does it)
ClearMenuRows:
    ld a,1
    ld (TileHigh),a
    ld hl,$3D00
    ld bc,$0100
    jp FillVram

; two-entry menu loop (the game's own sequence: cursor off, pad, cursor on): A = 1 select, 2 back
Menu2:
    call WaitFrame
    xor a
    call MenuCursor
    call ReadPad
    ld a,(PadNew)
    ld b,a
    and $10
    jr nz,@select
    ld a,b
    and $20
    jr nz,@back
    ld a,b
    and $0F
    jr z,@draw
    ld a,(MenuSel)
    inc a
    and 1
    ld (MenuSel),a
@draw:
    or a                         ; carry clear for the cursor routine
    ld a,$61
    call MenuCursor
    jr Menu2
@select:
    ld a,1
    ret
@back:
    ld a,2
    ret

; --- pages: a second name table at $3000, the title stays untouched underneath ---
PageOpen:                        ; fade out, display off, digits, an empty page (draw, then PageShow)
    call FadeOut
    call ScreenOff
    ld hl,($DF-$40)*8            ; the font's digits $DF-$E8 -> title tiles 256+$B0..$B9 ($3600)
    ld (UpLo),hl
    ld hl,80
    ld (UpCnt),hl
    ld hl,$3600
    ld (UpDst),hl
    call UnpackFont
    ld hl,$2000+ColonTile*32
    call ColonUp
    ld a,1
    ld (NtAttr),a
    ld a,$30
    ld (NtBaseHi),a
    ld a,$B0
    ld (NtDigit0),a
    ld a,22
    ld (HelpRow),a
    xor a
    ld (NtColBias),a
    ld (NtTileOfs),a
    ld hl,$3000
    di
    call SetVramAddr
    ld bc,32*24
@clear:
    ld a,$40
    out ($BE),a
    ld a,1
    out ($BE),a
    dec bc
    ld a,b
    or c
    jr nz,@clear
    ei
    ret
PageShow:
    ld a,$FD                     ; name table at $3000
    call SetNameTable
    call ScreenOn
    jp FadeIn                    ; the title palette ($C9C0)
PageClose:
    call FadeOut
    call ScreenOff
    ld a,$FF                     ; the title's name table ($3800) again
    call SetNameTable
    call ScreenOn
    jp FadeIn

SetNameTable:                    ; A = VDP register 2
    ld (VdpReg2),a
    di
    out ($BF),a
    ld a,$82
    out ($BF),a
    ei
    ret

; --- writing into a name table (works with the display on: one character at a time) ---
NtAt:                            ; B = row, C = column of the name table NtBaseHi
    push hl
    ld l,b
    ld h,0
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl                    ; row * 64
    ld a,(NtColBias)
    add a,c
    and 31
    add a,a
    add a,l
    ld l,a
    ld a,(NtBaseHi)
    adc a,h
    ld h,a
    ld (NtAddr),hl
    pop hl
    ret
NtChar:                          ; A = character: tile A + NtTileOfs (NtTile: A = the tile itself)
    push hl
    ld hl,NtTileOfs
    add a,(hl)
    pop hl
NtTile:                          ; A = tile, high byte NtAttr, at NtAddr (advances; column 31 -> 0 of the same row)
    push hl
    push af
    ld hl,(NtAddr)
    di
    call SetVramAddr
    pop af
    out ($BE),a
    call VdpDelay
    ld a,(NtAttr)
    out ($BE),a
    ei
    inc hl
    inc hl
    ld a,l
    and $3F
    jr nz,@row
    ld a,l                       ; past column 31 (NtColBias moved the text right): wrap within the row
    sub $40
    ld l,a
    jr nc,@row
    dec h
@row:
    ld (NtAddr),hl
    pop hl
    ret
NtPrint:                         ; HL -> text, 0 (digits $B0-$B9 go through NtDigit: either font)
    ld a,(hl)
    inc hl
    or a
    ret z
    cp $B0
    jr c,@chr
    cp $BA
    jr nc,@chr
    sub $B0
    call NtDigit
    jr NtPrint
@chr:
    call NtChar
    jr NtPrint
NtSpaces:                        ; B spaces
    ld a,$40
    call NtChar
    djnz NtSpaces
    ret
NtDigit:                         ; A = 0-9
    push hl
    ld hl,NtDigit0
    add a,(hl)
    pop hl
    jp NtTile
NtBcd:                           ; HL -> B bytes of BCD, leading zeros left out; returns Z when the value is 0 (then "0" is printed)
    ld e,0                       ; E = digits printed
@byte:
    ld a,(hl)
    rrca
    rrca
    rrca
    rrca
    and $0F
    call @digit
    ld a,(hl)
    and $0F
    call @digit
    inc hl
    djnz @byte
    inc e
    dec e
    ret nz
    xor a
    call NtDigit
    xor a                        ; Z
    ret
@digit:
    or a
    jr nz,@print
    inc e
    dec e
    ret z                        ; a leading zero
@print:
    call NtDigit
    inc e
    ret
NtBcd2:                          ; A = BCD byte, both digits
    push af
    rrca
    rrca
    rrca
    rrca
    and $0F
    call NtDigit
    pop af
    and $0F
    jp NtDigit

; --- digits for the pages: the digit tiles of the game's font (asset $C0 = bank 12, entry 0;
; 4 bit-planes, each a list of runs: n < $80 = n bytes, n >= $80 = the next byte n & $7F times,
; 0 = end; its first tile is $40, the digits are tiles $DF-$E8) -> VRAM $3600 (tiles 256+$B0..$B9).
; Only those 80 bytes per plane are written; nothing else of the font is unpacked. ---
; the font (asset $C0 = bank 12 entry 0, 4 RLE planes, every 4th byte) walked byte by byte; only the
; window UpLo / UpCnt (plane bytes, 8 per tile) is written, to VRAM UpDst (tiles in their plane order)
UnpackFont:                      ; (records wholly outside UpLo .. UpEnd-1 are skipped without reading them)
    ld hl,(UpLo)
    ld de,(UpCnt)
    add hl,de
    ld (UpEnd),hl
    ld hl,$8000                  ; the asset pointer of entry 0
    call Stub_ReadB12
    ld e,a
    inc hl
    call Stub_ReadB12
    ld d,a
    ex de,hl
    ld c,0                       ; plane
@plane:
    ld de,0                      ; byte number in this plane (8 per tile)
@next:
    call @read
    or a
    jr z,@endplane
    jp m,@run
    ld b,a
    call @outside
    jr nc,@lit
    ld a,l                       ; skip B literal bytes: HL += B, DE += B
    add a,b
    ld l,a
    jr nc,@skipde
    inc h
    jr @skipde
@lit:
    call @read
    call @put
    djnz @lit
    jr @next
@run:
    and $7F
    ld b,a
    call @read
    call @outside
    jr c,@skipde
@rep:
    call @put
    djnz @rep
    jr @next
@skipde:                         ; DE += B
    ld a,e
    add a,b
    ld e,a
    jr nc,@next
    inc d
    jr @next
@outside:                        ; carry = bytes DE .. DE+B-1 all outside UpLo .. UpEnd-1 (A, BC, HL kept)
    push hl
    push af
    inc b
    dec b
    jr z,@in                     ; B = 0 (256 times): the byte-by-byte path
    ld hl,(UpEnd)
    or a
    sbc hl,de
    jr c,@out                    ; DE >= UpEnd
    jr z,@out
    push de
    ld l,b
    ld h,0
    add hl,de
    ld de,(UpLo)
    or a
    sbc hl,de                    ; DE + B <= UpLo
    pop de
    jr c,@out
    jr z,@out
@in:
    pop af
    pop hl
    scf
    ccf
    ret
@out:
    pop af
    pop hl
    scf
    ret
@endplane:
    inc c
    ld a,c
    cp 4
    jr nz,@plane
    ret
@read:
    call Stub_ReadB12
    inc hl
    ret
@put:                            ; A = byte DE of plane C (B, C, HL kept; DE advances)
    push hl
    push bc
    push af
    push de
    ex de,hl
    ld de,(UpLo)
    or a
    sbc hl,de                    ; HL = byte - UpLo
    jr c,@skip
    ld de,(UpCnt)
    or a
    sbc hl,de
    jr nc,@skip
    add hl,de
    add hl,hl
    add hl,hl
    ld a,l
    or c
    ld l,a                       ; 4 * (byte - UpLo) + plane
    ld de,(UpDst)
    add hl,de
    di
    call SetVramAddr
    pop de
    pop af
    out ($BE),a
    ei
    jr @done
@skip:
    pop de
    pop af
@done:
    pop bc
    pop hl
    inc de
    ret

; --- play time (every frame wait): only while a game runs (modes 5-$12; not the title, not Game Over) ---
PlayTick:
    ld a,(OptLoaded)             ; the first frame wait after power-on: the options from the battery RAM
    or a
    call z,OptLoad
    ld a,(AutoBlink)             ; Winkle's "GAME SAVED TO AUTO." blinks
    or a
    call nz,AutoTick
    ld a,($C802)
    cp 5
    ret c
    cp $13
    ret nc
    cp $11                       ; inside a cave room ($11) or a dungeon (7, 8, boss $0A): the map's "entered"
    jr z,@ent
    cp 7
    jr z,@ent
    cp 8
    jr z,@ent
    cp $0A
    jr nz,@time
@ent:
    ld a,($C80D)                 ; (the overworld screen of the entrance; 182 = the New Game start area)
    cp 182
    jr nc,@time
    ld hl,MapEnt
    call MarkSet
@time:
    ld hl,PlayTime
    inc (hl)
    ld a,(hl)
    sub 60
    ret c
    ld (hl),a                    ; frames
    inc hl
    inc (hl)
    ld a,(hl)
    sub 60
    ret c
    ld (hl),a                    ; seconds
    inc hl
    ld a,(hl)
    add a,1
    daa
    ld (hl),a
    sub $60
    ret c
    ld (hl),a                    ; minutes (BCD)
    inc hl
    ld a,(hl)
    cp $99
    ret z
    add a,1
    daa
    ld (hl),a                    ; hours (BCD)
    ret

; --- battery RAM ---
SramMagic: .DB "GVPLUS", 1

SramPrepare:                     ; a battery RAM without our header gets one and empty slots; LastSlot from it
    ld hl,$8000
    ld de,Tmp
    ld bc,8
    call Stub_Sram
    ld hl,Tmp
    ld de,SramMagic
    ld b,7
@cmp:
    ld a,(de)
    cp (hl)
    jr nz,@init
    inc hl
    inc de
    djnz @cmp
    ld a,(Tmp+7)
    cp 6
    jr c,@ok
    xor a
@ok:
    ld (LastSlot),a
    ret
@init:
    ld hl,SramMagic
    ld de,Tmp
    ld bc,7
    ldir
    xor a
    ld (de),a                    ; Tmp+7: slot loaded last = AUTO
    ld (LastSlot),a
    ld (Tmp+8),a
    ld hl,Tmp
    ld de,$8000
    ld bc,8
    call Stub_Sram
    ld b,6                       ; every slot empty
@clear:
    push bc
    ld a,b
    dec a
    call SlotAddr
    ex de,hl
    ld hl,Tmp+8
    ld bc,1
    call Stub_Sram
    pop bc
    djnz @clear
    ret

WriteLastSlot:
    ld a,(LastSlot)
    ld (Tmp),a
    ld hl,Tmp
    ld de,$8007
    ld bc,1
    jp Stub_Sram

SlotAddr:                        ; A = slot -> HL = its battery RAM address
    ld l,a
    ld h,0
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    ld de,$8100
    add hl,de
    ret

SlotSum:                         ; A = check byte of SlotBuf
    ld hl,SlotBuf
    ld b,63
    xor a
@add:
    add a,(hl)
    inc hl
    djnz @add
    xor $5A
    ret

ReadSlot:                        ; A = slot -> SlotBuf; carry set = empty or damaged
    call SlotAddr
    ld de,SlotBuf
    ld bc,64
    call Stub_Sram
    ld a,(SlotBuf)
    cp $A5
    scf
    ret nz
    call SlotSum
    ld hl,SlotBuf+63
    cp (hl)
    scf
    ret nz
    or a                         ; carry clear
    ret

SaveSlot:                        ; A = slot, B = hero y, C = x, D = start screen, SvFlag = its $C84C: the game -> battery RAM
    ld (MapSlot),a               ; (its map block follows the record)
    push af
    push bc
    push de
    call FindNewest              ; Tmp+11 = the highest save number so far
    ld hl,(Tmp+11)
    inc hl
    ld (Tmp+14),hl               ; this save's number
    pop de
    pop bc
    ld hl,SlotBuf
    ld (hl),$A5
    inc hl
    ld (hl),d
    inc hl
    ld (hl),b
    inc hl
    ld (hl),c
    inc hl
    ex de,hl
    ld hl,PlayTime
    ld bc,4
    ldir
    ex de,hl
    ld a,(SvFlag)
    ld (hl),a
    inc hl
    ld a,($C817)
    ld (hl),a
    inc hl
    ex de,hl
    ld hl,$C839
    ld bc,19
    ldir
    ld hl,$C86E
    ld bc,11
    ldir
    ex de,hl                     ; HL = +40: the save number
    ld de,(Tmp+14)
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ld b,21
    xor a
@zero:
    ld (hl),a
    inc hl
    djnz @zero
    call SlotSum
    ld (SlotBuf+63),a
    pop af
    call SlotAddr
    ex de,hl
    ld hl,SlotBuf
    ld bc,64
    call Stub_Sram
    jp SaveMap

; A = the newest save slot (the highest save number; slots saved before save numbers existed have 0:
; then the slot loaded last if it is used, else the first used one); carry = no slot is used.
; Tmp+11 = the highest save number. Uses SlotBuf, Tmp+10..+13.
FindNewest:
    call SramPrepare             ; (LastSlot; a battery RAM without our header gets one)
    ld a,$FF
    ld (Tmp+10),a                ; best slot
    ld hl,0
    ld (Tmp+11),hl               ; its number
    xor a
@slot:
    ld (Tmp+13),a
    call ReadSlot
    jr c,@next
    ld a,(Tmp+10)
    inc a
    jr z,@take                   ; the first used slot
    ld hl,(SlotBuf+40)
    ld de,(Tmp+11)
    or a
    sbc hl,de
    jr c,@next
    jr z,@next
@take:
    ld a,(Tmp+13)
    ld (Tmp+10),a
    ld hl,(SlotBuf+40)
    ld (Tmp+11),hl
@next:
    ld a,(Tmp+13)
    inc a
    cp 6
    jr c,@slot
    ld a,(Tmp+10)
    inc a
    scf
    ret z                        ; none
    ld hl,(Tmp+11)
    ld a,h
    or l
    jr nz,@have
    ld a,(LastSlot)              ; only old saves: the slot loaded last, if it is used
    push af
    call ReadSlot
    pop bc
    ld a,b
    ret nc
@have:
    ld a,(Tmp+10)
    or a
    ret

; $3ED3 OVERWORLD SPEED: the hero's 1-pixel step (one of $3D8F / $3E06 / $3E49 / $3E8E, each with the
; game's own wall, turning, cave and screen-edge checks) is done and the shared tail (animation,
; $322C / $3425 / $3766) follows. On the overworld (mode 6) the step runs OptWalk more times per
; frame: back to the dispatch $3D40 (same input) until the extra steps are used up or a scroll
; ($C814) / a cave entrance (mode $11) started; the tail runs once.
StepH:                           ; (only A and the flags change: the tail may use the other registers)
    ld a,(OptEasyCave)
    or a
    call nz,CaveEasy
    ld a,(OptWalk)
    or a
    jr z,@tail
    ld a,(StepFlags)
    bit 0,a
    jr nz,@more                  ; an extra step of this frame
    xor 2                        ; the frame's own step: this frame's extra steps (2X: 1, 1.5X: 1 every
    ld (StepFlags),a             ; other frame)
    ld a,(ix+5)                  ; the facing before this frame's steps ($3D64 stores it before every step)
    ld (StepFace),a
    ld a,(OptWalk)
    cp 2
    ld a,1
    jr z,@set
    ld a,(StepFlags)
    and 2
    rrca
@set:
    ld (StepLeft),a
@more:
    ld a,($C802)
    cp 6
    jr nz,@done
    ld a,($C814)
    or a
    jr nz,@done
    ld a,(StepLeft)
    or a
    jr z,@done
    dec a
    ld (StepLeft),a
    ld a,(StepFlags)
    or 1
    ld (StepFlags),a
    jp Step_Again
@done:
    ld a,(StepFlags)
    and $FE
    ld (StepFlags),a
    ld a,(StepFace)              ; the tail compares the facing with the one before the frame's FIRST step:
    ld (ix+5),a                  ; a turn made in an earlier step of this frame still restarts the animation
@tail:                           ; and sets the sprite frame $C103, which the sword takes its direction from

    ld a,(ix+$10)                ; the original instruction
    jp Leave16

; EASY CAVE ENTRY (from StepH, after every 1-pixel step on the overworld). The game enters a cave only when
; a step ends exactly on the entrance cell's point (x = 16 n, y = 16 n + 8: $3DC7, shared by all four step
; handlers) and the terrain there is 0 (an opening). Here: the entrance point nearest to the hero (and the
; other one when he is exactly half-way, 8 pixels off), if this step brought him closer to it (so walking
; out of a cave never pulls him back in) and he was already within 16 pixels of it (a continuous walk, not
; a screen change): the hero is put on that point and the game's own entry follows ($C813 = 1, mode $11,
; +$1D = $FF, +$1E = 0, as at $3DE9).
CaveEasy:
    ld a,($C802)
    cp 6
    ret nz
    ld a,($C814)
    or a
    ret nz
    push bc
    push de
    push hl
    ld a,(ix+2)                  ; the nearest point
    add a,8
    and $F0
    ld c,a
    ld a,(ix+1)
    and $F0
    add a,8
    ld b,a
    call @try
    jr c,@done
    ld a,(ix+2)                  ; half-way across: also the point to the left
    and $0F
    cp 8
    jr nz,@noleft
    ld a,c
    sub 16
    ld c,a
    call @try
    jr c,@done
    ld a,c
    add a,16
    ld c,a
@noleft:
    ld a,(ix+1)                  ; half-way down: also the point above
    and $0F
    jr nz,@done
    ld a,b
    sub 16
    ld b,a
    call @try
    jr c,@done
    ld a,(ix+2)
    and $0F
    cp 8
    jr nz,@done
    ld a,c
    sub 16
    ld c,a
    call @try
@done:
    ld a,(ix+2)
    ld (CaveLastX),a
    ld a,(ix+1)
    ld (CaveLastY),a
    pop hl
    pop de
    pop bc
    ret
@try:                            ; B = y, C = x of an entrance point: carry = entered
    ld a,c                       ; inside the playfield (x 16..240, y 8..184)
    sub 16
    cp 225
    ret nc
    ld a,b
    sub 8
    cp 177
    ret nc
    ld a,(CaveLastX)             ; the distance before this step (each axis within 16)
    ld e,c
    call AbsDiff
    cp 17
    ret nc
    ld d,a
    ld a,(CaveLastY)
    ld e,b
    call AbsDiff
    cp 17
    ret nc
    add a,d
    ld d,a
    ld a,(ix+2)                  ; ... and now
    ld e,c
    call AbsDiff
    ld h,a
    ld a,(ix+1)
    ld e,b
    call AbsDiff
    ld l,a
    add a,h
    cp d
    ret nc                       ; not closer
    ld a,(OptEasyCave)           ; HALF: in line with the point on one axis (at least half of the hero over
    dec a                        ; the entrance); QUARTER: any candidate (8 off on both axes = a quarter)
    jr nz,@near
    ld a,h
    or a
    jr z,@near
    ld a,l
    or a
    ret nz                       ; (carry clear: not entered)
@near:
    ld l,(ix+1)
    ld h,(ix+2)
    ld (ix+1),b                  ; the terrain at the point ($2D06 with A = 0, as the game's test)
    ld (ix+2),c
    push hl
    push bc
    xor a
    call $2D06
    pop bc
    pop hl
    or a
    jr z,@in
    ld (ix+1),l                  ; not an opening: the hero stays where he is
    ld (ix+2),h
    or a
    ret
@in:
    ld a,1
    ld ($C813),a
    ld a,$11
    ld ($C802),a
    ld (ix+$1D),$FF
    ld (ix+$1E),0
    scf
    ret
AbsDiff:                         ; A = |A - E|
    sub e
    ret nc
    neg
    ret

; ===== the map (Pause on the overworld + button 2) =====
; Remembered per game: MapVis (screens seen: marked when a screen is loaded, $193D) and MapEnt (caves /
; dungeons entered: marked by the frame tick in modes $11 / 7 / 8 / $0A), kept with every save slot in
; battery RAM $8300 + 64 n ($A5, the 46 bytes, check byte = sum xor $5A). The page: a 13 x 14 grid on the
; Pause screen's name table, seen screens in their area's colour (a dot = its cave entered), your screen
; blinks white, the cursor blinks as a frame; button 1 on a seen screen = its page (area, what its cave
; holds, the advice text; only for caves entered, like the PC HUD), arrows there = the next seen screen
; that way. Cave data: screen -> room bank 5 $9890, room record $9947 (+3 = cave id); area id bank 5
; $9E48 (low nibble, 0 = the previous screen's); bank 7: people $8CDA, offers / advice $8C8F, prices
; $8D87, herb caves $944A, texts $967C, boss names $933C (length-prefixed DESPA .. HAIDEE).

PauseButton:                     ; the Pause (status) screen: button 1 = SAVE GAME, button 2 = the map
    ld a,(PadNew)
    and $10
    jp nz,PauseSave
    jp PauseMap

BitPtr:                          ; A = screen, HL = a bit set -> HL = its byte, A = its mask (BC, DE kept)
    push bc
    ld c,a
    srl a
    srl a
    srl a
    add a,l
    ld l,a
    jr nc,@nc
    inc h
@nc:
    ld a,c
    and 7
    ld b,a
    ld a,1
    jr z,@done
@sh:
    add a,a
    djnz @sh
@done:
    pop bc
    ret
MarkSet:                         ; A = screen, HL = a bit set: set its bit
    call BitPtr
    or (hl)
    ld (hl),a
    ret
MapClear:                        ; an empty map
    ld hl,MapVis
    ld b,MapBytes
    xor a
@c:
    ld (hl),a
    inc hl
    djnz @c
    ret
MapAddr:                         ; A = slot -> HL = its map block ($8300 + 64 n)
    call SlotAddr
    ld de,$0200
    add hl,de
    ret
SaveMap:                         ; the map -> the block of slot MapSlot
    ld hl,SlotBuf
    ld (hl),$A5
    inc hl
    ex de,hl
    ld hl,MapVis
    ld bc,MapBytes
    ldir
    ld hl,SlotBuf
    ld b,MapBytes+1
    call OptSumB
    ld (SlotBuf+MapBytes+1),a
    ld a,(MapSlot)
    call MapAddr
    ex de,hl
    ld hl,SlotBuf
    ld bc,MapBytes+2
    jp Stub_Sram
LoadMap:                         ; A = slot: its map block -> the map (none / damaged: an empty map)
    call MapAddr
    ld de,SlotBuf
    ld bc,MapBytes+2
    call Stub_Sram
    ld a,(SlotBuf)
    cp $A5
    jr nz,MapClear
    ld hl,SlotBuf
    ld b,MapBytes+1
    call OptSumB
    ld hl,SlotBuf+MapBytes+1
    cp (hl)
    jr nz,MapClear
    ld hl,SlotBuf+1
    ld de,MapVis
    ld bc,MapBytes
    ldir
    ret

DivMod13:                        ; A = screen -> B = row (0-13), C = column (0-12)
    ld b,0
@sub:
    cp 13
    jr c,@done
    sub 13
    inc b
    jr @sub
@done:
    ld c,a
    ret
AreaOf:                          ; A = screen -> A = 0 Despa's valley, 1 Rolick's desert, 2 graveyard, 3 swamp,
    ld c,a                       ; 4 lake, 5 Crawky's forest, 6 Haidee's forest, 7 unknown
    ld e,a
    ld d,0
    ld hl,$9E48
    add hl,de
    ld a,5
    call Stub_ReadB
    and 15
    jr nz,@id
    ld a,c
    or a
    jr z,@id
    dec hl
    ld a,5
    call Stub_ReadB
    and 15
@id:
    cp 1
    jr nz,@other
    ld a,c                       ; area 1: the valley (rows 12-14) or the desert above it
    cp 11*13
    ld a,0
    ret nc
    inc a
    ret
@other:
    ld e,a
    ld d,0
    ld hl,AreaIndex
    add hl,de
    ld a,(hl)
    ret
AreaIndex:   .DB 7, 0, 7, 2, 3, 3, 4, 5, 6, 7, 7, 7, 7, 7, 7, 7
AreaBoss:    .DB 1, 2, 3, 4, 5, 6, 7
AreaWords:   .DW StrValley, StrDesert, StrGraveyard, StrSwamp, StrLake, StrForest, StrForest
CaveId:                          ; A = screen -> A = its cave id (bank 5: screen -> room, room record +3)
    ld e,a
    ld d,0
    ld hl,$9890
    add hl,de
    ld a,5
    call Stub_ReadB
    ld l,a
    ld h,0
    ld e,l
    ld d,h
    add hl,hl
    add hl,hl
    add hl,hl
    or a
    sbc hl,de                    ; room * 7
    ld de,$9947+3
    add hl,de
    ld a,5
    jp Stub_ReadB

PrintScreen:                     ; A = screen: "L13"
    call DivMod13
    ld a,c
    add a,'A'
    call NtChar
    ld a,b
    inc a
    cp 10
    jr c,@one
    sub 10
    push af
    ld a,1
    call NtDigit
    pop af
@one:
    jp NtDigit
PrintBoss:                       ; A = 1-8: the boss's name (the game's list in bank 7; 8 = ours); C = its last letter
    cp 8
    jr nz,@list
    ld hl,StrGolvellius
    call NtPrint
    ld c,'S'
    ret
@list:
    ld b,a
    ld hl,$933C
@skip:
    dec b
    jr z,@print
    ld a,7
    call Stub_ReadB
    inc a
    add a,l
    ld l,a
    jr nc,@skip
    inc h
    jr @skip
@print:
    ld a,7
    call Stub_ReadB
    ld b,a
    inc hl
@ch:
    ld a,7
    call Stub_ReadB
    ld c,a
    call NtChar
    inc hl
    djnz @ch
    ret
PrintPoss:                       ; after a name ending in C: "'S " or "' "
    ld a,c
    cp 'S'
    ld hl,StrPossS
    jp z,NtPrint
    ld hl,StrPoss
    jp NtPrint
PrintArea:                       ; A = screen: "DESPA'S VALLEY" (nothing for an unknown area)
    call AreaOf
    cp 7
    ret z
    ld (MapK),a
    ld e,a
    ld d,0
    ld hl,AreaBoss
    add hl,de
    ld a,(hl)
    call PrintBoss
    call PrintPoss
    ld a,(MapK)
    add a,a
    ld e,a
    ld d,0
    ld hl,AreaWords
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    jp NtPrint

Bought:                          ; A = cave id $B0+ -> A = 1 bought / taken, 0 not, $FF has no flag (herbs)
    sub $B0
    ld c,a
    ld e,0
    ld hl,$944A
    ld b,12
@h:
    ld a,7
    call Stub_ReadB
    cp c
    jr z,@none
    jr nc,@stop
    inc e
    inc hl
    djnz @h
@stop:
    ld a,c
    sub e
    ld c,a
    srl a
    srl a
    srl a
    ld e,a
    ld d,0
    ld hl,$C86F
    add hl,de
    ld a,c
    and 7
    ld b,a
    ld a,1
    jr z,@m
@sh:
    add a,a
    djnz @sh
@m:
    and (hl)
    ret z
    ld a,1
    ret
@none:
    ld a,$FF
    ret

CaveLines:                       ; A = screen: what its cave holds, rows MapRow / MapRow+1 from column 2
    ld (MapS2),a
    call CaveId
    ld (MapCid),a
    ld a,(MapRow)
    ld b,a
    ld c,2
    call NtAt
    ld a,(MapCid)
    or a
    jr z,@house
    ld a,(MapS2)
    ld hl,MapEnt
    call BitPtr
    and (hl)
    jr nz,@known
    ld hl,StrCaveQ
    jp NtPrint
@house:
    ld hl,StrStartHouse
    jp NtPrint
@known:
    ld a,(MapCid)
    cp $10
    jr nc,@nd
    ld hl,StrDungeon             ; a dungeon: "DESPA'S CAVE"
    call NtPrint
    ld a,(MapCid)
    call PrintBoss
    call PrintPoss
    ld hl,StrCave
    jp NtPrint
@nd:
    cp $50
    jr nc,@np
    rrca                         ; Randar, Enny, Winkle, Dina
    rrca
    rrca
    rrca
    and 15
    ld e,a
    ld d,0
    ld hl,$8CDA
    add hl,de
    ld a,7
    call Stub_ReadB
    dec a
    cp 4
    jr nc,@cave
    add a,a
    ld e,a
    ld d,0
    ld hl,PeopleStr
    jp @word
@np:
    cp $B0
    jr nc,@offer
    ld hl,StrAdvice
    jp NtPrint
@cave:
    ld hl,StrCave
    jp NtPrint
@offer:
    ld e,a
    ld d,0
    ld hl,$8C8F
    add hl,de
    ld a,7
    call Stub_ReadB
    ld (MapCode),a
    cp $33
    jr c,@item
    cp $36
    jr nc,@cave
    ld hl,StrOldWoman
    jp NtPrint
@item:
    sub $28
    jr c,@cave
    add a,a
    ld e,a
    ld d,0
    ld hl,OfferStr
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    call NtPrint
    ld a,(MapCid)                ; its price: 3 BCD digits x 100 gold, high nibble of the 2nd byte = crystal - 1
    sub $B0
    add a,a
    ld e,a
    ld d,0
    ld hl,$8D87
    add hl,de
    ld a,7
    call Stub_ReadB
    ld (MapLo),a
    inc hl
    ld a,7
    call Stub_ReadB
    ld (MapHi),a
    ld a,(MapCode)
    cp $2A
    jr nz,@line2
    ld a,$40
    call NtChar
    ld a,(MapHi)
    rrca
    rrca
    rrca
    rrca
    and 15
    inc a
    call NtDigit
@line2:
    ld a,(MapRow)
    inc a
    ld b,a
    ld c,2
    call NtAt
    ld a,(MapCode)
    cp $30
    jr z,@free
    ld a,(MapHi)
    and 15
    ld b,a
    ld a,(MapLo)
    or b
    jr z,@free
    ld a,(MapHi)
    and 15
    ld c,a
    jr z,@d2
    call NtDigit
@d2:
    ld a,(MapLo)
    rrca
    rrca
    rrca
    rrca
    and 15
    ld b,a
    or c
    jr z,@d3
    ld a,b
    call NtDigit
@d3:
    ld a,(MapLo)
    and 15
    call NtDigit
    ld hl,StrGoldPrice
    call NtPrint
    jr @status
@free:
    ld hl,StrFree
    call NtPrint
@status:
    ld a,(MapCid)
    call Bought
    or a
    ret z
    inc a
    ret z
    ld hl,StrBought
    ld a,(MapCode)
    cp $30
    jr nz,@b
    ld hl,StrTaken
@b:
    jp NtPrint
@word:
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    jp NtPrint
PeopleStr:   .DW StrRandar, StrEnny, StrWinkle, StrDina
OfferStr:    .DW StrO28, StrO29, StrO2A, StrO2B, StrO2C, StrO2D, StrO2E, StrO2F, StrO30, StrO31, StrO32

TypoFix:                         ; A = the bank-7 character at HL: TEXT FIXES applied (as TextH)
    push bc
    push de
    ld c,a
    ld a,(OptText)
    or a
    jr z,@done
    push hl
    ex de,hl
    ld hl,TypoTable
    ld b,(TypoEnd-TypoTable)/3
@scan:
    ld a,(hl)
    inc hl
    cp e
    jr nz,@skip
    ld a,(hl)
    cp d
    jr nz,@skip
    inc hl
    ld c,(hl)
    jr @found
@skip:
    inc hl
    inc hl
    djnz @scan
@found:
    pop hl
@done:
    ld a,c
    pop de
    pop bc
    ret
MapTextChar:                     ; A = a character of the game's texts: printed; carry = a column was used
    cp $41
    jr c,@sym
    cp $5B
    jr nc,@sym
@put:
    call NtChar
    scf
    ret
@sym:
    or a
    jr nz,@s1
    ld a,$40                     ; $00 = space
    jr @put
@s1:
    cp $5B
    jr c,@s2
    cp $60
    jr c,@put                    ; . ' ! , ?
@s2:
    cp $2C
    jr nz,@s3
    ld a,$5B
    jr @put
@s3:
    cp $6C
    jr z,@put
    cp $6F
    jr z,@q
    cp $70
    jr nz,@s4
@q:
    ld a,$5C                     ; quotes as '
    jr @put
@s4:
    cp $CD
    jr nz,@s5
    ld a,$40                     ; '-' (not in the font) as a space
    jr @put
@s5:
    cp $DF
    jr c,@no
    cp $E9
    jr nc,@no
    sub $DF
    call NtDigit
    scf
    ret
@no:
    or a
    ret
MapAdvice:                       ; A = cave id $50-$AF: the person's text from row MapRow (up to its first command)
    ld e,a
    ld d,0
    ld hl,$8C8F
    add hl,de
    ld a,7
    call Stub_ReadB              ; the text number
    add a,a
    ld e,a
    ld d,0
    ld hl,$967C
    add hl,de
    ld a,7
    call Stub_ReadB
    ld e,a
    inc hl
    ld a,7
    call Stub_ReadB
    ld d,a
    ex de,hl
    inc hl                       ; 2 header bytes
    inc hl
    xor a
    ld (MapCol),a
    call @row
@ch:
    ld a,7
    call Stub_ReadB
    call TypoFix
    inc hl
    cp $FF
    jr z,@nl
    cp $F0
    ret nc
    call MapTextChar
    jr nc,@ch
    ld a,(MapCol)
    inc a
    ld (MapCol),a
    cp 29
    jr c,@ch
@nl:
    ld a,(MapRow)
    inc a
    ld (MapRow),a
    cp 22
    ret nc
    xor a
    ld (MapCol),a
    call @row
    jr @ch
@row:
    ld a,(MapRow)
    ld b,a
    ld c,2
    jp NtAt

MapClearNt:                      ; the whole name table: spaces (display off)
    ld hl,$3800
    di
    call SetVramAddr
    ld bc,32*24
@c:
    ld a,(NtTileOfs)
    add a,$40
    out ($BE),a
    xor a
    out ($BE),a
    dec bc
    ld a,b
    or c
    jr nz,@c
    ei
    ret
ClearRow:                        ; B = row: columns 1-30 spaces
    ld c,1
    call NtAt
    ld b,30
    jp NtSpaces

MapTiles:                        ; tiles MT0..: per colour (MapTileCols) L, L with each mark, R; then the cursor L, R
    ld hl,MT0*32
    di
    call SetVramAddr
    ld de,MapTileCols
@col:
    ld a,(de)
    or a
    jr z,@cursor
    ld (Tmp+10),a
    push de
    ld hl,ShapeL
    call @tile
    ld hl,ShapeLCave
    call @tile
    ld hl,ShapeLWinkle
    call @tile
    ld hl,ShapeLDungeon
    call @tile
    ld hl,ShapeR
    call @tile
    pop de
    inc de
    jr @col
@cursor:
    xor a
    ld (Tmp+10),a
    ld hl,ShapeCurL
    call @tile
    ld hl,ShapeCurR
    call @tile
    ei
    ret
@tile:                           ; HL -> 8 x (mask in the colour, mask in colour 15): 32 bytes
    ld b,8
@row:
    ld a,(Tmp+10)
    ld c,a
    ld d,(hl)
    inc hl
    ld e,(hl)
    inc hl
    push bc
    ld b,4
@plane:
    xor a
    rr c
    jr nc,@p0
    ld a,d
@p0:
    or e
    out ($BE),a
    djnz @plane
    pop bc
    djnz @row
    ret
MapTileCols: .DB 1, 2, 3, 4, 5, 6, 7, 8, 15, 0     ; area k = 0..7 -> colour k + 1; 8 = white (your screen)
ShapeL:      .DB $FF,0, $FF,0, $FF,0, $FF,0, $FF,0, $FF,0, $FF,0, 0,0
; the marks (upper left corner, 3 x 3; told apart by brightness and shape, not by hue)
ShapeLCave:  .DB $FF,0, $8F,$70, $8F,$70, $8F,$70, $FF,0, $FF,0, $FF,0, 0,0   ; white: a cave entered
ShapeLWinkle: .DB $FF,0, $8F,0, $8F,0, $8F,0, $FF,0, $FF,0, $FF,0, 0,0        ; black: Winkle's (the save)
ShapeLDungeon: .DB $FF,0, $8F,$70, $8F,$50, $8F,$70, $FF,0, $FF,0, $FF,0, 0,0 ; white, black centre: a dungeon
ShapeR:      .DB $FC,0, $FC,0, $FC,0, $FC,0, $FC,0, $FC,0, $FC,0, 0,0
ShapeCurL:   .DB 0,$FF, 0,$80, 0,$80, 0,$80, 0,$80, 0,$80, 0,$FF, 0,0
ShapeCurR:   .DB 0,$FC, 0,$04, 0,$04, 0,$04, 0,$04, 0,$04, 0,$FC, 0,0
MapPal:      .DB $00, $06, $1F, $26, $09, $34, $0C, $18, $15, $00, $00, $00, $00, $00, $00, $3F

MapSquare:                       ; A = screen: its square on the grid (2 tiles at row 3 + y, column 4 + 2 x)
    ld (MapS),a
    call DivMod13
    ld a,b
    add a,3
    ld b,a
    ld a,c
    add a,a
    add a,4
    ld c,a
    call NtAt
    ld a,(MapS)
    ld hl,MapCur
    cp (hl)
    jr nz,@nc
    ld a,(MapTick)
    and 16
    jr z,@nc
    ld a,MT_CUR
    call NtTile
    ld a,MT_CUR+1
    jp NtTile
@nc:
    ld a,(MapS)
    ld hl,MapVis
    call BitPtr
    and (hl)
    jr nz,@vis
    ld a,$40
    call NtChar
    ld a,$40
    jp NtChar
@vis:
    ld a,(MapS)
    ld hl,$C80D
    cp (hl)
    jr nz,@area
    ld a,(MapTick)
    and 8
    ld a,8
    jr nz,@k
@area:
    ld a,(MapS)
    call AreaOf
@k:
    ld e,a                       ; 5 tiles per colour: L, L + one of the 3 marks, R
    add a,a
    add a,a
    add a,e
    add a,MT0
    ld e,a
    ld a,(MapS)
    ld hl,MapEnt
    call BitPtr
    and (hl)
    ld a,e
    jr z,@l
    push de
    ld a,(MapS)
    call MarkKind
    pop de
    add a,e
@l:
    call NtTile
    ld a,e
    add a,4
    jp NtTile

MarkKind:                        ; A = screen (its cave entered) -> 0 none, 1 a cave, 2 Winkle's (the save), 3 a dungeon
    call CaveId
    or a
    ret z                        ; 0 = L14, the start: no entrance, no mark
    cp $10
    jr c,@dungeon
    cp $50
    jr nc,@cave
    rrca                         ; Randar, Enny, Winkle, Dina: $8CDA[id >> 4], Winkle = 3
    rrca
    rrca
    rrca
    and 15
    ld e,a
    ld d,0
    ld hl,$8CDA
    add hl,de
    ld a,7
    call Stub_ReadB
    cp 3
    jr nz,@cave
    ld a,2
    ret
@cave:
    ld a,1
    ret
@dungeon:
    ld a,3
    ret

MapCurInfo:                      ; rows 18-21: the cursor's screen
    ld b,18
    call ClearRow
    ld b,19
    call ClearRow
    ld b,20
    call ClearRow
    ld b,21
    call ClearRow
    ld bc,2+256*18
    call NtAt
    ld a,(MapCur)
    call PrintScreen
    ld hl,StrGap2
    call NtPrint
    ld a,(MapCur)
    ld hl,MapVis
    call BitPtr
    and (hl)
    jr nz,@vis
    ld hl,StrNotSeen
    jp NtPrint
@vis:
    ld a,(MapCur)
    call PrintArea
    ld a,20
    ld (MapRow),a
    ld a,(MapCur)
    jp CaveLines

MapGrid:                         ; the whole grid page (display off)
    call MapClearNt
    ld bc,11+256*1               ; (row 0 is partly hidden on the Pause screen)
    call NtAt
    ld hl,StrWorldMap
    call NtPrint
    ld d,13                      ; column letters
    ld c,4
    ld e,'A'
@let:
    ld b,2
    call NtAt
    ld a,e
    call NtChar
    inc e
    inc c
    inc c
    dec d
    jr nz,@let
    ld d,0                       ; row numbers
@num:
    ld a,d
    add a,3
    ld b,a
    ld c,2                       ; columns 2-3: right next to the squares (column 4 on)
    call NtAt
    ld a,d
    inc a
    cp 10
    jr c,@n1
    sub 10
    push af
    ld a,1
    call NtDigit
    pop af
    jr @n2
@n1:
    push af
    ld a,$40
    call NtChar
    pop af
@n2:
    call NtDigit
    inc d
    ld a,d
    cp 14
    jr nz,@num
    xor a
@sq:
    push af
    call MapSquare
    pop af
    inc a
    cp 182
    jr nz,@sq
    call MapCurInfo
    ld hl,StrMapHelp
    ld (HelpText),hl
    jp DrawHelp

MapStep:                         ; A = screen, E = direction bits (up, down, left, right) -> A = the neighbour; carry = none
    ld d,a
    call DivMod13
    bit 0,e
    jr z,@nu
    ld a,b
    or a
    scf
    ret z
    ld a,d
    sub 13
    or a
    ret
@nu:
    bit 1,e
    jr z,@nd
    ld a,b
    cp 13
    scf
    ret z
    ld a,d
    add a,13
    or a
    ret
@nd:
    bit 2,e
    jr z,@nl
    ld a,c
    or a
    scf
    ret z
    ld a,d
    dec a
    or a
    ret
@nl:
    bit 3,e
    scf
    ret z
    ld a,c
    cp 12
    scf
    ret z
    ld a,d
    inc a
    or a
    ret
MapNext:                         ; E = direction: MapCur = the next seen screen that way; carry = none
    ld a,(MapCur)
@step:
    call MapStep
    ret c
    ld (MapTry),a
    ld hl,MapVis
    call BitPtr
    and (hl)
    ld a,(MapTry)
    jr z,@step
    ld (MapCur),a
    or a
    ret
MapDir:                          ; -> A = the direction bits to act on this frame (a press, or held: repeat)
    ld a,(PadNew)
    and $0F
    jr z,@held
    ld b,a
    xor a
    ld (MapHold),a
    ld a,b
    ret
@held:
    ld a,($C806)
    and $0F
    jr nz,@h
    ld (MapHold),a
    ret
@h:
    ld b,a
    ld hl,MapHold
    ld a,(hl)
    cp 200
    jr nc,@rep
    inc (hl)
@rep:
    ld a,(hl)
    cp 16
    jr c,@no
    and 3
    jr nz,@no
    ld a,b
    ret
@no:
    xor a
    ret

PauseMap:                        ; from the Pause (status) screen, button 2
    call ScreenOff
    call GameNt
    ld a,$C0                     ; the font as for SAVE GAME: characters $40-$6F -> tiles $00-$2F,
    ld (NtTileOfs),a             ; digits -> $30-$39 (inside the status tiles, reloaded afterwards)
    ld a,$30
    ld (NtDigit0),a
    ld hl,0
    ld (UpLo),hl
    ld hl,48*8
    ld (UpCnt),hl
    ld hl,$0000
    ld (UpDst),hl
    call UnpackFont
    ld hl,($DF-$40)*8
    ld (UpLo),hl
    ld hl,80
    ld (UpCnt),hl
    ld hl,$0600
    ld (UpDst),hl
    call UnpackFont
    ld hl,(ColonTile-$40)*32
    call ColonUp
    call MapTiles
    ld hl,MapPal                 ; the game's: 16 colours -> $C060, uploaded at the next frame
    call $0490
    ld a,(GfxId)                 ; the overworld graphics in VRAM now (the pictures may load others)
    call AreaCode
    ld (PicCode),a
    ld a,($C84C)
    ld (PicFlag),a
    ld a,($C80D)
    ld (MapCur),a
    xor a
    ld (MapHold),a
    ld a,16
    ld (MapTick),a
@full:
    call MapGrid
    call ScreenOn
@loop:
    call WaitPaused
    ld a,($C005)                 ; Pause: back to the game (as from the status screen)
    inc a
    jp z,@unpause
    ld hl,MapTick
    inc (hl)
    ld a,(hl)
    and 7
    jr nz,@keys
    ld a,($C80D)                 ; blinking: your screen and the cursor
    call MapSquare
    ld a,(MapCur)
    call MapSquare
@keys:
    call ReadPad
    ld a,(PadNew)
    ld b,a
    and $20
    jr nz,@back
    bit 4,b
    jr nz,@open
    call MapDir
    or a
    jr z,@loop
    ld e,a
    ld a,(MapCur)
    ld (MapTry),a
    call MapStep
    jr c,@loop
    ld (MapCur),a
    ld a,16
    ld (MapTick),a
    ld a,(MapTry)
    call MapSquare
    ld a,(MapCur)
    call MapSquare
    call MapCurInfo
    jr @loop
@open:
    ld a,(MapCur)
    ld hl,MapVis
    call BitPtr
    and (hl)
    jr z,@loop
    call MapInfo
    jr c,@unpause
    call ScreenOff
    call MapTiles                ; (the picture used their tiles and its own palette)
    ld hl,MapPal
    call $0490
    xor a
    ld (NtAttr),a
    ld a,16
    ld (MapTick),a
    jp @full
@back:
    call ScreenOff
    call Stub_Unpause72          ; as after SAVE GAME: the overworld's name table back, then the status screen
    jp $291F
@unpause:                        ; $C005 = $FF: the pause loop's own end ($2945 tests it, $294B unpauses:
    ld a,$FF                     ; sound, asset $72 + $B0, the overworld palette, the screen redrawn); set
    ld ($C005),a                 ; again: a page's frame waits since the press ($07C3) cleared it
    jp $2945

MapInfo:                         ; the cursor's screen page; arrows = the next seen screen that way, 2 = back,
                                 ; Pause = carry (back to the game)
    call ScreenOff
    call MapClearNt
    ld a,(MapCur)
    call PicDraw
    ld a,$08                     ; the text: sprite palette (its colour 15 is white; the picture has its
    ld (NtAttr),a                ; area's colours)
    ld bc,2+256*13
    call NtAt
    ld a,(MapCur)
    call PrintScreen
    ld hl,StrGap2
    call NtPrint
    ld a,(MapCur)
    call PrintArea
    ld a,14
    ld (MapRow),a
    ld a,(MapCur)
    call CaveLines
    ld a,(MapCid)
    cp $50
    jr c,@help
    cp $B0
    jr nc,@help
    ld a,(MapCur)
    ld hl,MapEnt
    call BitPtr
    and (hl)
    jr z,@help
    ld a,17
    ld (MapRow),a
    ld a,(MapCid)
    call MapAdvice
@help:
    ld hl,StrMapInfoHelp
    ld (HelpText),hl
    call DrawHelp
    call ScreenOn
@loop:
    call WaitPaused
    ld a,($C005)                 ; Pause: carry (back to the game)
    inc a
    jr z,@pause
    call ReadPad
    ld a,(PadNew)
    ld b,a
    and $20
    jr nz,@back
    ld a,b
    and $0F
    jr z,@loop
    ld e,a
    call MapNext
    jr c,@loop
    jp MapInfo
@pause:
    call PicRestore
    call ScreenOn                ; (the unpause expects the display on, as on the status screen)
    scf
    ret
@back:
    call PicRestore
    or a                         ; (carry clear)
    ret

; ---- the picture: the screen at half size, made from the ROM's own data (nothing of it is stored in the
; patch). Layout: room table bank 5 $9890 -> record $9947 + 7 n (0 layout id, 2 entrance cell, 6 entrance
; metatile), layout = bank 3 $8000 + 180 id (id < $5B) or bank 4 $8000 + 180 (id - $5B), 15 x 12 metatiles.
; Metatile m = 4 tile numbers (tile 256 + n, palette 0): m < $58 asset $84 (+ asset $8E for m $36-$3B and
; $45-$4C when $C84C: layout id >= $53), m >= $58 asset $1ABB[area]. Tiles: asset $D0 -> $2000 (+ asset $8D
; when $C84C), asset $1A77[area] + 1 -> $2E00; palette = asset $1A77[area]. Area = bank 5 $9E48[screen] & 15,
; 0 = the previous screen's ($1A3D). Those tiles are loaded into VRAM $2000-$37FF (the Pause screen leaves
; them alone) when another area's are there, read back, every other pixel of a metatile's 16 x 16 kept ->
; one low tile ($3A..), and the game's set is put back when the page closes (PicRestore). The hero's own
; screen comes from RAM as it is now ($CA00, $CC00, palette $C84D). An entrance is drawn only for caves
; you have been inside (MapEnt), MAP FIXES as at $193D.
PicDraw:                         ; A = screen (display off; the name table cleared)
    ld (PicS),a
    xor a
    ld (NtAttr),a
    ld (PicN),a
    ld a,(PicS)
    ld hl,$C80D
    cp (hl)
    jr nz,@other
    ld a,1
    ld (PicLive),a
    ld a,(GfxId)
    call AreaCode
    ld b,a
    ld a,($C84C)
    ld c,a
    call PicGfx
    ld hl,$C84D                  ; the overworld's palette (kept there during the pause)
    call $0490
    jr @cells
@other:
    xor a
    ld (PicLive),a
    call PicRoom
    ld a,(PicArea)
    call AreaCode
    push af
    ld b,a
    ld a,(PicTFlag)
    ld c,a
    call PicGfx
    pop af
    call AssetAddr               ; its palette -> Tmp -> $C060
    ld de,Tmp
    ld b,16
@pal:
    ld a,c
    call Stub_ReadB
    ld (de),a
    inc hl
    inc de
    djnz @pal
    ld hl,Tmp
    call $0490
@cells:
    xor a
    ld (PicCell),a
    ld b,PIC_ROW
@row:
    push bc
    ld c,PIC_COL
    call NtAt
    ld b,15
@col:
    push bc
    call PicCellM
    call PicTile
    call NtTile
    ld hl,PicCell
    inc (hl)
    pop bc
    djnz @col
    pop bc
    inc b
    ld a,b
    cp PIC_ROW+12
    jr nz,@row
    ret

PicRestore:                      ; the game's own overworld graphics back into VRAM (display off first)
    call ScreenOff
    ld a,(GfxId)
    call AreaCode
    ld b,a
    ld a,($C84C)
    ld c,a
PicGfx:                          ; B = area palette asset (tiles = B + 1), C = $C84C: make VRAM hold them
    ld a,(PicFlag)
    cp c
    jr z,@area
    ld a,c
    ld (PicFlag),a
    push bc
    ld a,$D0
    ld de,$2000
    call Stub_Unpack
    pop bc
    ld a,c
    or a
    push bc
    call nz,PicPatch
    pop bc
    ld a,$FF                     ; (the area's tiles again too: the common set may reach into them)
    ld (PicCode),a
@area:
    ld a,(PicCode)
    cp b
    ret z
    ld a,b
    ld (PicCode),a
    inc a
    ld de,$2E00
    jp Stub_Unpack

PicPatch:                        ; $C84C: asset $8D -> VRAM $2780 ($60), $28E0 ($100), $2AE0 ($80), as $1C05
    ld a,$8D
    call AssetAddr
    ld de,$2780
    ld b,$60
    call @seg
    ld de,$28E0
    ld b,0
    call @seg
    ld de,$2AE0
    ld b,$80
@seg:
    ex de,hl
    di
    call SetVramAddr
    ex de,hl
@byte:
    ld a,c
    call Stub_ReadB
    out ($BE),a
    inc hl
    djnz @byte
    ei
    ret

AreaCode:                        ; A = area id -> A = its palette asset ($1A77, bank 0; the tiles are + 1)
    ld e,a
    ld d,0
    ld hl,$1A77
    add hl,de
    ld a,(hl)
    ret

AssetAddr:                       ; A = asset code -> C = its bank, HL = its data (the asset loader's table)
    ld b,a
    rrca
    rrca
    rrca
    rrca
    and $0F
    ld c,a
    ld a,b
    and $0F
    add a,a
    ld l,a
    ld h,$80
    ld a,c
    call Stub_ReadB
    ld e,a
    inc hl
    ld a,c
    call Stub_ReadB
    ld d,a
    ex de,hl
    ret

PicRoom:                         ; PicS (another screen): layout, entrance, area, $C84C
    ld a,(PicS)
    ld e,a
    ld d,0
    ld hl,$9890
    add hl,de
    ld a,5
    call Stub_ReadB              ; its room
    ld l,a
    ld h,0
    ld b,h
    ld c,l
    add hl,hl
    add hl,bc
    add hl,hl
    add hl,bc                    ; * 7
    ld bc,$9947
    add hl,bc
    push hl
    ld a,5
    call Stub_ReadB              ; layout id
    ld c,a
    ld a,(OptMap)                ; MAP FIXES: C5 (screen 54) its own layout
    or a
    jr z,@nofix1
    ld a,(PicS)
    cp 54
    jr nz,@nofix1
    ld a,c
    cp $8E
    jr nz,@nofix1
    ld c,$7E
@nofix1:
    ld a,c
    cp $53
    ccf
    ld a,0
    rla
    ld (PicTFlag),a              ; layout id >= $53: the $C84C set
    ld a,c
    ld b,3
    cp $5B
    jr c,@bank
    sub $5B
    inc b
@bank:
    ld e,a                       ; address = $8000 + 180 * id
    ld d,0
    ld l,e
    ld h,d
    add hl,hl
    add hl,hl                    ; 4
    ld d,h
    ld e,l
    add hl,hl
    add hl,hl                    ; 16
    ex de,hl
    add hl,de                    ; 20
    ex de,hl
    add hl,hl                    ; 32
    ex de,hl
    add hl,de                    ; 52
    ex de,hl
    add hl,hl
    add hl,hl                    ; 128
    add hl,de                    ; 180
    ld de,$8000
    add hl,de
    ld (PicLayP),hl
    ld a,b
    ld (PicLayB),a
    pop hl
    inc hl
    inc hl
    ld a,5
    call Stub_ReadB              ; entrance cell
    ld (PicEnt),a
    ld de,4
    add hl,de
    ld a,5
    call Stub_ReadB              ; entrance metatile
    ld c,a
    ld a,(OptMap)                ; MAP FIXES: H4 (screen 46) the cave picture $2B
    or a
    jr z,@nofix2
    ld a,(PicS)
    cp 46
    jr nz,@nofix2
    ld a,c
    cp $94
    jr nz,@nofix2
    ld c,$2B
@nofix2:
    ld a,c
    ld (PicEntM),a
    or a
    jr z,@noent
    ld a,(PicS)                  ; only caves you have been inside
    ld hl,MapEnt
    call BitPtr
    and (hl)
    jr nz,@area
@noent:
    ld a,$FF
    ld (PicEnt),a
@area:
    ld a,(PicS)                  ; area id, 0 = the previous screen's
    ld e,a
    ld d,0
    ld hl,$9E48
    add hl,de
    ld a,5
    call Stub_ReadB
    and $0F
    jr nz,@got
    dec hl
    ld a,5
    call Stub_ReadB
    and $0F
@got:
    ld (PicArea),a
    ret

PicCellM:                        ; A = the metatile of cell PicCell
    ld a,(PicCell)
    ld e,a
    ld d,0
    ld a,(PicLive)
    or a
    jr z,@rom
    ld hl,$CA00
    add hl,de
    ld a,(hl)
    ret
@rom:
    ld a,(PicEnt)
    cp e
    jr nz,@lay
    ld a,(PicEntM)
    ret
@lay:
    ld hl,(PicLayP)
    add hl,de
    ld a,(PicLayB)
    jp Stub_ReadB

PicTile:                         ; A = metatile -> A = its output tile (made the first time)
    ld (PicM),a
    ld hl,PicList
    ld a,(PicN)
    or a
    jr z,@new
    ld b,a
    ld c,0
@find:
    ld a,(PicM)
    cp (hl)
    jr z,@found
    inc hl
    inc c
    djnz @find
@new:
    ld a,(PicN)
    cp 65
    jr nc,@full
    ld c,a
    inc a
    ld (PicN),a
    ld a,(PicM)
    ld (hl),a
    push bc
    call PicMeta
    pop bc
    ld a,c
    add a,PicT0
    push af
    call PicShrink
    pop af
    ret
@found:
    ld a,c
    add a,PicT0
    ret
@full:
    ld a,PicT0
    ret

PicMeta:                         ; PicM -> Meta: its 4 tile numbers
    ld a,(PicM)
    ld e,a
    ld a,(PicLive)
    or a
    jr z,@rom
    ld l,e
    ld h,0
    add hl,hl
    add hl,hl
    ld bc,$CC00
    add hl,bc
    ld de,Meta
    ld bc,4
    ldir
    ret
@rom:
    ld a,e
    cp $58
    jr c,@common
    sub $58
    ld l,a
    ld h,0
    add hl,hl
    add hl,hl
    push hl
    ld a,(PicArea)
    ld c,a
    ld b,0
    ld hl,$1ABB
    add hl,bc
    ld a,(hl)
    jr @asset
@common:
    ld a,(PicTFlag)
    or a
    jr z,@base
    ld a,e
    sub $36
    cp 6
    jr c,@patch
    ld a,e
    sub $45
    cp 8
    jr nc,@base
    add a,6
@patch:
    ld l,a
    ld h,0
    add hl,hl
    add hl,hl
    push hl
    ld a,$8E
    jr @asset
@base:
    ld l,e
    ld h,0
    add hl,hl
    add hl,hl
    push hl
    ld a,$84
@asset:
    call AssetAddr
    pop de
    add hl,de
    ld de,Meta
    ld b,4
@copy:
    ld a,c
    call Stub_ReadB
    ld (de),a
    inc hl
    inc de
    djnz @copy
    ret

PicShrink:                       ; A = output tile: Meta's 16 x 16 pixels, every other one, -> that low tile
    ld (PicT),a
    xor a
    ld (PicH),a
@half:
    xor a
    ld (PicR),a
    ld de,Tmp
@row:
    push de
    ld a,(PicH)
    add a,a
    call @meta
    call @src
    ld de,PicA
    call @read4
    ld a,(PicH)
    add a,a
    inc a
    call @meta
    call @src
    ld de,PicB
    call @read4
    pop de
    ld hl,PicA
    ld b,4
@plane:
    ld a,(hl)                    ; left tile: pixels 0 2 4 6 -> 0-3, right tile -> 4-7
    call Compact
    add a,a
    add a,a
    add a,a
    add a,a
    ld c,a
    push hl
    inc hl
    inc hl
    inc hl
    inc hl
    ld a,(hl)
    pop hl
    call Compact
    or c
    ld (de),a
    inc de
    inc hl
    djnz @plane
    ld a,(PicR)
    inc a
    ld (PicR),a
    cp 4
    jr nz,@row
    ld a,(PicT)                  ; Tmp = 4 rows -> the tile's first or second half
    ld l,a
    ld h,0
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    ld a,(PicH)
    or a
    jr z,@put
    ld bc,16
    add hl,bc
@put:
    di
    call SetVramAddr
    ld hl,Tmp
    ld b,16
@out:
    ld a,(hl)
    out ($BE),a
    inc hl
    djnz @out
    ei
    ld a,(PicH)
    inc a
    ld (PicH),a
    cp 2
    jp nz,@half
    ret
@meta:                           ; A = 0-3 -> A = Meta[A]
    ld e,a
    ld d,0
    ld hl,Meta
    add hl,de
    ld a,(hl)
    ret
@src:                            ; A = tile n -> HL = VRAM $2000 + 32 n + 8 PicR (source row 2 PicR)
    ld l,a
    ld h,0
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    ld a,(PicR)
    add a,a
    add a,a
    add a,a
    ld e,a
    ld d,$20
    add hl,de
    ret
@read4:                          ; HL = VRAM -> DE.. 4 bytes
    di
    ld a,l
    out ($BF),a
    ld a,h
    out ($BF),a                  ; read address (bit 6 clear)
    ld b,4
@in:
    call VdpDelay
    in a,($BE)
    ld (de),a
    inc de
    djnz @in
    ei
    ret

Compact:                         ; A = 8 pixels of a plane -> A = pixels 0 2 4 6 (bits 7 5 3 1) as bits 3-0
    push hl
    push de
    ld e,a
    ld d,0
    ld hl,CompactTab
    add hl,de
    ld a,(hl)
    pop de
    pop hl
    ret
CompactTab:
    .DB $0, $0, $1, $1, $0, $0, $1, $1, $2, $2, $3, $3, $2, $2, $3, $3
    .DB $0, $0, $1, $1, $0, $0, $1, $1, $2, $2, $3, $3, $2, $2, $3, $3
    .DB $4, $4, $5, $5, $4, $4, $5, $5, $6, $6, $7, $7, $6, $6, $7, $7
    .DB $4, $4, $5, $5, $4, $4, $5, $5, $6, $6, $7, $7, $6, $6, $7, $7
    .DB $0, $0, $1, $1, $0, $0, $1, $1, $2, $2, $3, $3, $2, $2, $3, $3
    .DB $0, $0, $1, $1, $0, $0, $1, $1, $2, $2, $3, $3, $2, $2, $3, $3
    .DB $4, $4, $5, $5, $4, $4, $5, $5, $6, $6, $7, $7, $6, $6, $7, $7
    .DB $4, $4, $5, $5, $4, $4, $5, $5, $6, $6, $7, $7, $6, $6, $7, $7
    .DB $8, $8, $9, $9, $8, $8, $9, $9, $A, $A, $B, $B, $A, $A, $B, $B
    .DB $8, $8, $9, $9, $8, $8, $9, $9, $A, $A, $B, $B, $A, $A, $B, $B
    .DB $C, $C, $D, $D, $C, $C, $D, $D, $E, $E, $F, $F, $E, $E, $F, $F
    .DB $C, $C, $D, $D, $C, $C, $D, $D, $E, $E, $F, $F, $E, $E, $F, $F
    .DB $8, $8, $9, $9, $8, $8, $9, $9, $A, $A, $B, $B, $A, $A, $B, $B
    .DB $8, $8, $9, $9, $8, $8, $9, $9, $A, $A, $B, $B, $A, $A, $B, $B
    .DB $C, $C, $D, $D, $C, $C, $D, $D, $E, $E, $F, $F, $E, $E, $F, $F
    .DB $C, $C, $D, $D, $C, $C, $D, $D, $E, $E, $F, $F, $E, $E, $F, $F

; $293F: the overworld's status screen is drawn, the display still off: "1 SAVE" centred under the
; left half of the frame (x 16-127), "2 MAP" under the right half (x 128-239), lines 180-186 = in the
; middle of the black border (lines 175-191). Placed to the pixel: the font characters A..V and the
; digits 1, 2 are unpacked to tiles $74-$8B, every character of both texts is read back into HintGlyphs
; (4 planes ORed), then each column of a text is put together in Tmp (16 lines = its tiles in rows 22
; and 23) and written to tiles $74.. (2 per column, 24 in all). The status screen uses tiles $00-$73;
; unpausing reloads $0000-$13BF (asset $B0).
PauseHintH:
    call GameNt
    ld hl,8                      ; font characters $41-$56 (A..V) -> tiles $74-$89
    ld (UpLo),hl
    ld hl,22*8
    ld (UpCnt),hl
    ld hl,$74*32
    ld (UpDst),hl
    call UnpackFont
    ld hl,($E0-$40)*8            ; the digits 1 and 2 (font tiles $E0, $E1) -> tiles $8A, $8B
    ld (UpLo),hl
    ld hl,16
    ld (UpCnt),hl
    ld hl,$8A*32
    ld (UpDst),hl
    call UnpackFont
    ld de,HintGlyphs             ; the glyphs of both texts first: their tiles are reused below
    ld hl,StrHintL+1
    call HintRead
    ld hl,StrHintR+1
    call HintRead
    ld hl,HintGlyphs
    ld (HintGP),hl
    ld a,$74
    ld (HintTile),a
    ld hl,StrHintL
    call HintText
    ld hl,StrHintR
    call HintText
    jp ScreenOn

HintRead:                        ; HL -> characters, 0: their glyphs to DE.. (8 bytes each; a space = 0)
    ld a,(hl)
    inc hl
    or a
    ret z
    push hl
    cp $40
    jr z,@space
    cp ColonTile
    jr z,@colon
    cp $B0
    jr c,@letter
    add a,$8A-$B1                ; '1', '2' -> tiles $8A, $8B
    jr @tile
@letter:
    add a,$74-$41                ; 'A'..'V' -> tiles $74..$89
@tile:
    ld l,a
    ld h,0
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    di
    ld a,l
    out ($BF),a
    ld a,h
    out ($BF),a                  ; VRAM read address (bit 6 clear)
    ld b,8
@line:
    ld c,0
    call @plane
    call @plane
    call @plane
    call @plane
    ld a,c
    ld (de),a
    inc de
    djnz @line
    ei
    pop hl
    jr HintRead
@plane:
    call VdpDelay
    in a,($BE)
    or c
    ld c,a
    ret
@space:
    xor a
    ld b,8
@zero:
    ld (de),a
    inc de
    djnz @zero
    pop hl
    jr HintRead
@colon:
    ld hl,ColonBits
    ld bc,8
    ldir
    pop hl
    jr HintRead

ColonUp:                         ; HL = VRAM address: our ':'
    ld de,ColonBits
GlyphUp:                         ; HL = VRAM address, DE -> 8 lines: one tile in colour 15 (every plane, as the font)
    di
    call SetVramAddr
    ld b,8
@line:
    ld a,(de)
    out ($BE),a
    call VdpDelay
    out ($BE),a
    call VdpDelay
    out ($BE),a
    call VdpDelay
    out ($BE),a
    inc de
    djnz @line
    ei
    ret

HintText:                        ; HL -> x, characters, 0 (glyphs from HintGP, which advances; tiles from HintTile)
    ld a,(hl)
    inc hl
    ld c,a
    and 7
    ld (HintSh),a                ; the pixel offset inside a column
    ld b,a
    ld a,$FF
    inc b
    jr @mgo
@mask:
    srl a
@mgo:
    djnz @mask
    ld (HintMask),a              ; $FF >> offset: the bits of a glyph that stay in its own column
    ld a,c
    rrca
    rrca
    rrca
    and 31
    ld (HintCol),a
    ld b,0
@count:
    ld a,(hl)
    or a
    jr z,@counted
    inc b
    inc hl
    jr @count
@counted:
    ld a,b
    ld (HintN),a
    ld a,(HintSh)
    or a
    jr z,@cols
    inc b                        ; a shifted text reaches into one more column
@cols:
    ld c,0                       ; C = column of the text, B = columns left
@col:
    push bc
    ld hl,Tmp
    ld b,16
    xor a
@clear:
    ld (hl),a
    inc hl
    djnz @clear
    pop bc
    push bc
    ld a,(HintN)
    ld b,a
    ld a,c
    cp b
    jr nc,@noleft
    call @glyph                  ; character C: its left part
    ld a,(HintMask)
    ld c,a
    call HintPart
@noleft:
    pop bc
    push bc
    ld a,(HintSh)
    or a
    jr z,@noright
    ld a,c
    or a
    jr z,@noright
    dec a                        ; character C-1: the part that spills over
    call @glyph
    ld a,(HintMask)
    cpl
    ld c,a
    call HintPart
@noright:
    ld a,(HintTile)              ; the two tiles (rows 22, 23) follow each other in VRAM
    ld l,a
    ld h,0
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    di
    call SetVramAddr
    ld hl,Tmp
    ld b,16
@out:
    ld a,(hl)
    out ($BE),a
    call VdpDelay
    out ($BE),a
    call VdpDelay
    out ($BE),a
    call VdpDelay
    out ($BE),a
    inc hl
    djnz @out
    ei
    pop bc
    push bc
    ld a,(HintCol)
    add a,c
    ld c,a
    ld b,22
    call NtAt
    ld a,(HintTile)
    call NtTile
    ld b,23
    call NtAt
    ld a,(HintTile)
    inc a
    call NtTile
    ld a,(HintTile)
    add a,2
    ld (HintTile),a
    pop bc
    inc c
    dec b
    jp nz,@col
    ld a,(HintN)                 ; the next text's glyphs
    call @glyph
    ld (HintGP),hl
    ret
@glyph:                          ; A = character number -> HL = HintGP + 8 A
    add a,a
    add a,a
    add a,a
    ld hl,(HintGP)
    add a,l
    ld l,a
    ret nc
    inc h
    ret

HintPart:                        ; HL -> glyph: each line rotated right by HintSh, AND C, ORed into Tmp + HINT_LINE
    ld de,Tmp+HINT_LINE
    ld b,8
@line:
    push bc
    ld a,(HintSh)
    ld b,a
    ld a,(hl)
    inc b
    jr @go
@rot:
    rrca
@go:
    djnz @rot
    pop bc
    and c
    ex de,hl
    or (hl)
    ld (hl),a
    ex de,hl
    inc hl
    inc de
    djnz @line
    ret

; --- OPTIONS: battery RAM $8010-$8017 = OptVals, $8018 = check byte ---
OptDefaults: .DB 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0     ; the original game
OptCount:    .DB 2, 2, 2, 3, 3, 2, 11, 11, 11, 3, 3  ; number of values
OptKinds:    .DB 0, 0, 0, 1, 1, 0, 2, 2, 2, 1, 1     ; 0 OFF / ON, 1 a name from OptNames, 2 0X..10X
OptNames:    .DW 0, 0, 0, ReachNames, SpeedNames, 0, 0, 0, 0, SpeedNames, EasyNames
OptPages:    .DW OptPage0, OptPage1               ; the options on each page (storage numbers), $FF = end
OptPage0:    .DB 0, 1, 2, 3, 10, 4, 9, 5, $FF     ; fixes and helpers
OptPage1:    .DB 6, 7, 8, $FF                     ; multipliers

OptSum:                          ; HL -> OPT_N bytes: A = their check byte
    ld b,OPT_N
OptSumB:                         ; (B bytes)
    xor a
@add:
    add a,(hl)
    inc hl
    djnz @add
    xor $5A
    ret

OptLoad:                         ; battery RAM -> OptVals; missing, damaged or out of range = the defaults
    ld a,1
    ld (OptLoaded),a
    ld hl,$8010
    ld de,Tmp
    ld bc,OPT_N+1
    call Stub_Sram
    ld c,OPT_N                   ; OPT_N options + check byte, else an older block (10 = before EASY CAVE
@try:                            ; ENTRY, 9 = before OVERWORLD SPEED)
    ld hl,Tmp
    ld b,c
    call OptSumB
    ld e,c
    ld d,0
    ld hl,Tmp
    add hl,de
    cp (hl)
    jr z,@found
    dec c
    ld a,c
    cp OPT_N-2
    jr nc,@try
    jr @def
@found:                          ; the options newer than the block: their defaults
    ld a,c
    cp OPT_N
    jr nc,@valid
    ld e,c
    ld d,0
    ld hl,OptDefaults
    add hl,de
    ld a,(hl)
    ld hl,Tmp
    add hl,de
    ld (hl),a
    inc c
    jr @found
@valid:
    ld hl,Tmp
    ld de,OptCount
    ld bc,OptDefaults
@range:                          ; a value out of range: that option's default
    ld a,(de)
    dec a                        ; the highest index
    cp (hl)
    jr nc,@inrange
    ld a,(bc)
    ld (hl),a
@inrange:
    inc hl
    inc de
    inc bc
    ld a,l
    cp (Tmp+OPT_N)&$FF
    jr nz,@range
    ld hl,Tmp
    jr OptCopy
@def:
OptReset:
    ld hl,OptDefaults
OptCopy:
    ld de,OptVals
    ld bc,OPT_N
    ldir
    ret

OptSave:                         ; OptVals -> battery RAM (with our header when the battery RAM has none yet)
    call SramPrepare
    ld hl,OptVals
    ld de,Tmp
    ld bc,OPT_N
    ldir
    ld hl,Tmp
    call OptSum
    ld (Tmp+OPT_N),a
    ld hl,Tmp
    ld de,$8010
    ld bc,OPT_N+1
    jp Stub_Sram

; the page: title row 0, page option n on row 2 + 2n (label column 3, value column 22, arrow column 1, '*' at
; column 30 when the value is the original game's), the PAGE row on row 20, help on row 22, the '*' legend
; on row 23. Up / down = row, left = previous value, right / button 1 = next value (on the PAGE row: the
; other page), button 2 = save and back.
OptionsPage:
    call PageOpen
    ld hl,$2000+StarTile*32      ; our '*' (colour 15 = every plane, as the font)
    ld de,StarBits
    call GlyphUp
    ld b,23
    ld hl,StrOptLegend
    call NtCentre
    xor a
    ld (OptCur),a
    ld (OptPage),a
    ld bc,12+256*0
    call NtAt
    ld hl,StrOptTitle
    call NtPrint
    ld hl,StrOptHelp
    ld (HelpText),hl
    call DrawHelp
    call OptDrawPage
    call PageShow
@loop:
    call WaitFrame
    call ReadPad
    ld a,(PadNew)
    ld b,a
    and $20
    jp nz,@back
    ld a,(OptCur)
    bit 0,b
    jr z,@notup
    or a                         ; up: first option -> PAGE row -> last option
    jr nz,@up1
    ld a,OPT_PAGEPOS
    jr @move
@up1:
    cp OPT_PAGEPOS
    jr nz,@up2
    call OptRows
@up2:
    dec a
    jr @move
@notup:
    bit 1,b
    jr z,@notdown
    cp OPT_PAGEPOS               ; down: last option -> PAGE row -> first option
    jr nz,@dn1
    xor a
    jr @move
@dn1:
    inc a
    ld c,a
    call OptRows
    cp c
    ld a,c
    jr nz,@move
    ld a,OPT_PAGEPOS
@move:
    ld (OptCur),a
    call OptCursors
    jp @loop
@notdown:
    ld c,a                       ; the position
    ld a,b
    and $1C                      ; left, right, button 1
    jp z,@loop
    ld a,c
    cp OPT_PAGEPOS
    jr nz,@value
    ld a,(OptPage)               ; the PAGE row: the other page
    xor 1
    ld (OptPage),a
    call OptDrawPage
    jp @loop
@value:
    call OptIndex
    ld c,a                       ; the option
    ld e,c
    ld d,0
    ld hl,OptCount
    add hl,de
    ld a,(hl)
    ld (Tmp+14),a                ; number of values
    ld hl,OptVals
    add hl,de
    ld a,(hl)
    bit 2,b
    jr z,@next
    dec a                        ; left
    jp p,@set
    ld a,(Tmp+14)
    dec a
    jr @set
@next:
    inc a
    push hl
    ld hl,Tmp+14
    cp (hl)
    pop hl
    jr c,@set
    xor a
@set:
    ld (hl),a
    ld a,(OptCur)
    call OptDrawRow
    jp @loop
@back:
    call OptSave
    jp PageClose

OptList:                         ; HL -> the current page's option list (DE changes)
    ld a,(OptPage)
    add a,a
    ld e,a
    ld d,0
    ld hl,OptPages
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ret
OptRows:                         ; A = the number of options on this page (BC, DE, HL kept)
    push bc
    push de
    push hl
    call OptList
    ld b,0
@count:
    ld a,(hl)
    inc a
    jr z,@got
    inc b
    inc hl
    jr @count
@got:
    ld a,b
    pop hl
    pop de
    pop bc
    ret
OptIndex:                        ; A = position on the page -> A = the option (BC, DE, HL kept)
    push de
    push hl
    push af
    call OptList
    pop af
    ld e,a
    ld d,0
    add hl,de
    ld a,(hl)
    pop hl
    pop de
    ret

OptDrawPage:                     ; rows 2-20: this page's options, the PAGE row, the cursor
    ld a,OPT_PAGEPOS
@clear:
    push af
    add a,a
    add a,2
    ld b,a
    ld c,3
    call NtAt
    ld b,28
    call NtSpaces
    pop af
    dec a
    jp p,@clear
    call OptRows
@rows:
    or a
    jr z,@page
    dec a
    push af
    call OptDrawRow
    pop af
    jr @rows
@page:
    ld bc,3+256*20
    call NtAt
    ld hl,StrPage
    call NtPrint
    ld bc,22+256*20
    call NtAt
    ld a,(OptPage)
    inc a
    call NtDigit
    ld a,$6E                     ; '/' (MAP "/": a quoted '/' would be ASCII $2F)
    call NtChar
    ld a,2
    call NtDigit
OptCursors:                      ; the arrow at the row under the cursor, spaces elsewhere
    ld a,OPT_PAGEPOS
@each:
    push af
    add a,a
    add a,2
    ld b,a
    ld c,1
    call NtAt
    pop af
    push af
    ld hl,OptCur
    cp (hl)
    ld a,$40
    jr nz,@put
    ld a,$61
@put:
    call NtChar
    pop af
    dec a
    jp p,@each
    ret

OptDrawRow:                      ; A = position on the page: its option's label, value and '*' when it is the original
    ld (Tmp+13),a
    call OptIndex
    ld (Tmp+15),a
    ld a,(Tmp+13)
    add a,a
    add a,2
    ld b,a
    ld c,3
    call NtAt
    ld a,(Tmp+15)
    add a,a
    ld e,a
    ld d,0
    ld hl,OptLabels
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    call NtPrint
    ld a,(Tmp+13)
    add a,a
    add a,2
    ld b,a
    ld c,22
    call NtAt
    push bc
    ld b,8
    call NtSpaces
    ld a,(Tmp+15)                ; column 30: '*' = the original game's value
    ld e,a
    ld d,0
    ld hl,OptDefaults
    add hl,de
    ld a,(hl)
    ld hl,OptVals
    add hl,de
    cp (hl)
    ld a,$40
    jr nz,@mark
    ld a,StarTile
@mark:
    call NtTile
    pop bc
    call NtAt
    ld a,(Tmp+15)
    ld e,a
    ld d,0
    ld hl,OptKinds
    add hl,de
    ld b,(hl)
    ld hl,OptVals
    add hl,de
    ld a,(hl)                    ; the value's index
    dec b
    jp m,@onoff
    jr z,@speed
    cp 10                        ; 0X..10X
    jr c,@one
    ld a,1
    call NtDigit
    xor a
@one:
    call NtDigit
    ld a,'X'
    jp NtChar
@onoff:
    ld hl,StrOff
    or a
    jp z,NtPrint
    ld hl,StrOn
    jp NtPrint
@speed:                          ; a name: OptNames[option][value]
    push af
    ld a,(Tmp+15)
    add a,a
    ld e,a
    ld d,0
    ld hl,OptNames
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    pop af
    add a,a
    ld e,a
    ld d,0
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    jp NtPrint

OptLabels:   .DW @l0, @l1, @l2, @l3, @l4, @l5, @l6, @l7, @l8, @l9, @l10
@l0: .ASC "TEXT FIXES"
     .DB 0
@l1: .ASC "MAP FIXES"
     .DB 0
@l2: .ASC "SWORD HIT FIX"
     .DB 0
@l3: .ASC "SWORD REACH"
     .DB 0
@l4: .ASC "CAVE DOWN SPEED"
     .DB 0
@l5: .ASC "KEEP GOLD"
     .DB 0
@l6: .ASC "GOLD DROPS"
     .DB 0
@l7: .ASC "DAMAGE DEALT"
     .DB 0
@l8: .ASC "DAMAGE RECEIVED"
     .DB 0
@l9: .ASC "OVERWORLD SPEED"
     .DB 0
@l10: .ASC "EASY CAVE ENTRY"
     .DB 0
StrPage:     .ASC "PAGE"
             .DB 0
ReachNames:  .DW StrOff, @r1, @r2
@r1: .ASC "4 PIXELS"
     .DB 0
@r2: .ASC "8 PIXELS"
     .DB 0
EasyNames:   .DW StrOff, @e1, @e2
@e1: .ASC "HALF"
     .DB 0
@e2: .ASC "QUARTER"
     .DB 0
SpeedNames:  .DW @s0, @s1, @s2
@s0: .ASC "1X"
     .DB 0
@s1: .ASC "1.5X"
     .DB 0
@s2: .ASC "2X"
     .DB 0
StrOff:      .ASC "OFF"
             .DB 0
StrOn:       .ASC "ON"
             .DB 0
StrOptTitle: .ASC "OPTIONS"
             .DB 0
StrOptHelp:  .ASC "1:CHANGE 2:BACK"
             .DB 0
StrOptLegend: .ASC "*:ORIGINAL GAME"
             .DB 0
StarBits:    .DB $00, $10, $54, $38, $54, $10, $00, $00   ; our '*' (8 x 8)
ColonBits:   .DB $00, $30, $30, $00, $30, $30, $00, $00   ; our ':' (2 x 2 dots in the font's 7 x 7 cell)

; Winkle made the password (we are in his cave room, bank 7 code below us): AUTO = this game,
; position = the cave's entrance cell (where the game puts the hero when he leaves), and a line
; above his text box
AutoSave:
    call SramPrepare
    ld a,($CAD2)                 ; entrance cell = row * 15 + column
    ld b,0
@div:
    cp 15
    jr c,@got
    sub 15
    inc b
    jr @div
@got:
    add a,a
    add a,a
    add a,a
    add a,a
    add a,16
    ld c,a                       ; x = 16 + 16 * column
    ld a,b
    add a,a
    add a,a
    add a,a
    add a,a
    add a,8
    ld b,a                       ; y = 8 + 16 * row
    ld a,($C84C)
    ld (SvFlag),a
    ld a,($C80D)
    ld d,a
    xor a
    call SaveSlot
    ld a,1                       ; the line, blinking while we stay in his room (PlayTick -> AutoTick)
    ld (AutoBlink),a
AutoLine:                        ; A = 0: spaces, else "GAME SAVED TO AUTO." (row 12, centred, low tile set)
    push af
    ld hl,$3800 + 12*64 + AUTO_COL*2
    ld (NtAddr),hl
    xor a
    ld (NtAttr),a
    ld (NtTileOfs),a             ; the font is the cave's own (a Pause save leaves $C0 here)
    pop af
    or a
    ld hl,StrAutoSaved
    jp nz,NtPrint
    ld b,19
    jp NtSpaces
AutoTick:                        ; every frame wait while AutoBlink: 32 frames on, 32 off; ends when the room is left
    ld a,($C802)
    cp $11
    jr nz,@stop
    ld hl,AutoBlink
    inc (hl)
    jr nz,@go
    ld (hl),64                   ; (never 0 again while blinking)
@go:
    ld a,(hl)
    and 31
    ret nz
    ld a,(hl)
    cpl
    and 32                       ; bit 5 clear = on
    jp AutoLine
@stop:
    xor a
    ld (AutoBlink),a
    ret

; --- the SAVE page ---
SavePage:
    call PageOpen
    call FindNewest              ; the cursor on the newest save (AUTO when there is none)
    jr nc,@cur
    xor a
@cur:
    ld (SaveCur),a
    ld bc,14+256*1
    call NtAt
    ld hl,StrSaveTitle
    call NtPrint
    ld hl,StrHelp
    ld (HelpText),hl
    call DrawHelp
    ld a,5
@slots:
    push af
    call DrawSlot
    pop af
    dec a
    jp p,@slots
    call DrawCursors
    call PageShow
@loop:
    call WaitFrame
    call ReadPad
    ld a,(PadNew)
    ld b,a
    and $20
    jp nz,PageClose              ; button 2: back to PASSWORD / SAVE
    ld a,b
    and $10
    jr nz,@select
    ld a,(SaveCur)
    bit 0,b
    jr z,@notup
    dec a
    jp p,@move
    ld a,5
    jr @move
@notup:
    bit 1,b
    jr z,@loop
    inc a
    cp 6
    jr c,@move
    xor a
@move:
    ld (SaveCur),a
    call DrawCursors
    jr @loop
@select:
    ld a,(SaveCur)
    call ReadSlot
    jr c,@loop                   ; an empty slot: nothing to do
    call SlotMenu
    jr @loop

SlotRow:                         ; A = slot -> B = its first row (4, 7, ... 19)
    ld b,a
    add a,a
    add a,b
    add a,4
    ld b,a
    ret

DrawCursors:                     ; the arrow at column 1 of the slot under the cursor, spaces elsewhere
    ld a,5
@each:
    push af
    call SlotRow
    ld c,1
    call NtAt
    pop af
    push af
    ld hl,SaveCur
    cp (hl)
    ld a,$40
    jr nz,@put
    ld a,$61
@put:
    call NtChar
    pop af
    dec a
    jp p,@each
    ret

DrawHelp:                        ; row HelpRow: HelpText, centred (columns 1-30 cleared)
    ld a,(HelpRow)
    ld b,a
    ld c,1
    call NtAt
    ld b,30
    call NtSpaces
    ld a,(HelpRow)
    ld b,a
    ld hl,(HelpText)
NtCentre:                        ; B = row, HL -> text, 0: centred on the row
    push hl
    ld c,32
@len:
    ld a,(hl)
    inc hl
    or a
    jr z,@got
    dec c
    jr @len
@got:
    srl c                        ; (32 - length) / 2
    ld a,c
    ld (CentreCol),a
    call NtAt
    pop hl
    jp NtPrint

DrawSlot:                        ; A = slot: its two rows
    ld (Tmp+15),a
    call SlotRow
    ld c,3
    call NtAt
    push bc
    ld b,29
    call NtSpaces
    pop bc
    inc b
    call NtAt
    push bc
    ld b,29
    call NtSpaces
    pop bc
    dec b
    call NtAt                    ; row 1, column 3: the name
    ld a,(Tmp+15)
    or a
    ld hl,StrAuto
    jr z,@name
    ld hl,StrSlot
    call NtPrint
    ld a,(Tmp+15)
    call NtDigit
    jr @named
@name:
    call NtPrint
@named:
    ld a,(Tmp+15)
    push bc
    call ReadSlot
    pop bc
    push af                      ; (carry = empty)
    ld c,11
    call NtAt
    pop af
    ld hl,StrEmpty
    jp c,NtPrint
    ld a,(SlotBuf+1)             ; the screen: column letter + row number (A1 .. M14)
    ld d,0
@div:
    cp 13
    jr c,@cols
    sub 13
    inc d
    jr @div
@cols:
    add a,'A'
    call NtChar
    ld a,d
    inc a
    cp 10
    jr c,@one
    sub 10
    push af
    ld a,1
    call NtDigit
    pop af
@one:
    call NtDigit
    ld c,21                      ; play time
    call NtAt
    ld hl,StrTime
    call NtPrint
    push bc
    ld hl,SlotBuf+7
    ld b,1
    call NtBcd
    ld a,ColonTile               ; hours:minutes (our ':'; a quoted ':' would be ASCII $3A)
    call NtChar
    ld a,(SlotBuf+6)
    call NtBcd2
    pop bc
    inc b                        ; row 2
    ld c,3
    call NtAt
    ld hl,StrGold
    call NtPrint
    push bc
    ld hl,SlotBuf+10+($C83F-$C839)
    ld b,3
    call NtBcd                   ; gold is kept in tens
    jr z,@gold0
    xor a
    call NtDigit
@gold0:
    pop bc
    ld c,15
    call NtAt
    ld hl,StrPower
    call NtPrint
    push bc
    ld hl,SlotBuf+10
    ld b,1
    call NtBcd
    pop bc
    ld c,24
    call NtAt
    ld hl,StrCrys
    call NtPrint
    ld a,(SlotBuf+10+($C847-$C839))
    and $7F
    ld b,7
    ld c,0
@bits:
    rrca
    jr nc,@nobit
    inc c
@nobit:
    djnz @bits
    ld a,c
    call NtDigit
    ld hl,StrOf7
    jp NtPrint

; LOAD / DELETE for the slot under the cursor (row 22); returns when done or cancelled
SlotMenu:
    xor a
    ld (SubSel),a
    ld hl,StrLoadDelete
    call SubDraw
@loop:
    call SubKeys
    jr z,@loop
    jp c,DrawHelp                ; button 2
    ld a,(SubSel)
    or a
    jp z,LoadSlot                ; LOAD (does not return)
    xor a                        ; DELETE: are you sure? (NO first)
    ld (SubSel),a
    ld hl,StrConfirm
    call SubDraw
@confirm:
    call SubKeys
    jr z,@confirm
    jp c,DrawHelp
    ld a,(SubSel)
    or a
    jp z,DrawHelp                ; NO
    xor a                        ; YES: the slot's mark off
    ld (Tmp),a
    ld a,(SaveCur)
    call SlotAddr
    ex de,hl
    ld hl,Tmp
    ld bc,1
    call Stub_Sram
    ld a,(SaveCur)
    call DrawSlot
    jp DrawHelp

; row 22 for a two-choice question: HL -> text, 0, column of choice 0, column of choice 1
SubDraw:                         ; HL -> text, 0, the two cursor positions in the text: centred on HelpRow
    push hl
    ld a,(HelpRow)
    ld b,a
    ld c,1
    call NtAt
    ld b,30
    call NtSpaces
    pop hl
    ld a,(HelpRow)
    ld b,a
    call NtCentre                ; HL -> the two cursor positions now
    ld (Tmp+12),hl
SubCursor:
    ld hl,(Tmp+12)
    ld a,(CentreCol)
    add a,(hl)
    ld c,a
    ld a,(HelpRow)
    ld b,a
    call NtAt
    ld a,(SubSel)
    or a
    ld a,$61
    jr z,@a
    ld a,$40
@a:
    call NtChar
    ld hl,(Tmp+12)
    inc hl
    ld a,(CentreCol)
    add a,(hl)
    ld c,a
    ld a,(HelpRow)
    ld b,a
    call NtAt
    ld a,(SubSel)
    or a
    ld a,$40
    jr z,@b
    ld a,$61
@b:
    jp NtChar

SubKeys:                         ; one frame: Z = nothing; NZ + carry = button 2; NZ + no carry = button 1
    call WaitFrame
    call ReadPad
    ld a,(PadNew)
    ld b,a
    and $0F
    jr z,@buttons
    ld a,(SubSel)
    xor 1
    ld (SubSel),a
    call SubCursor
    xor a                        ; Z
    ret
@buttons:
    ld a,b
    and $20
    jr z,@one
    or a
    scf
    ret
@one:
    ld a,b
    and $10                      ; NZ = button 1 (carry clear), Z = nothing
    ret

LoadSlot:                        ; SlotBuf (valid) -> the game's RAM, then LAST GAME CONTINUE
    ld hl,SlotBuf+1
    ld a,(hl)
    ld (LoadScreen),a
    inc hl
    ld a,(hl)
    ld (LoadY),a
    inc hl
    ld a,(hl)
    ld (LoadX),a
    inc hl
    ld de,PlayTime
    ld bc,4
    ldir
    ld a,(hl)
    ld (LoadC84C),a
    inc hl
    ld a,(hl)
    ld ($C817),a
    inc hl
    ld de,$C839
    ld bc,19
    ldir
    ld de,$C86E
    ld bc,11
    ldir
    ld a,(SaveCur)               ; the slot's map (an older slot: none, the map starts empty)
    call LoadMap
    ld a,1
    ld (LoadPending),a
    ld a,(SaveCur)
    ld (LastSlot),a
    call WriteLastSlot
    call FadeOut
    call ScreenOff
    ld a,$FF
    call SetNameTable
    call ScreenOn                ; (the palette is black; the game's start draws the screen)
    jp Exit_LoadGame

; --- SAVE GAME (Pause / Game Over): on the game's own name table ($3800) with the game's font in
; the low tile set (loaded by the caller); slots 1-5; SvScreen / SvY / SvX / SvFlag = what is saved.
; Display off on entry, on when it returns. ---
SaveMenu:                        ; (the caller set the name table / font: GameNt, PauseSave)
    ld hl,$3800
    di
    call SetVramAddr
    ld bc,32*24
@clear:
    ld a,(NtTileOfs)
    add a,$40                    ; the space
    out ($BE),a
    xor a
    out ($BE),a
    dec bc
    ld a,b
    or c
    jr nz,@clear
    ei
    call SramPrepare
    ld bc,11+256*1
    call NtAt
    ld hl,StrSaveGame
    call NtPrint
    ld hl,StrHelpSave
    ld (HelpText),hl
    call DrawHelp
    ld a,5
@slots:
    push af
    call DrawSlot
    pop af
    dec a
    jp p,@slots
    ld a,(LastSlot)
    or a
    jr nz,@cur
    inc a
@cur:
    ld (SaveCur),a
    call DrawCursors
    call ScreenOn
@loop:
    call WaitPaused
    call ReadPad
    ld a,(PadNew)
    ld b,a
    and $20
    ret nz                       ; button 2: nothing saved
    ld a,b
    and $10
    jr nz,@select
    ld a,(SaveCur)
    bit 0,b
    jr z,@notup
    dec a
    jr nz,@move
    ld a,5
    jr @move
@notup:
    bit 1,b
    jr z,@loop
    inc a
    cp 6
    jr c,@move
    ld a,1
@move:
    ld (SaveCur),a
    call DrawCursors
    jr @loop
@select:
    ld a,(SaveCur)
    call ReadSlot
    jr c,@save                   ; empty: save at once
    xor a                        ; used: OVERWRITE? (NO first)
    ld (SubSel),a
    ld hl,StrOverwrite
    call SubDraw
@ask:
    call SubKeys
    jr z,@ask
    jr c,@no
    ld a,(SubSel)
    or a
    jr nz,@save
@no:
    call DrawHelp
    jr @loop
@save:
    ld a,(SvY)
    ld b,a
    ld a,(SvX)
    ld c,a
    ld a,(SvScreen)
    ld d,a
    ld a,(SaveCur)
    call SaveSlot
    ld a,(SaveCur)
    ld (LastSlot),a
    call WriteLastSlot
    ld a,(SaveCur)
    call DrawSlot
    ld hl,StrSaved
    ld (HelpText),hl
    call DrawHelp
    ld b,60
@show:
    push bc
    call WaitPaused
    pop bc
    djnz @show
    ret

GameNt:                          ; text on the game's own screen: name table $3800, its low-set font, and
    ld a,$38                     ; columns that follow its horizontal scroll (reg 8 shadow $C057: $F8 or $08
    ld (NtBaseHi),a              ; on the overworld), so column n is always at x = 8n (column 0 = blanked)
    ld a,22
    ld (HelpRow),a
    ld a,$DF
    ld (NtDigit0),a
    xor a
    ld (NtAttr),a
    ld (NtTileOfs),a
    ld a,($C057)
    neg
    rrca
    rrca
    rrca
    and 31
    ld (NtColBias),a
    ret

WaitPaused:                      ; one frame without play time
    xor a
    ld ($C005),a
    jp $07C7

; game-start hooks (Hook_* in bank 1: entered with Enter16, back with Leave16)
NewGameH:                        ; $12C8 New Game: play time 0, an empty map, no pending load, then the original code
    call MapClear
    xor a
    ld hl,PlayTime
    ld (hl),a
    inc hl
    ld (hl),a
    inc hl
    ld (hl),a
    inc hl
    ld (hl),a
    ld (LoadPending),a
    ld ($C83E),a                 ; the original xor a / ld ($C83E),a (A = 0, Z)
    jp Leave16
StartScreenH:                    ; $13E3: the start screen of a continue
    ld a,(LoadPending)
    or a
    ld a,$B4                     ; L14 (the original)
    jr z,@set
    ld a,(LoadScreen)
@set:
    ld ($C80D),a
    jp Leave16                   ; (-> $13E6 nop nop, $13E8 ret)
StartPosH:                       ; $1391: the hero's start position (and the loaded screen's tile-set flag)
    ld hl,$8898                  ; x $88, y $98 (the original)
    ld a,(LoadPending)
    or a
    jr z,@set
    ld a,($C802)
    cp 6                         ; only on the overworld (the game may send the hero to the start area instead)
    jr nz,@set
    xor a
    ld (LoadPending),a
    ld a,(LoadC84C)
    ld ($C84C),a                 ; ($1384 cleared it; the screen load needs the saved screen's value)
    ld hl,(LoadY)
@set:
    ld ($C101),hl                ; $C101 = y, $C102 = x
    jp Leave16                   ; (-> 7 x nop, $139B)

; --- option hooks (Hook_* in bank 1: entered with Enter16, back with Leave16 = the game's bank, AF kept) ---
MapH:                            ; $193D: the screen just loaded ($C80D) is seen (the map); MAP FIXES: its room record is at $CAD0
    ld a,$FF                     ; EASY CAVE ENTRY: no previous step on this screen yet (a loaded save, a
    ld (CaveLastY),a             ; teleport, back out of a cave: the first step never counts as "closer")
    push hl
    ld a,($C80D)
    cp 182
    jr nc,@seen
    ld hl,MapVis
    call MarkSet
@seen:
    pop hl
    ld a,(OptMap)
    or a
    jr z,@done
    ld a,($C80D)
    cp 54                        ; C5 (room record 126): its own layout $7E, not E1's $8E
    jr nz,@h4
    ld a,($CAD0)
    cp $8E
    jr nz,@done
    ld a,$7E
    ld ($CAD0),a
    jr @done
@h4:
    cp 46                        ; H4 (room record 175): the cave entrance graphic $2B, not $94
    jr nz,@done
    ld a,($CAD6)
    cp $94
    jr nz,@done
    ld a,$2B
    ld ($CAD6),a
@done:
    ld a,($CAD0)                 ; the original instruction
    jp Leave16

DealtH:                          ; $33EE DAMAGE DEALT: A = sword damage -> E, capped at the enemy's HP (IX+$15)
    ld e,a
    ld a,(OptDealt)
    cp 1
    jr z,@done                   ; x1: the original
    push bc
    ld b,a
    xor a
    inc b
    dec b
    jr z,@got                    ; x0: no damage
@mul:
    add a,e
    jr c,@sat
    djnz @mul
    jr @got
@sat:
    ld a,$FF
@got:
    ld e,a
    ld a,(ix+$15)
    cp e
    jr nc,@fits
    ld e,a                       ; no more than its HP (= exactly dead)
@fits:
    pop bc
@done:
    ld a,(ix+$15)                ; the original second instruction
    jp Leave16

GoldH:                           ; $35EA GOLD DROPS: E (BCD tens) added OptGold times; carry = overflow (-> GOLD MAX)
    push bc
    ld a,(OptGold)
    ld b,a
    or a
    jr z,@none                   ; x0: nothing
@add:
    ld a,($C841)                 ; the original add, once per x
    add a,e
    daa
    ld ($C841),a
    ld a,($C840)
    adc a,0
    daa
    ld ($C840),a
    ld a,($C83F)
    adc a,0
    daa
    ld ($C83F),a
    jr c,@over
    djnz @add
@none:
    pop bc
    or a                         ; no overflow
    jp Leave16
@over:
    pop bc
    scf
    jp Leave16

RecvH:                           ; $37D8 DAMAGE RECEIVED: A = (HL) (< 0 = power lost), HL = $C81A as the original
    ld a,(hl)
    ld hl,$C81A
    bit 7,a
    jp z,Leave16                 ; healing / nothing: unchanged
    push bc
    ld c,a
    ld a,(OptRecv)
    cp 1
    jr z,@keep                   ; x1: the original
    ld b,a
    ld a,c
    neg
    ld c,a                       ; the damage (1..128)
    xor a
    inc b
    dec b
    jr z,@keep0                  ; x0: no damage
@mul:
    add a,c
    jr c,@sat
    cp $80
    jr nc,@sat
    djnz @mul
    jr @neg
@sat:
    ld a,$7F
@neg:
    neg
    pop bc
    jp Leave16
@keep0:
    pop bc
    jp Leave16                   ; A = 0
@keep:
    ld a,c
    pop bc
    jp Leave16

ReachH:                          ; $3509 SWORD REACH: widen the box of a hitting sword ($84) once the game wrote it
    ld a,(OptReach)
    or a
    jr z,@done
    push hl
    push de
    push bc
    add a,a
    add a,a
    ld c,a                       ; 4 / 8 pixels
    ld hl,$C120
    ld de,ReachBox
    call @one
    ld hl,$C140
    ld de,ReachBox+4
    call @one
    pop bc
    pop de
    pop hl
@done:
    ld a,($C120)                 ; the original instruction
    jp Leave16
@one:                            ; HL = sword object, DE -> the box we wrote last, C = pixels
    ld a,(hl)
    cp $84
    ret nz
    ld a,l
    add a,$16
    ld l,a                       ; +$16..+$19: low / high x, low / high y
    push hl
    push de
    ld b,4
@cmp:
    ld a,(de)
    cp (hl)
    jr nz,@new
    inc hl
    inc de
    djnz @cmp
    pop de                       ; still our widened box
    pop hl
    ret
@new:
    pop de
    pop hl
    ld b,2
@axis:
    ld a,(hl)
    sub c
    jr nc,@lo
    xor a
@lo:
    ld (hl),a
    ld (de),a
    inc hl
    inc de
    ld a,(hl)
    add a,c
    jr nc,@hi
    ld a,$FF
@hi:
    ld (hl),a
    ld (de),a
    inc hl
    inc de
    djnz @axis
    ret

SwingH:                          ; $3F45 / $43AC / $4678 SWORD HIT FIX: a new swing (SwingNo + 1, never 0)
    ld a,(SwingNo)
    inc a
    jr nz,@n
    inc a
@n:
    ld (SwingNo),a
    ld a,4                       ; the original: the swing starts (state 4)
    ld ($C120),a
    jp Leave16

ContactH:                        ; $33D1 SWORD HIT FIX: the sword touches the enemy at IX; A = its hit cooldown
    ld a,(OptSword)              ; (+$1F: damage only at 0, then 11; counted down only while touching)
    or a
    jr z,@done
    push hl
    push de
    push ix
    pop hl
    ld de,$10000-$C160
    add hl,de                    ; HL = IX - $C160 (carry: IX >= $C160)
    jr nc,@out
    ld a,h
    cp 2
    jr nc,@out
    ld a,l
    rlca
    rlca
    rlca
    and 7
    bit 0,h
    jr z,@slot
    add a,8
@slot:
    cp 13
    jr nc,@out                   ; not one of the 13 enemy objects
    ld e,a
    ld d,0
    ld hl,SwingSeen
    add hl,de
    ld a,(SwingNo)
    cp (hl)
    jr z,@out                    ; this swing touched it before: the original cooldown
    ld (hl),a                    ; the first contact of a new swing: the cooldown starts at 0
    ld (ix+$1F),0
@out:
    pop de
    pop hl
@done:
    ld a,(ix+$1F)                ; the original instruction
    jp Leave16

CaveH:                           ; bank 1 $4646 CAVE DOWN SPEED: the hero's speed (IX+4 = $C104) by direction
    push af
    ld a,(OptCave)
    or a
    jr z,@done                   ; off: the game's own speed, untouched
    push bc
    add a,2
    ld b,a                       ; down: 3 or 4
    ld a,($C801)                 ; direction: 0 down-left, 3 down, 6 down-right
    or a
    jr z,@down
    cp 3
    jr z,@down
    cp 6
    jr z,@down
    ld b,2                       ; any other: the original 2
@down:
    ld a,b
    ld ($C104),a
    pop bc
@done:
    pop af
    ret

TextH:                           ; bank 7 $8EC8 TEXT FIXES: A = the character at HL; then the original cp $F0
    push hl
    push de
    push bc
    ld c,a
    ld a,(OptText)
    or a
    jr z,@done
    ex de,hl
    ld hl,TypoTable
    ld b,(TypoEnd-TypoTable)/3
@scan:
    ld a,(hl)
    inc hl
    cp e
    jr nz,@skip
    ld a,(hl)
    cp d
    jr nz,@skip
    inc hl
    ld c,(hl)
    jr @done
@skip:
    inc hl
    inc hl
    djnz @scan
@done:
    ld a,c
    pop bc
    pop de
    pop hl
    cp $F0
    jp Leave16
TypoTable:                       ; bank 7 address, the right letter
    .DW $99AF
    .DB 'O'                      ; PASSWARD -> PASSWORD (text $03, Winkle)
    .DW $A1E6
    .DB 'F'                      ; "DIDST THOU GOLD THE RING" -> FIND (text $24)
    .DW $A1E7
    .DB 'I'
    .DW $A1E8
    .DB 'N'
    .DW $A665
    .DB 'F'                      ; "GO GOLD MORE" -> FIND (text $2B)
    .DW $A666
    .DB 'I'
    .DW $A667
    .DB 'N'
    .DW $AA24
    .DB 'F'                      ; "HELP THEE GOLD MANY UNUSUAL OBJECTS" -> FIND (text $2F)
    .DW $AA25
    .DB 'I'
    .DW $AA26
    .DB 'N'
TypoEnd:

StrH:                            ; $0306 TEXT FIXES in the credits: A = the character, HL past it; then or a
    or a
    jp z,Leave16                 ; the end of the string
    push hl
    push bc
    ld c,a
    ld a,(OptText)
    or a
    jr z,@done
    ld a,(SavedBank)
    cp 15                        ; the credits strings are in bank 15
    jr nz,@done
    dec hl
    ld a,h
    cp $BC
    jr nz,@done
    ld a,l
    cp $5B                       ; "DESGINED" at $BC58: G I -> I G
    jr nz,@i
    ld c,'I'
    jr @done
@i:
    cp $5C
    jr nz,@done
    ld c,'G'
@done:
    ld a,c
    pop bc
    pop hl
    or a
    jp Leave16

; the overworld's Pause screen, button 1 (from Stub_PauseWait)
PauseSave:
    call ScreenOff
    call GameNt
    ld a,$C0                     ; characters $40-$6F -> tiles $00-$2F, digits -> $30-$39: only inside the
    ld (NtTileOfs),a             ; status screen's tiles ($0000-$07FF, reloaded below and on unpause). VRAM
    ld a,$30                     ; $1800-$1F3F holds the overworld's name table + sprite rows (asset $71):
    ld (NtDigit0),a              ; the whole font (to $1D1F at $0800) would overwrite them
    ld hl,0
    ld (UpLo),hl
    ld hl,48*8
    ld (UpCnt),hl
    ld hl,$0000
    ld (UpDst),hl
    call UnpackFont
    ld hl,($DF-$40)*8
    ld (UpLo),hl
    ld hl,80
    ld (UpCnt),hl
    ld hl,$0600
    ld (UpDst),hl
    call UnpackFont
    ld hl,(ColonTile-$40)*32
    call ColonUp
    ld a,($C80D)
    ld (SvScreen),a
    ld a,($C101)
    ld (SvY),a
    ld a,($C102)
    ld (SvX),a
    ld a,($C84C)
    ld (SvFlag),a
    call SaveMenu
    call ScreenOff
    call Stub_Unpause72          ; the overworld's name table back to $3800 (as on unpause), so that
    jp $291F                     ; the status screen ($71 saves it again) is drawn and the pause loop waits

; Game Over: the password was shown and a button pressed (from Stub_GameOver; the font is loaded)
GameOverAsk:
    call GameNt
    xor a
    ld (SubSel),a
    ld hl,StrSaveAsk
    call SubDraw
@ask:
    call SubKeys
    jr z,@ask
    jr c,@done
    ld a,(SubSel)
    or a
    jr z,@done
    call ScreenOff
    ld a,$B4                     ; LAST GAME CONTINUE starts at L14 with the original position
    ld (SvScreen),a
    ld a,$98
    ld (SvY),a
    ld a,$88
    ld (SvX),a
    xor a
    ld (SvFlag),a
    ld hl,ColonTile*32
    call ColonUp
    call SaveMenu
@done:
    call SoundStop               ; the original $2156, then $2159: fade, back to the title
    jp $2159

; --- texts ---
StrStart:    .DB 12, 20
             .ASC "START"
             .DB 0
StrOptions:  .DB 12, 22
             .ASC "OPTIONS"
             .DB 0
StrPassword: .DB 12, 20
             .ASC "PASSWORD"
             .DB 0
StrSave:     .DB 12, 22
             .ASC "SAVE"
             .DB 0

StrSaveTitle:  .ASC "SAVE"
               .DB 0
StrHelp:       .ASC "1:SELECT 2:BACK"
               .DB 0
StrAuto:       .ASC "AUTO"
               .DB 0
StrSlot:       .ASC "SLOT "
               .DB 0
StrEmpty:      .ASC "EMPTY"
               .DB 0
StrTime:       .ASC "TIME "
               .DB 0
StrGold:       .ASC "GOLD "
               .DB 0
StrPower:      .ASC "POWER "
               .DB 0
StrCrys:       .ASC "CRYS "
               .DB 0
StrOf7:        .ASC "/7"
               .DB 0
StrLoadDelete: .ASC "  LOAD    DELETE"          ; then the cursor positions in the text
               .DB 0, 0, 8
StrConfirm:    .ASC "DELETE?    NO    YES"
               .DB 0, 9, 15
StrAutoSaved:  .ASC "GAME SAVED TO AUTO."
               .DB 0
StrSaveGame:   .ASC "SAVE GAME"
               .DB 0
StrHelpSave:   .ASC "1:SAVE 2:BACK"
               .DB 0
StrOverwrite:  .ASC "OVERWRITE?    NO    YES"
               .DB 0, 12, 18
StrSaved:      .ASC "SAVED."
               .DB 0
StrHintL:      .DB 48                    ; x of the first character cell ("1" is drawn from x + 1): ink 49-94
               .ASC "1:SAVE"
               .DB 0
StrHintR:      .DB 164                   ; ink 164-202
               .ASC "2:MAP"
               .DB 0
StrWorldMap:   .ASC "WORLD MAP"
               .DB 0
StrMapHelp:    .ASC "1:OPEN 2:BACK PAUSE:EXIT"
               .DB 0
StrMapInfoHelp: .ASC "ARROWS:NEXT 2:BACK PAUSE:EXIT"
               .DB 0
StrGap2:       .ASC "  "
               .DB 0
StrNotSeen:    .ASC "NOT SEEN YET"
               .DB 0
StrCaveQ:      .ASC "CAVE  ?"
               .DB 0
StrStartHouse: .ASC "START"            ; (our name: L14 has no entrance, only the scripted old man's room)
               .DB 0
StrDungeon:    .ASC "DUNGEON  "
               .DB 0
StrCave:       .ASC "CAVE"
               .DB 0
StrAdvice:     .ASC "ADVICE"
               .DB 0
StrOldWoman:   .ASC "THE OLD WOMAN"
               .DB 0
StrRandar:     .ASC "RANDAR  POWER FOR GOLD"
               .DB 0
StrEnny:       .ASC "ENNY  POWER FOR GOLD"
               .DB 0
StrWinkle:     .ASC "WINKLE  PASSWORD"
               .DB 0
StrDina:       .ASC "DINA  GOLD FOR POWER"
               .DB 0
StrO28:        .ASC "POTION OF LIFE"
               .DB 0
StrO29:        .ASC "BIBLE"
               .DB 0
StrO2A:        .ASC "CRYSTAL"
               .DB 0
StrO2B:        .ASC "SWORD"
               .DB 0
StrO2C:        .ASC "SHIELD"
               .DB 0
StrO2D:        .ASC "PENDANT"
               .DB 0
StrO2E:        .ASC "BOOTS"
               .DB 0
StrO2F:        .ASC "RING THAT BREAKS ROCK"
               .DB 0
StrO30:        .ASC "RING FOR THE OLD WOMAN"
               .DB 0
StrO31:        .ASC "HERB MEA"
               .DB 0
StrO32:        .ASC "MAGIC MIRROR"
               .DB 0
StrGoldPrice:  .ASC "00 GOLD"
               .DB 0
StrFree:       .ASC "FREE"
               .DB 0
StrBought:     .ASC "    BOUGHT"
               .DB 0
StrTaken:      .ASC "    TAKEN"
               .DB 0
StrGolvellius: .ASC "GOLVELLIUS"
               .DB 0
StrPoss:       .ASC "'S "
               .DB 0
StrPossS:      .ASC "' "
               .DB 0
StrValley:     .ASC "VALLEY"
               .DB 0
StrDesert:     .ASC "DESERT"
               .DB 0
StrGraveyard:  .ASC "GRAVEYARD"
               .DB 0
StrSwamp:      .ASC "SWAMP"
               .DB 0
StrLake:       .ASC "LAKE"
               .DB 0
StrForest:     .ASC "FOREST"
               .DB 0
StrSaveAsk:    .ASC "SAVE THIS GAME?    NO    YES"
               .DB 0, 17, 23
.ENDS
