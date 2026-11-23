#!/bin/sh
# Make the patched Japanese ROMs from the official release zip (ROMs/Patches/...) and regenerate their game
# folders. Retranslation 2.6.1 (PSRP, SMS Power), English and English literal; base = the original Japan ROM
# (the BPS patches check its CRC32).
# Patch-specific facts (found by static analysis of the patched ROMs, see CLAUDE.md "Patched ROMs"):
#   * the retranslation adds an options-menu entry: the menu table at $33B8 has 6 entries (stock: 5).
#   * its text decompressor does `ld a,(hl) ; inc hl ; push hl ; ld l,a ; ld h,TABLE>>8 ; ld l,(hl) ; inc h ;
#     jp (hl)`: a 256-byte low-byte table, targets in the next page. 2.6.1 EN: bank 30 $4E00 (page $4F);
#     2.6.1 literal: bank 29 $7800 (page $79). (2.5: bank 29 $5200 / $5100.)
#   * a routine in bank 30 reached only by tail jumps (`call ... ; cp $64 ; jr z ; cp $66 ; jp nz ...`):
#     2.6.1 EN $612B, literal $5377 (2.5: bank 29 $67E6 / $655E). Found as the one dispatch miss of a
#     random-input soak; registered with --banked.
# To find these again for a new release: search the patched ROM for the decompressor bytes
# 7E 23 E5 6F 26 xx 6E 24 E9 (xx = table page), soak it (Tools/lockstep.sh) and read dispatch_misses.log.
cd "$(dirname "$0")/../.." || exit 1
Z="ROMs/sms/Patches/Phantasy Star/Retranslation 2.6.1 (PSRP, SMS Power)/PhantasyStar-SMS-EN-2.6.1.zip"
J="ROMs/sms/Phantasy Star (Japan).sms"
python -I Tools/apply_patch.py "$Z::ps1jert.en.bps"      "$J" "ROMs/sms/Patched/Phantasy Star (Japan) [Retranslation 2.6.1 EN].sms" || exit 1
python -I Tools/apply_patch.py "$Z::ps1jert.literal.bps" "$J" "ROMs/sms/Patched/Phantasy Star (Japan) [Retranslation 2.6.1 EN literal].sms" || exit 1
python -I Tools/mkgame.py ps-ja-en261 "Patched/Phantasy Star (Japan) [Retranslation 2.6.1 EN].sms" "Phantasy Star (Retranslation 2.6.1 EN)" \
    --auto-tables --byte-table 0x4E00:30:256:0x4F --table-count 0x33B8=6 --banked 0x612B:30 --qol-hooks
python -I Tools/mkgame.py ps-ja-en261-literal "Patched/Phantasy Star (Japan) [Retranslation 2.6.1 EN literal].sms" "Phantasy Star (Retranslation 2.6.1 EN literal)" \
    --auto-tables --byte-table 0x7800:29:256:0x79 --table-count 0x33B8=6 --banked 0x5377:30 --qol-hooks
