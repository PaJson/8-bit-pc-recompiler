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
;   The status screen says "1  SAVE GAME" under its frame (letters in tiles the unpause reloads).
;   Once a slot is used, the title's cursors start on CONTINUE and on SAVE.
; Step 6: OPTIONS page (title -> OPTIONS): TEXT FIXES, MAP FIXES, SWORD HIT FIX, CAVE DOWN SPEED,
;   KEEP GOLD, GOLD DROPS / DAMAGE DEALT / DAMAGE RECEIVED x0-x10, OVERWORLD SPEED 1X-4X (the hero's
;   1-pixel overworld step runs up to 4 times per frame, with every check of the game), RESET TO DEFAULTS. Kept in battery
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
.DEFINE OPT_N       10
.DEFINE OptLoaded   $C0F5        ; 1 = OptVals came from the battery RAM (or the defaults) since power-on
.DEFINE OptCur      $C0DB        ; OPTIONS page: the row under the cursor (OPT_N = RESET TO DEFAULTS)
.DEFINE ReachBox    $C0DC        ; 8: SWORD REACH: the box last widened, per sword object ($C120, $C140)
.DEFINE SwingNo     $C0E4        ; SWORD HIT FIX: counts the swings (1..255)
.DEFINE SwingSeen   $C0E5        ; 13: per enemy object ($C160 + 32 n): the swing that touched it last
.DEFINE UpEnd       $C0F2        ; 2: UnpackFont: UpLo + UpCnt
.DEFINE HelpRow     $C0F4        ; the row of DrawHelp / SubDraw (22)
.DEFINE StepLeft    $C0F6        ; OVERWORLD SPEED: extra steps left in this frame
.DEFINE StepFlags   $C0F7        ; bit 0 = an extra step is running, bit 1 = 1.5X: this frame has one
.DEFINE StarTile    $BA          ; OPTIONS page: our '*' (title tiles 256+$BA, VRAM $3740)

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
    and $10
    ret z
    pop hl                       ; (back into the pause loop later by jp $291F)
    call Map16
    jp PauseSave
Stub_Winkle:                     ; from bank 7 $9138: the password, then the AUTO save
    ld a,7                       ; (bank 7 is in slot 2 already: says so for the recompiler)
    ld ($FFFF),a
    call MakePassword
    call Enter16                 ; (saves bank 7)
    call AutoSave
    jp Leave16
.ENDS

; ---------------------------------------------------------------------------------------------
; Bank 16
.BANK 16 SLOT 2
.ORG $0000
.SECTION "gvplus_id" FORCE
GvPlusId:
    .DB "GOLVELLIUS PLUS", 0     ; 16 bytes
    .DB 1                         ; patch format version
    .DB 6                         ; step / feature level (6 = OVERWORLD SPEED 1X / 1.5X / 2X, 10 options)
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
    ld a,($C802)
    cp 5
    ret c
    cp $13
    ret nc
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
    jp Stub_Sram

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
    ld a,(OptWalk)
    or a
    jr z,@tail
    ld a,(StepFlags)
    bit 0,a
    jr nz,@more                  ; an extra step of this frame
    xor 2                        ; the frame's own step: this frame's extra steps (2X: 1, 1.5X: 1 every
    ld (StepFlags),a             ; other frame)
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
@tail:
    ld a,(ix+$10)                ; the original instruction
    jp Leave16

; $293F: the overworld's status screen is drawn, the display still off: "1  SAVE GAME" under its frame.
; The status screen uses tiles $00-$73; unpausing reloads $0000-$13BF (asset $B0), so tiles $74-$8B
; take the font's characters $40-$56 (space .. V) and the digit 1.
PauseHintH:
    call GameNt
    ld a,$74-$40
    ld (NtTileOfs),a
    ld a,$8B-1
    ld (NtDigit0),a
    ld hl,0
    ld (UpLo),hl
    ld hl,23*8
    ld (UpCnt),hl
    ld hl,$74*32
    ld (UpDst),hl
    call UnpackFont
    ld hl,($E0-$40)*8            ; the digit 1 (font tile $E0)
    ld (UpLo),hl
    ld hl,8
    ld (UpCnt),hl
    ld hl,$8B*32
    ld (UpDst),hl
    call UnpackFont
    ld bc,10+256*22              ; row 22 (under the frame), column 10
    call NtAt
    ld hl,StrPauseHint
    call NtPrint
    jp ScreenOn

; --- OPTIONS: battery RAM $8010-$8017 = OptVals, $8018 = check byte ---
OptDefaults: .DB 0, 0, 0, 0, 0, 0, 1, 1, 1, 0     ; the original game
OptCount:    .DB 2, 2, 2, 3, 3, 2, 11, 11, 11, 3  ; number of values
OptKinds:    .DB 0, 0, 0, 1, 1, 0, 2, 2, 2, 1     ; 0 OFF / ON, 1 a name from OptNames, 2 X0..X10
OptNames:    .DW 0, 0, 0, ReachNames, SpeedNames, 0, 0, 0, 0, SpeedNames

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
    ld hl,Tmp
    call OptSum
    ld hl,Tmp+OPT_N
    cp (hl)
    jr z,@valid
    ld hl,Tmp                    ; saved before OVERWORLD SPEED existed: 9 options + their check byte
    ld b,OPT_N-1
    call OptSumB
    ld hl,Tmp+OPT_N-1
    cp (hl)
    jr nz,@def
    xor a
    ld (hl),a                    ; OVERWORLD SPEED: the default
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

; the page: title row 0, option n on row 2 + 2n (label column 3, value column 22, arrow column 1,
; '*' at column 30 when the value is the original game's), help on row 22, the '*' legend on row 23.
; Up / down = row, left = previous value, right / button 1 = next value, button 2 = save and back.
OptionsPage:
    call PageOpen
    ld hl,$2000+StarTile*32      ; our '*' (colour 15 = every plane, as the font)
    di
    call SetVramAddr
    ld hl,StarBits
    ld b,8
@star:
    ld a,(hl)
    out ($BE),a
    out ($BE),a
    out ($BE),a
    out ($BE),a
    inc hl
    djnz @star
    ei
    ld bc,9+256*23
    call NtAt
    ld a,StarTile
    call NtTile
    ld hl,StrOptLegend
    call NtPrint
    xor a
    ld (OptCur),a
    ld bc,12+256*0
    call NtAt
    ld hl,StrOptTitle
    call NtPrint
    ld hl,StrOptHelp
    ld (HelpText),hl
    call DrawHelp
    ld a,OPT_N-1
@rows:
    push af
    call OptDrawRow
    pop af
    dec a
    jp p,@rows
    call OptCursors
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
    dec a
    jp p,@move
    ld a,OPT_N-1
    jr @move
@notup:
    bit 1,b
    jr z,@notdown
    inc a
    cp OPT_N
    jr c,@move
    xor a
@move:
    ld (OptCur),a
    call OptCursors
    jp @loop
@notdown:
    ld c,a                       ; the option
    ld a,b
    and $1C                      ; left, right, button 1
    jp z,@loop
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
    ld a,c
    call OptDrawRow
    jp @loop
@back:
    call OptSave
    jp PageClose

OptCursors:                      ; the arrow at the row under the cursor, spaces elsewhere
    ld a,OPT_N-1
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

OptDrawRow:                      ; A = row (0..OPT_N-1): its label, value and '*' when it is the original
    ld (Tmp+15),a
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
    ld a,(Tmp+15)
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
    push af                      ; X0..X10
    ld a,'X'
    call NtChar
    pop af
    cp 10
    jp c,NtDigit
    ld a,1
    call NtDigit
    xor a
    jp NtDigit
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

OptLabels:   .DW @l0, @l1, @l2, @l3, @l4, @l5, @l6, @l7, @l8, @l9
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
ReachNames:  .DW StrOff, @r1, @r2
@r1: .ASC "4 PIXELS"
     .DB 0
@r2: .ASC "8 PIXELS"
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
StrOptHelp:  .ASC "1  CHANGE      2  BACK"
             .DB 0
StrOptLegend: .ASC "  ORIGINAL GAME"
             .DB 0
StarBits:    .DB $00, $10, $54, $38, $54, $10, $00, $00   ; our '*' (8 x 8)

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
    ld hl,$3800 + 12*64 + 2*2    ; row 12, column 2 of the cave room's name table, low tile set
    ld (NtAddr),hl
    xor a
    ld (NtAttr),a
    ld (NtTileOfs),a             ; the font is the cave's own (a Pause save leaves $C0 here)
    ld hl,StrAutoSaved
    jp NtPrint

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

DrawHelp:                        ; row HelpRow: HelpText
    ld a,(HelpRow)
    ld b,a
    ld c,2
    call NtAt
    ld b,28
    call NtSpaces
    ld a,(HelpRow)
    ld b,a
    ld c,5
    call NtAt
    ld hl,(HelpText)
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
    ld a,'H'
    call NtChar
    ld a,(SlotBuf+6)
    call NtBcd2
    ld a,'M'
    call NtChar
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
SubDraw:
    push hl
    ld a,(HelpRow)
    ld b,a
    ld c,2
    call NtAt
    ld b,28
    call NtSpaces
    pop hl
    ld a,(HelpRow)
    ld b,a
    ld c,3
    call NtAt
    call NtPrint                 ; HL -> the two cursor columns now
    ld (Tmp+12),hl
SubCursor:
    ld hl,(Tmp+12)
    ld c,(hl)
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
    ld c,(hl)
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
NewGameH:                        ; $12C8 New Game: play time 0, no pending load, then the original code
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
MapH:                            ; $193D MAP FIXES: the room record of the screen just loaded is at $CAD0
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
StrHelp:       .ASC "1  SELECT      2  BACK"
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
StrLoadDelete: .ASC "     LOAD       DELETE"
               .DB 0, 6, 17
StrConfirm:    .ASC "DELETE?      NO    YES"
               .DB 0, 14, 20
StrAutoSaved:  .ASC "GAME SAVED TO AUTO."
               .DB 0
StrSaveGame:   .ASC "SAVE GAME"
               .DB 0
StrHelpSave:   .ASC "1  SAVE        2  BACK"
               .DB 0
StrOverwrite:  .ASC "OVERWRITE?   NO    YES"
               .DB 0, 14, 20
StrSaved:      .ASC "     SAVED."
               .DB 0
StrPauseHint:  .ASC "1  SAVE GAME"
               .DB 0
StrSaveAsk:    .ASC "SAVE THIS GAME?  NO   YES"
               .DB 0, 18, 23
.ENDS
