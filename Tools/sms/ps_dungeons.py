#!/usr/bin/env python3
"""Phantasy Star (SMS): which dungeon floors / parts of floors can be reached, and from where.

  python -I Tools/sms/ps_dungeons.py "<ROM>" [--all]

Rev A / USA addresses (the ps1disasm labels). Reads:
  LABEL_DC1  ($0DC1): map -> tileset (24 maps)
  LABEL_DF1  ($0DF1): 14-byte record per tileset; +12 = its entrance list (bank 3): 6-byte records
             y, x, type, a, b, c; type $0A = dungeon (floor a, position b, facing c) (LABEL_78BD),
             $08 = map (LABEL_787B record a = map, b = y, c = x), $0C = interaction; $FF ends.
             The lists overlap (a later tileset's list starts inside an earlier one).
  B15_DungeonLayouts (bank 15): 128 bytes per floor, a nibble per cell (0 floor, 1 wall, 2 up,
             3 down, 4-6 doors, 7 fake wall, 8 object / pit, $A-$E = exits (stairs / doors that
             leave the dungeon)). Stairs: floor +1 / -1 at the same position; a pit (8 without
             an object in B03_ObjectData): floor - 1.
  Scripted entries (code, not tables; every `ld (Dungeon_position),hl` in ps1.asm): floor $00 at
             $DE (LABEL_792A, map 5 event), $15 at $9C (Drasgow, mode $0A), $21 at $E6 (LABEL_4C70:
             $22/$E6 then LABEL_661C = the pit "fall", floor - 1), $3C at $F7 (ending: $3D/$F7 then
             the fall = $3C, the credits dungeon).
A floor's walkable area is split into 4-connected parts (walls = 1 block). A part is reached if an
entrance or scripted entry lands in it, or stairs / a pit of a reached part lead into it.
"""
import sys, re

def main():
    rom = open(sys.argv[1], "rb").read()
    show_all = "--all" in sys.argv
    dc1 = rom[0x0DC1:0x0DC1 + 24]
    def b3(p): return 3 * 0x4000 + (p - 0x8000)
    # entrance lists per tileset
    starts = {}
    for ts in range(11):
        r = rom[0x0DF1 + 14 * ts:0x0DF1 + 14 * ts + 14]
        p = r[12] | r[13] << 8
        if 0x8000 <= p < 0xC000: starts[ts] = b3(p)
    entries = {}                                   # file offset -> (tileset, record)
    for ts, off in sorted(starts.items(), key=lambda t: t[1]):
        o = off
        while rom[o] != 0xFF:
            entries[o] = (ts, rom[o:o + 6])        # later (inner) list start overrides
            o += 6
    # the owner of a record = the list that starts closest before it
    owner = {}
    for o in entries:
        best = max((s for s in starts.values() if s <= o), default=None)
        owner[o] = [t for t, s in starts.items() if s == best]
    # layouts: find B15_DungeonLayouts by the first floor's bytes (Medusa Cave)
    lay0 = bytes.fromhex("11111111111111111111A000010111A1")
    lo = rom.find(lay0, 15 * 0x4000, 16 * 0x4000)
    if lo < 0: sys.exit("dungeon layouts not found")
    NF = 61
    def cells(f):
        b = rom[lo + 128 * f: lo + 128 * f + 128]
        return [n for x in b for n in (x >> 4, x & 15)]
    # objects (chests / events): B03_ObjectData, found by its code pattern (see ps_automap.c)
    m = re.search(rb"\x21\xFF\xFF\x36\x03\x21(..)\x11\x06\x00", rom[:0x8000], re.S)
    objs = set()
    if m:
        o = b3(m.group(1)[0] | m.group(1)[1] << 8)
        while rom[o] != 0xFF:
            objs.add((rom[o], rom[o + 1])); o += 7      # floor, pos, flag ptr, kind, data
    names = ["Medusa Cave|Gothic Tunnel|Baya Malay Pass", "Triada Prison", "Camineet Warehouse|Abion Warehouse",
        "Medusa Tower B1F", "Medusa Tower 1F", "Medusa Tower 2F", "Medusa Tower 3F", "Medusa Tower 4F",
        "Medusa Tower 5F|Baya Malay Tower B3F", "Medusa Tower 6F|Baya Malay Tower B2F", "Medusa Tower 7F|Baya Malay Tower B1F",
        "Baya Malay Tower 1F", "Baya Malay Tower 2F", "Baya Malay Tower 3F", "Baya Malay Tower 4F", "Baya Malay Tower 5F",
        "Iala Cave B3F|Naula Cave B3F", "Iala Cave B2F|Naula Cave B2F", "Iala Cave B1F|Naula Cave B1F", "Iala Cave 1F|Naula Cave 1F",
        "Baya Malay Prison", "Drasgow Warehouse|Dr. Mad's Lab", "Bortevo Pass",
        "Island Tower 1F", "Island Tower 2F", "Island Tower 3F", "Island Tower 4F", "Island Tower 5F",
        "Maharu Cave B2F", "Maharu Cave B1F", "Maharu Cave 1F",
        "Governor's Palace B3F", "Governor's Palace B2F", "Governor's Palace B1F|Path to the Governor",
        "Tarzimal Cave B2F", "Tarzimal Cave B1F", "Tarzimal Cave 1F", "Casba Cave B2F", "Casba Cave B1F", "Casba Cave 1F",
        "Skure Tunnel|Twintown Tunnel", "Fort Guaron B1F", "Fort Guaron 1F",
        "Corona Tower 1F", "Corona Tower 2F", "Corona Tower 3F", "Corona Tower 4F",
        "Dezoris Path 1|Dezoris Path 5 B1F|Laconian Shield Cave B5F", "Dezoris Path 2|Dezoris Path 5 1F|Laconian Shield Cave B4F",
        "Dezoris Path 4|Laconian Shield Cave B3F", "Dezoris Path 3|Laconian Shield Cave B2F",
        "Laconian Shield Cave B1F", "Laconian Shield Cave 1F",
        "Air Castle B4F", "Air Castle B3F", "Air Castle B2F", "Air Castle B1F", "Air Castle 1F", "Aeroprism Cave",
        "Credits Dungeon B1F", "Credits Dungeon 1F"]
    # parts of each floor
    part = {}
    for f in range(NF):
        c = cells(f); lab = [-1] * 256; n = 0
        for p in range(256):
            if c[p] == 1 or lab[p] >= 0: continue
            st = [p]; lab[p] = n
            while st:
                q = st.pop(); y, x = q >> 4, q & 15
                for d in ((y > 0, -16), (y < 15, 16), (x > 0, -1), (x < 15, 1)):
                    if d[0] and c[q + d[1]] != 1 and lab[q + d[1]] < 0: lab[q + d[1]] = n; st.append(q + d[1])
            n += 1
        part[f] = (lab, n, c)
    # seeds
    seeds = []                                     # (floor, pos, how)
    tsname = {0: "Palma", 1: "Motavia", 2: "Dezoris", 3: "Dezoris"}
    for o, (ts, r) in sorted(entries.items()):
        if r[2] == 0x0A:
            ts0 = owner[o][0]
            where = tsname.get(ts0, "town tileset %d" % ts0)
            seeds.append((r[3], r[4], "entrance on %s at y=%02X x=%02X (screen %d,%d), facing %d" % (where, r[0], r[1], r[1] >> 4, (r[0] >> 4) & 7, r[5])))
    for f, p, why in ((0x00, 0xDE, "script LABEL_792A (map 5, $C2E5 = $4C)"), (0x15, 0x9C, "script (Drasgow, item $33)"),
                      (0x21, 0xE6, "script LABEL_4C70 (Governor, $C516; $22/$E6 then a fall = $21)"),
                      (0x3C, 0xF7, "ending (credits; $3D/$F7 then a fall = $3C)")):
        seeds.append((f, p, why))
    reached = {}                                   # (floor, part) -> how
    work = []
    for f, p, why in seeds:
        if f >= NF: continue
        k = (f, part[f][0][p])
        if k not in reached: reached[k] = why; work.append(k)
    while work:
        f, pt = work.pop()
        lab, n, c = part[f]
        for p in range(256):
            if lab[p] != pt: continue
            t = c[p]
            for tf, why in ((f + 1, "stairs up from %02X" % f) if t == 2 else (None, None),
                            (f - 1, "stairs down from %02X" % f) if t == 3 else (None, None),
                            (f - 1, "pit on %02X" % f) if t == 8 and (f, p) not in objs else (None, None)):
                if tf is None or not (0 <= tf < NF): continue
                k = (tf, part[tf][0][p])
                if part[tf][2][p] == 1: continue   # lands in a wall: not a real link
                if k not in reached: reached[k] = why + " pos %02X" % p; work.append(k)
    print("B15_DungeonLayouts at $%05X, %d entrance records, %d objects" % (lo, len(entries), len(objs)))
    for f in range(NF):
        lab, n, c = part[f]
        sizes = [lab.count(i) for i in range(n)]
        line = []
        for i in range(n):
            if sizes[i] < 2 and not show_all: continue
            k = (f, i)
            line.append("   part %d (%d cells): %s" % (i, sizes[i], reached.get(k, "*** NOT REACHED ***")))
        print("%02X %s" % (f, names[f] if f < len(names) else "?"))
        for l in line: print(l)

main()
