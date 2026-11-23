"""Write our own Golvellius fix patch (IPS) from the user's ROM, for playing in other emulators / on hardware.

    python -I Tools/sms/gv_make_ips.py <Golvellius ROM> <out.ips> [--base <patch>] [--rom <out.sms>]

Contents (the same data as the recompiled build's options, Common/Golvellius/gv_qol.c):
  MAP FIXES  - SMS Power's "Golvellius SMS Bugfix" bytes: $15CB9 $8E -> $7E (screen C5 gets its own
               room), $15E16 $94 -> $2B (cave graphic of screen H5)
  TEXT FIXES - the 5 certain typos, same length, found by their original bytes: Winkle's "PASSWARD",
               3 x "GOLD" for "FIND" (cave texts $24 $2B $2F), credits "DESGINED"
  checksum   - the header checksum at $7FFA (the Western BIOS checks it on real hardware), recomputed
               over the range the header's size code names
Only DATA, no code: the code-based options (sword hit fix, cave down speed, Dina, save slots, ...)
exist only in the recompiled build.

--base <patch>: build on a ROM that another IPS / BPS patch has changed first (a file or
"<zip>::<member>", e.g. another author's hack; one that already contains the map fixes is fine): our records are made against that ROM, so the checksum record is right for the combination.
Apply the base patch first, then ours. Refused if a record of ours would change a byte the base
patch changed (they would not combine).
--rom <out.sms>: also write the patched ROM.
Nothing here is ROM content stored in the project: the patch is made from the user's own ROM.
"""
import sys, os, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))   # Tools (apply_patch)
import apply_patch

CRC_ORIG = 0xA51376FE
MAP_FIX = [(0x15CB9, 0x8E, 0x7E), (0x15E16, 0x94, 0x2B)]
TYPOS = [  # (lo, hi, space byte, original, fixed) - same table as gv_qol.c
    (0x1C000, 0x20000, 0x00, "PASSWARD",       "PASSWORD"),
    (0x1C000, 0x20000, 0x00, "THOU GOLD THE",  "THOU FIND THE"),
    (0x1C000, 0x20000, 0x00, "GO GOLD MORE",   "GO FIND MORE"),
    (0x1C000, 0x20000, 0x00, "HELP THEE GOLD", "HELP THEE FIND"),
    (0x3C000, 0x40000, 0x40, "DESGINED",       "DESIGNED"),
]
SIZES = {0xA: 0x2000, 0xB: 0x4000, 0xC: 0x8000, 0xD: 0xC000, 0xE: 0x10000, 0xF: 0x20000, 0x0: 0x40000, 0x1: 0x80000, 0x2: 0x100000}

def enc(t, sp): return bytes(sp if c == ' ' else ord(c) for c in t)

def checksum(rom):
    """SMS header checksum: 16-bit sum of the bytes the size code covers, without the header $7FF0-$7FFF"""
    n = SIZES.get(rom[0x7FFF] & 15)
    if n is None: raise SystemExit('unknown size code in the header: %02X' % rom[0x7FFF])
    s = sum(rom[0:0x7FF0])
    if n > 0x8000: s += sum(rom[0x8000:n])
    return s & 0xFFFF

def ips(records):
    out = bytearray(b'PATCH')
    for off, data in records:
        out += off.to_bytes(3, 'big') + len(data).to_bytes(2, 'big') + data
    return bytes(out + b'EOF')

def main(argv):
    args = list(argv); base = rom_out = None
    if '--base' in args: i = args.index('--base'); base = args[i + 1]; del args[i:i + 2]
    if '--rom' in args: i = args.index('--rom'); rom_out = args[i + 1]; del args[i:i + 2]
    if len(args) != 2: print(__doc__); return 2
    src = open(args[0], 'rb').read()
    if zlib.crc32(src) & 0xFFFFFFFF != CRC_ORIG:
        print('not the expected ROM (Golvellius - Valley of Doom (USA, Europe, Brazil), CRC32 %08X)' % CRC_ORIG); return 1
    if checksum(src) != src[0x7FFA] | src[0x7FFB] << 8:
        print('warning: the original header checksum does not match our computation')
    rom = bytearray(src)
    if base:
        p = apply_patch.read_patch(base)
        rom = bytearray(apply_patch.apply_bps(p, bytes(rom), False) if p[:4] == b'BPS1' else apply_patch.apply_ips(p, bytes(rom)))
        base_changed = {o for o in range(min(len(src), len(rom))) if src[o] != rom[o]} | set(range(len(src), len(rom)))
        print('base patch: %d bytes changed' % len(base_changed))
    else:
        base_changed = set()
    before = bytes(rom)
    # map fixes
    for off, old, new in MAP_FIX:
        if rom[off] not in (old, new): print('map fix byte $%05X is %02X (expected %02X): base patch changed it?' % (off, rom[off], old)); return 1
        rom[off] = new
    # typos
    for lo, hi, sp, a, b in TYPOS:
        ea, eb = enc(a, sp), enc(b, sp)
        hits = [o for o in range(lo, hi - len(ea) + 1) if rom[o:o + len(ea)] == ea]
        if len(hits) != 1:
            print('"%s" found %d times (expected once): not applied' % (a, len(hits))); return 1
        rom[hits[0]:hits[0] + len(eb)] = eb
    # checksum
    c = checksum(rom); rom[0x7FFA] = c & 0xFF; rom[0x7FFB] = c >> 8
    # records = runs of changed bytes against the (base-patched) ROM
    recs = []; o = 0
    while o < len(rom):
        if rom[o] != before[o]:
            s = o
            while o < len(rom) and rom[o] != before[o] and o - s < 0xFFFF: o += 1
            recs.append((s, bytes(rom[s:o])))
        else: o += 1
    clash = [r for r in recs if any(x in base_changed for x in range(r[0], r[0] + len(r[1]))) and r[0] != 0x7FFA]
    if clash:
        print('our records overlap the base patch at: ' + ', '.join('$%05X' % r[0] for r in clash)); return 1
    open(args[1], 'wb').write(ips(recs))
    print('%s: %d records (%s)' % (args[1], len(recs), ', '.join('$%05X+%d' % (r[0], len(r[1])) for r in recs)))
    if rom_out:
        if os.path.abspath(rom_out) == os.path.abspath(args[0]): print('refusing to overwrite the input ROM'); return 1
        open(rom_out, 'wb').write(rom)
        print('%s: CRC32 %08X' % (rom_out, zlib.crc32(rom) & 0xFFFFFFFF))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
