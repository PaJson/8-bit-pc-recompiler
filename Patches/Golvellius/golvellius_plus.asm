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
;   full-screen pages (placeholders until steps 4 and 6). In our menus button 1 = select,
;   button 2 = back; the original menus keep their behaviour.
;
; Memory: banks 0 and 1 are always in slots 0/1 ($0000-$7FFF; the game never remaps them),
; slot 2 ($8000-$BFFF) is switched by the game (register $FFFF) and also shows the battery RAM
; when $FFFC bit 3 is set. Our code lives in bank 16 (slot 2). Anything that changes slot 2
; (the game's asset loader, going back into the game's code) goes through the stubs in bank 1
; ($7E99-$7FEF, the ROM's only free space): code in slot 2 must never switch slot 2 itself.
; Every bank switch uses a constant (ld a,N / ld ($FFFF),a) and every jump has a fixed target,
; so the PC recompiler can follow all of it statically.

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

; Text: the game's fonts have letters at their ASCII codes, '@' ($40) = space, digits at $DF-$E8
; (font asset $C0 only, i.e. our full-screen pages; the title font has no digits)
.ASCIITABLE
MAP " " = $40
MAP "0" TO "9" = $DF
MAP "." = $5B
MAP "'" = $5C
MAP "!" = $5D
MAP "," = $5E
MAP "?" = $5F
MAP "-" = $CD
.ENDA

; ---------------------------------------------------------------------------------------------
; RAM ($C0A1-$C0FF is never used by the game: RESEARCH.md)
.DEFINE GvRet       $C0A1        ; title re-entry: 0 normal, 1 top menu (OPTIONS), 2 continue menu (SAVE), 3 title wait
.DEFINE GvPage      $C0A2        ; the open page's GvRet value, copied to GvRet only when the page closes (the Reset
                                 ; button restarts at $0405 without clearing RAM: no stale GvRet)
.DEFINE MenuSel     $C100        ; menu selection (the game's own title menus use it too; the cursor routine reads it)

; the game's routines and RAM (bank 0)
.DEFINE WaitFrame   $07C3        ; wait one frame
.DEFINE ReadPad     $0DBB        ; -> $C806 held, $C807 newly pressed
.DEFINE PadNew      $C807        ; bit 0-3 up/down/left/right, 4 button 1, 5 button 2
.DEFINE FillVram    $01CF        ; HL address, BC count, A value
.DEFINE SetVramAddr $01B9        ; VRAM write address HL
.DEFINE DrawString  $02FC        ; HL -> column, row, text, 0 (high tile byte = $C047)
.DEFINE DrawStrings $030F        ; HL -> 0-ended list of string pointers
.DEFINE TileHigh    $C047
.DEFINE MenuCursor  $2899        ; A = tile at column 10, row 20 + 2 * (MenuSel); needs carry clear
.DEFINE ScreenOff   $0D1A
.DEFINE ScreenOn    $0D20
.DEFINE LoadAsset   $0E0B        ; A = asset code -> maps its bank in slot 2, HL = data
.DEFINE UnpackTiles $038F        ; HL data -> VRAM DE
.DEFINE TitleSetup  $2749        ; draws the title screen (then $27A4: music, $27AE: wait for a button)

; ---------------------------------------------------------------------------------------------
; Hooks into the game's code (bank 0)
.BANK 0 SLOT 0
.ORGA $27A4                      ; title: was ld a,2 / ld ($FFFF),a (then music, wait)
.SECTION "hook_title_setup" OVERWRITE
    jp Stub_TitleSetup
    nop
    nop
.ENDS
.ORGA $27F8                      ; title: a button was pressed; was ld hl,$3D00 (NEW GAME / CONTINUE)
.SECTION "hook_title_button" OVERWRITE
    jp Stub_TopMenu
.ENDS
.ORGA $284B                      ; CONTINUE chosen; was ld hl,$28C2 (LAST GAME / PASSWORD CONTINUE)
.SECTION "hook_continue" OVERWRITE
    jp Stub_ContMenu
.ENDS

; ---------------------------------------------------------------------------------------------
; Bank 1 stubs (fixed slot): every switch of slot 2 between the game and our bank 16
.BANK 1 SLOT 1
.ORGA $7E99
.SECTION "gvplus_stubs" FORCE
Stub_TopMenu:                    ; from $27F8
    ld a,16
    ld ($FFFF),a
    jp TopMenu
Stub_ContMenu:                   ; from $284B
    ld a,16
    ld ($FFFF),a
    jp ContMenu
Stub_TitleSetup:                 ; from $27A4: the title screen is drawn
    ld a,(GvRet)
    or a
    jr nz,@ours
    ld a,2                       ; normal start: the original code (music, wait for a button)
    ld ($FFFF),a
    jp $27A9
@ours:
    cp 3
    jr nz,@menu
    xor a                        ; back from our top menu: wait for a button again, music keeps playing
    ld (GvRet),a
    ld a,2
    ld ($FFFF),a
    jp $27AE
@menu:
    ld a,16                      ; back from a page: straight into our menu
    ld ($FFFF),a
    jp TitleReturn
Exit_NewGameMenu:                ; -> the original NEW GAME / CONTINUE menu
    ld a,2
    ld ($FFFF),a
    jp $2806
Exit_Password:                   ; -> the original LAST GAME / PASSWORD CONTINUE menu
    ld a,2
    ld ($FFFF),a
    ld hl,$28C2
    jp $284E
FontToLow:                       ; font asset $C0 -> VRAM $0800 (tiles $40+), back to bank 16
    ld a,$C0
    call LoadAsset
    ld de,$0800
    call UnpackTiles
    ld a,16
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
    .DB 3                         ; step / feature level (3 = title menus)
.ENDS

.SECTION "gvplus_title" FREE

; --- title menus (rows 20 / 22 under the title picture, cursor at column 10, like the game's) ---
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
    ld b,1
    jp ShowPage
@back:
    ld a,3                       ; redraw the title (the copyright lines) and wait for a button
    ld (GvRet),a
    ld sp,$DFF0
    jp TitleSetup

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
    ld b,2
    jp ShowPage
@back:
    call ClearMenuRows
    jp Exit_NewGameMenu

TitleReturn:                     ; the title was redrawn after a page: back into the menu we came from
    ld a,(GvRet)
    ld b,a
    xor a
    ld (GvRet),a
    ld a,1
    ld (MenuSel),a               ; the cursor on the entry that opened the page
    ld a,b
    cp 2
    jp z,ContMenuSel
    jp TopMenuSel

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

; --- full-screen pages: HL = 0-ended list of strings, B = GvRet (which menu to return to) ---
ShowPage:
    ld a,b
    ld (GvPage),a
    push hl
    call ScreenOff
    call FontToLow
    call ClearScreen
    xor a
    ld (TileHigh),a              ; low tile set = font asset $C0
    pop hl
    call DrawStrings
    call ScreenOn
@wait:
    call WaitFrame
    call ReadPad
    ld a,(PadNew)
    and $20                      ; button 2 = back
    jr z,@wait
    ld a,(GvPage)
    ld (GvRet),a
    ld sp,$DFF0
    jp TitleSetup                ; -> Stub_TitleSetup -> TitleReturn

ClearScreen:                     ; name table rows 0-23 = space ($40, low set)
    ld hl,$3800
    di
    call SetVramAddr
    ld bc,32*24
@loop:
    ld a,$40
    out ($BE),a
    xor a
    out ($BE),a
    dec bc
    ld a,b
    or c
    jr nz,@loop
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
@t2: .DB 6, 10
     .ASC "NOTHING HERE YET."
     .DB 0
@t3: .DB 7, 20
     .ASC "BUTTON 2 - BACK"
     .DB 0

PageSave:    .DW @t1, @t2, @t3, @t4, @t5, @t6, @t7, @t8, 0
@t1: .DB 14, 3
     .ASC "SAVE"
     .DB 0
@t2: .DB 10, 7
     .ASC "AUTO"
     .DB 0
@t3: .DB 10, 9
     .ASC "SLOT 1"
     .DB 0
@t4: .DB 10, 11
     .ASC "SLOT 2"
     .DB 0
@t5: .DB 10, 13
     .ASC "SLOT 3"
     .DB 0
@t6: .DB 10, 15
     .ASC "SLOT 4"
     .DB 0
@t7: .DB 10, 17
     .ASC "SLOT 5"
     .DB 0
@t8: .DB 7, 20
     .ASC "BUTTON 2 - BACK"
     .DB 0
.ENDS
