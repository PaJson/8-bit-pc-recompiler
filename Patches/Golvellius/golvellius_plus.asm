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
;   DELETE (DELETE asks), the cursor starts on the slot loaded last. A load restores the progress
;   the game itself keeps in RAM between a Game Over and LAST GAME CONTINUE, then runs the game's
;   LAST GAME CONTINUE with the start screen and position replaced (AUTO: in front of Winkle's
;   cave, where the game puts the hero when he leaves it). Play time: our own counter (the game
;   has none), one tick per frame the game waits ($07C3) while a game runs (mode 5-$12).
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
;   $8100 + 64 * n  slot n (0 = AUTO, 1-5): +0 $A5 = used, +1 screen, +2 hero y, +3 hero x,
;     +4 play time (frames, seconds, minutes BCD, hours BCD), +8 $C84C (screen tile-set flag),
;     +9 $C817, +10 $C839-$C84B (19), +29 $C86E-$C878 (11), +40..+62 0, +63 checksum

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
; $5B-$5F, '/' at $6E; our digits sit at $B0-$B9 of the title font (unpacked by UnpackDigits).
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
    jp Stub_NewGame
    nop
.ENDS
.ORGA $13E3                      ; continue: was ld a,$B4 / ld ($C80D),a (start screen L14)
.SECTION "hook_start_screen" OVERWRITE
    jp Stub_StartScreen
    nop
    nop
.ENDS
.ORGA $1391                      ; every game start: was ld a,$88 / ld ($C102),a / ld a,$98 / ld ($C101),a (hero x, y)
.SECTION "hook_start_position" OVERWRITE
    jp Stub_StartPos
    .DB 0, 0, 0, 0, 0, 0, 0
.ENDS
.BANK 7 SLOT 2
.ORGA $9138                      ; Winkle (text command $FA): was call $8336 (make + draw the password)
.SECTION "hook_winkle" OVERWRITE
    call Stub_Winkle
.ENDS

; ---------------------------------------------------------------------------------------------
; Bank 1 stubs (fixed slot): every switch of slot 2 between the game and our bank 16
.BANK 1 SLOT 1
.ORGA $7E99
.SECTION "gvplus_stubs" FORCE
Stub_TitleButton:                ; from $27F8
    ld a,16
    ld ($FFFF),a
    jp TitleButton
Stub_ContMenu:                   ; from $284B
    ld a,16
    ld ($FFFF),a
    jp ContMenu
Stub_NewGameKeys:                ; button 1 = the original select, button 2 = back to START / OPTIONS
    ld a,b
    and $10
    jp nz,$2844
    ld a,b
    and $20
    jp z,$282F
    ld a,16
    ld ($FFFF),a
    jp TopMenu
Stub_LastGameKeys:               ; button 1 = the original select, button 2 = back to PASSWORD / SAVE
    ld a,b
    and $10
    jp nz,$288A
    ld a,b
    and $20
    jp z,$2875
    ld a,16
    ld ($FFFF),a
    jp ContMenu
Exit_TitleWait:                  ; -> the title's wait for a button (music keeps playing, the intro demo after a while)
    ld a,2
    ld ($FFFF),a
    jp $27AE
Exit_NewGameMenu:                ; -> the original NEW GAME / CONTINUE menu
    ld a,2
    ld ($FFFF),a
    jp $2806
Exit_BackToContinue:             ; back from PASSWORD / SAVE: the original menu with the cursor on CONTINUE
    ld a,2                       ; (its own start $2806 picks NEW GAME when no game is in RAM)
    ld ($FFFF),a
    ld hl,$28AC                  ; "NEW GAME" / "CONTINUE", as $2806-$280F
    call DrawString
    ld hl,$28B7
    call DrawString
    ld a,1
    ld (MenuSel),a
    jp $281D                     ; the menu loop
Exit_Password:                   ; -> the original LAST GAME / PASSWORD CONTINUE menu
    ld a,2
    ld ($FFFF),a
    ld hl,$28C2
    jp $284E
Exit_LoadGame:                   ; a slot was loaded into RAM: the game's LAST GAME CONTINUE
    ld a,2
    ld ($FFFF),a
    jp $131B
Stub_ReadB12:                    ; A = the byte at HL in bank 12 (the font asset), back to bank 16
    ld a,12
    ld ($FFFF),a
    ld a,(hl)
    push af
    ld a,16
    ld ($FFFF),a
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
    ld a,16
    ld ($FFFF),a
    call PlayTick
    pop af
    ld ($FFFF),a
    xor a
    ld ($C005),a
    jp $07C7
Stub_NewGame:                    ; from $12C8: play time 0, no pending load, then the original code
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
    ld ($C83E),a
    jp $12CC
Stub_StartScreen:                ; from $13E3: the start screen of a continue
    ld a,(LoadPending)
    or a
    ld a,$B4                     ; L14 (the original)
    jr z,@set
    ld a,(LoadScreen)
@set:
    ld ($C80D),a
    ret                          ; (the original $13E8 is ret)
Stub_StartPos:                   ; from $1391: the hero's start position (and the loaded screen's tile-set flag)
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
    jp $139B
Stub_Winkle:                     ; from bank 7 $9138: the password, then the AUTO save
    ld a,7                       ; (bank 7 is in slot 2 already: says so for the recompiler)
    ld ($FFFF),a
    call MakePassword
    ld a,16
    ld ($FFFF),a
    call AutoSave
    ld a,7
    ld ($FFFF),a
    ret
.ENDS

; ---------------------------------------------------------------------------------------------
; Bank 16
.BANK 16 SLOT 2
.ORG $0000
.SECTION "gvplus_id" FORCE
GvPlusId:
    .DB "GOLVELLIUS PLUS", 0     ; 16 bytes
    .DB 1                         ; patch format version
    .DB 4                         ; step / feature level (4 = save slots)
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
    call ClearMenuRows           ; START
    jp Exit_NewGameMenu
@options:
    ld hl,PageOptions
    call ShowPage
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

ContMenu:
    xor a
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
    jp Exit_Password
@save:
    call SavePage                ; (does not return when a slot is loaded)
    ld a,1
    ld (MenuSel),a
    jr ContMenuSel
@back:
    call ClearMenuRows
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
    call UnpackDigits
    ld a,1
    ld (NtAttr),a
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

; a page of fixed strings: HL = 0-ended list of strings (column, row, text, 0); returns on button 2
ShowPage:
    push hl
    call PageOpen
    pop hl
@string:
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,d
    or e
    jr z,@shown
    push hl
    ex de,hl
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    call NtAt
    call NtPrint
    pop hl
    jr @string
@shown:
    call PageShow
@wait:
    call WaitFrame
    call ReadPad
    ld a,(PadNew)
    and $20                      ; button 2 = back
    jr z,@wait
    jp PageClose

; --- writing into a name table (works with the display on: one character at a time) ---
NtAt:                            ; B = row, C = column of the page's name table ($3000)
    push hl
    ld l,b
    ld h,0
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl                    ; row * 64
    ld a,c
    add a,a
    add a,l
    ld l,a
    ld a,h
    adc a,$30
    ld h,a
    ld (NtAddr),hl
    pop hl
    ret
NtChar:                          ; A = tile, high byte NtAttr, at NtAddr (advances)
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
    ld (NtAddr),hl
    pop hl
    ret
NtPrint:                         ; HL -> text, 0
    ld a,(hl)
    inc hl
    or a
    ret z
    call NtChar
    jr NtPrint
NtSpaces:                        ; B spaces
    ld a,$40
    call NtChar
    djnz NtSpaces
    ret
NtDigit:                         ; A = 0-9
    add a,$B0
    jp NtChar
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
UnpackDigits:
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
@lit:
    call @read
    call @put
    djnz @lit
    jr @next
@run:
    and $7F
    ld b,a
    call @read
@rep:
    call @put
    djnz @rep
    jr @next
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
@put:                            ; A = byte DE of plane C; keep bytes ($DF-$40)*8 .. +79
    push hl
    push af
    ld hl,-(($DF-$40)*8)
    add hl,de                    ; carry = DE >= first digit byte
    inc de
    jr nc,@skip
    ld a,h
    or a
    jr nz,@skip
    ld a,l
    cp 80
    jr nc,@skip
    add hl,hl
    add hl,hl
    ld a,l
    or c
    ld l,a
    ld a,h
    add a,$36
    ld h,a                       ; $3600 + 4 * byte + plane
    di
    call SetVramAddr
    pop af
    out ($BE),a
    ei
    pop hl
    ret
@skip:
    pop af
    pop hl
    ret

; --- play time (every frame wait): only while a game runs (modes 5-$12; not the title, not Game Over) ---
PlayTick:
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

SaveSlot:                        ; A = slot, B = hero y, C = x: the current game -> battery RAM
    push af
    ld hl,SlotBuf
    ld (hl),$A5
    inc hl
    ld a,($C80D)
    ld (hl),a
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
    ld a,($C84C)
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
    ex de,hl
    ld b,23
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
    xor a
    call SaveSlot
    ld hl,$3800 + 12*64 + 2*2    ; row 12, column 2 of the cave room's name table, low tile set
    ld (NtAddr),hl
    xor a
    ld (NtAttr),a
    ld hl,StrAutoSaved
    jp NtPrint

; --- the SAVE page ---
SavePage:
    call PageOpen
    call SramPrepare
    ld a,(LastSlot)
    ld (SaveCur),a
    ld bc,14+256*1
    call NtAt
    ld hl,StrSaveTitle
    call NtPrint
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

DrawHelp:                        ; row 22: the buttons
    ld bc,2+256*22
    call NtAt
    ld b,28
    call NtSpaces
    ld bc,5+256*22
    call NtAt
    ld hl,StrHelp
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
    ld bc,2+256*22
    call NtAt
    ld b,28
    call NtSpaces
    pop hl
    ld bc,3+256*22
    call NtAt
    call NtPrint                 ; HL -> the two cursor columns now
    ld (Tmp+12),hl
SubCursor:
    ld hl,(Tmp+12)
    ld c,(hl)
    ld b,22
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
    ld b,22
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

PageOptions: .DW @t1, @t2, @t3, 0
@t1: .DB 12, 3
     .ASC "OPTIONS"
     .DB 0
@t2: .DB 7, 10
     .ASC "NOTHING HERE YET."
     .DB 0
@t3: .DB 8, 20
     .ASC "BUTTON 2  BACK"
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
.ENDS
