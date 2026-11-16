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
;   full-screen pages (placeholders until steps 4 and 6). Button 1 = select, button 2 = back, also
;   in the two original menus, now submenus (originally both buttons selected).
;   Nothing of the title is ever reloaded (the title's unpacking runs with the frame interrupt off,
;   which would freeze the music): the menus only replace the two text rows under the picture
;   (saved to RAM and written back when going back to the title), and a page is a second name
;   table at $3000 in tiles the title does not use (the font's kana), shown by switching VDP
;   register 2; its digits are unpacked from the game's font into unused tiles.
;
; Memory: banks 0 and 1 are always in slots 0/1 ($0000-$7FFF; the game never remaps them),
; slot 2 ($8000-$BFFF) is switched by the game (register $FFFF) and also shows the battery RAM
; when $FFFC bit 3 is set. Our code lives in bank 16 (slot 2). Anything that changes slot 2
; (the game's asset loader, going back into the game's code) goes through the stubs in bank 1
; ($7E99-$7FEF, the ROM's only free space): code in slot 2 must never switch slot 2 itself.
; Every bank switch uses a constant (ld a,N / ld ($FFFF),a) and every jump has a fixed target,
; so the PC recompiler can follow all of it statically.
;
; Title VRAM (RESEARCH.md): low tiles $00-$F7 = the picture; high tiles 256+$01..$3D = picture +
; copyright lines, 256+$40..$62 = the font's letters / cursor (our menu texts), 256+$63..$BF unused
; (the font's kana); name table $3800. Pages: name table $3000-$35FF (256+$80..$AF), digits
; 256+$B0..$B9 ($3600-$373F).

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

; Text: the title font (high tile set) has the letters at their ASCII codes, '@' ($40) = space,
; '. ' ! , ?' at $5B-$5F; our digits sit at $B0-$B9 (unpacked by Stub_Digits).
.ASCIITABLE
MAP " " = $40
MAP "0" TO "9" = $B0
MAP "." = $5B
MAP "'" = $5C
MAP "!" = $5D
MAP "," = $5E
MAP "?" = $5F
.ENDA

; ---------------------------------------------------------------------------------------------
; RAM ($C0A1-$C0FF is never used by the game; $D178-$DAE9 not while the title runs: RESEARCH.md)
.DEFINE MenuSel     $C100        ; menu selection (the game's own title menus use it too; the cursor routine reads it)
.DEFINE TitleRows   $D200        ; 256 bytes: name table rows 20-23 of the title (the copyright lines)

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

; ---------------------------------------------------------------------------------------------
; Hooks into the game's code (bank 0)
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
Exit_Password:                   ; -> the original LAST GAME / PASSWORD CONTINUE menu
    ld a,2
    ld ($FFFF),a
    ld hl,$28C2
    jp $284E

; The digit tiles of the game's font (asset $C0, packed as 4 bit-planes of runs: n < $80 = n
; bytes, n >= $80 = the next byte n & $7F times, 0 = end of the plane; its first tile is $40, the
; digits are tiles $DF-$E8) -> VRAM $3600 (tiles 256+$B0..$B9). Only those 80 bytes per plane are
; written; nothing else of the font is unpacked. Back to bank 16 at the end.
Stub_Digits:
    ld a,$C0
    call LoadAsset               ; bank 12 in slot 2, HL = the packed font
    ld c,0                       ; plane
@plane:
    ld de,0                      ; byte number in this plane (8 per tile)
@next:
    ld a,(hl)
    inc hl
    or a
    jr z,@endplane
    jp m,@run
    ld b,a
@lit:
    ld a,(hl)
    inc hl
    call @put
    djnz @lit
    jr @next
@run:
    and $7F
    ld b,a
    ld a,(hl)
    inc hl
@rep:
    call @put
    djnz @rep
    jr @next
@endplane:
    inc c
    ld a,c
    cp 4
    jr nz,@plane
    ld a,16
    ld ($FFFF),a
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
.ENDS

; ---------------------------------------------------------------------------------------------
; Bank 16
.BANK 16 SLOT 2
.ORG $0000
.SECTION "gvplus_id" FORCE
GvPlusId:
    .DB "GOLVELLIUS PLUS", 0     ; 16 bytes
    .DB 1                         ; patch format version
    .DB 3                         ; step / feature level (3 = title menus)
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
    ld hl,PageSave
    call ShowPage
    ld a,1
    ld (MenuSel),a
    jr ContMenuSel
@back:
    call ClearMenuRows
    jp Exit_NewGameMenu

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

; --- full-screen pages: HL = 0-ended list of strings (column, row, text, 0); returns on button 2 ---
ShowPage:
    push hl
    call FadeOut
    call ScreenOff
    call Stub_Digits
    ld hl,$3000                  ; the page's name table: all spaces (high set)
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
    call PageString
    pop hl
    jr @string
@shown:
    ld a,$FD                     ; name table at $3000
    call SetNameTable
    call ScreenOn
    call FadeIn                  ; the title palette ($C9C0)
@wait:
    call WaitFrame
    call ReadPad
    ld a,(PadNew)
    and $20                      ; button 2 = back
    jr z,@wait
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

PageString:                      ; HL -> column, row, text, 0 into the page's name table (display off)
    ld a,(hl)
    inc hl
    add a,a
    ld e,a
    ld d,0                       ; DE = column * 2
    ld a,(hl)
    inc hl
    push hl
    ld l,a
    ld h,0
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl                    ; row * 64
    add hl,de
    ld de,$3000
    add hl,de
    di
    call SetVramAddr
    pop hl
@char:
    ld a,(hl)
    inc hl
    or a
    jr z,@end
    out ($BE),a
    ld a,1                       ; high tile set, palette 0
    out ($BE),a
    jr @char
@end:
    ei
    ret

; --- texts (column, row, text, 0) ---
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

PageSave:    .DW @t1, @t2, @t3, @t4, @t5, @t6, @t7, @t8, 0
@t1: .DB 14, 3
     .ASC "SAVE"
     .DB 0
@t2: .DB 12, 7
     .ASC "AUTO"
     .DB 0
@t3: .DB 12, 9
     .ASC "SLOT 1"
     .DB 0
@t4: .DB 12, 11
     .ASC "SLOT 2"
     .DB 0
@t5: .DB 12, 13
     .ASC "SLOT 3"
     .DB 0
@t6: .DB 12, 15
     .ASC "SLOT 4"
     .DB 0
@t7: .DB 12, 17
     .ASC "SLOT 5"
     .DB 0
@t8: .DB 8, 20
     .ASC "BUTTON 2  BACK"
     .DB 0
.ENDS
