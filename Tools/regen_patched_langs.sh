#!/bin/sh
# The other languages of Retranslation 2.6.1 (PSRP, SMS Power): French, French extended ("with additional
# lore"), Catalan, German, Spanish, Latin, Brazilian Portuguese. Like Tools/regen_patched.sh (English): the
# patched ROMs are made from the official zips with our own Tools/apply_patch.py (base = the original Japan
# ROM, CRC checked by BPS), then the game folders are generated.
#
# Per language (found by static analysis, 2026-11-17): the code is the English build's with other
# addresses (banks 0/1 differ from EN 2.6.1 in ~1300-1400 bytes, as EN literal does), so the same three
# facts as for English, at other places:
#   * options-menu table $33B8: 6 entries in every language.
#   * text decompressor `7E 23 E5 6F 26 xx 6E 24 E9` (bank 30 in every language): a 256-byte low-byte
#     table at $xx00 of bank 30, targets in page xx+1.
#   * the routine reached only by tail jumps (EN bank 30 $612B; bytes `CD .. .. FE 64 28 08 FE 66 C2 ..
#     .. CD 15 39 FE 69 20 08 CD 15 39 32 D3`): the patch packs it into free space, so its bank differs
#     per language (FR bank 30, CA / FR extended bank 1, DE / LA bank 2, ES bank 15, PT-BR bank 22;
#     always a slot-1 address).
cd "$(dirname "$0")/.." || exit 1
D="ROMs/Patches/Phantasy Star/Retranslation 2.6.1 (PSRP, SMS Power)"
J="ROMs/Phantasy Star (Japan).sms"
# zip-language : patch name : name in the ROM file / title : slug : decompressor page : tail routine addr:bank
for x in "FR:fr:FR:ps-ja-fr261:4F:0x6374:30" \
         "FR:fr-extended:FR extended:ps-ja-fr261-extended:66:0x42DC:1" \
         "ca:ca:CA:ps-ja-ca261:4F:0x42BB:1" \
         "de:de:DE:ps-ja-de261:54:0x7D6E:2" \
         "es:es:ES:ps-ja-es261:54:0x7DEE:15" \
         "la:la:LA:ps-ja-la261:64:0x7E60:2" \
         "pt-br:pt-br:PT-BR:ps-ja-ptbr261:4F:0x7A3D:22"; do
  IFS=: read z p n slug pg ta tb <<EOF
$x
EOF
  rom="Phantasy Star (Japan) [Retranslation 2.6.1 $n].sms"
  python -I Tools/apply_patch.py "$D/PhantasyStar-SMS-$z-2.6.1.zip::ps1jert.$p.bps" "$J" "ROMs/Patched/$rom" || exit 1
  page=$(printf '0x%02X' $((0x$pg + 1)))
  python -I Tools/mkgame.py "$slug" "Patched/$rom" "Phantasy Star (Retranslation 2.6.1 $n)" \
      --auto-tables --byte-table "0x${pg}00:30:256:$page" --table-count 0x33B8=6 --banked "$ta:$tb" --qol-hooks || exit 1
done
