#!/bin/sh
# Regenerate the game folders for the patched Japanese ROMs (ROMs/Patched/*, made with flips from ROMPatches/).
# Patch-specific facts (found by static analysis of the patched ROMs, see CLAUDE.md "Patched ROMs"):
#   * Retranslation 2.5 adds an options-menu entry: the menu table at $33B8 has 6 entries (stock: 5).
#   * its decompressor (bank 29, slot 1) does `ld a,(hl) ; ld l,(table+a) ; inc h ; jp (hl)`:
#     a 256-byte low-byte table in bank 29 ($5200 EN / $5100 literal), targets in the next page.
#   * $67E6 (EN) / $655E (literal) is routine code inside bank 29 (slot 1) reached by tail jumps.
cd "$(dirname "$0")/.." || exit 1
python -I Tools/mkgame.py ps-ja-en25 "Patched/Phantasy Star (Japan) [Retranslation 2.5 EN].sms" "Phantasy Star (Retranslation 2.5 EN)" \
    --auto-tables --byte-table 0x5200:29:256:0x53 --table-count 0x33B8=6 --banked 0x67E6:29
python -I Tools/mkgame.py ps-ja-en25-literal "Patched/Phantasy Star (Japan) [Retranslation 2.5 EN literal].sms" "Phantasy Star (Retranslation 2.5 EN literal)" \
    --auto-tables --byte-table 0x5100:29:256:0x52 --table-count 0x33B8=6 --banked 0x655E:29
python -I Tools/mkgame.py ps-ja-smspower "Patched/Phantasy Star (Japan) [SMSPower Retranslation 2.01 New Sprites].sms" "Phantasy Star (SMSPower Retranslation 2.01)" \
    --auto-tables
