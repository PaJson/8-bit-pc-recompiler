; Golvellius Plus - our own ROM patch for Golvellius - Valley of Doom (USA, Europe, Brazil)
; (No-Intro, 256 KB, CRC32 A51376FE). Built by Tools/gv_build_patch.py with WLA-DX
; (External/wla-dx): the user's ROM, padded to 512 KB with $FF, is the background image;
; this file only says what changes. No ROM content is stored here.
;
; Step 1 (2026-11-16): the ROM grows to 512 KB (banks 16-31 = new space), the header says
; 512 KB (size code $1) so the checksum covers the whole ROM, and bank 16 starts with an ID
; block. Nothing the game runs is changed yet.
;
; Memory: banks 0 and 1 are always in slots 0/1 ($0000-$7FFF; the game never remaps them),
; slot 2 ($8000-$BFFF) is switched by the game (register $FFFF) and also shows the battery RAM
; when $FFFC bit 3 is set. So new code in banks 16+ runs in slot 2, and code that touches the
; battery RAM must run from banks 0/1 (bank 1 has the ROM's only free space: $7E99-$7FEF).

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

; ---------------------------------------------------------------------------------------------
; Bank 16: ID block (lets our PC build and tools see which patch version a ROM has)
.BANK 16 SLOT 2
.ORG $0000
.SECTION "gvplus_id" FORCE
GvPlusId:
    .DB "GOLVELLIUS PLUS", 0     ; 16 bytes
    .DB 1                         ; patch format version
    .DB 1                         ; step / feature level (1 = 512 KB base only)
.ENDS
