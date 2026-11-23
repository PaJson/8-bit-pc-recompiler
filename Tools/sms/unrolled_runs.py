"""Find unrolled block-I/O runs (Duff's-device code) in a ROM: a unit of 2..8 bytes, made only of single-byte
register ops and ED-prefixed block ops (LDI/LDD/INI/IND/OUTI/OUTD), repeated >= 8 times. Games enter these at
computed offsets, so every instruction boundary inside is a function entry. Returns sorted entry addresses."""
BLOCK = {0xA0, 0xA8, 0xA2, 0xAA, 0xA3, 0xAB}
SAFE1 = set([0x00, 0x03,0x04,0x05,0x0B,0x0C,0x0D,0x13,0x14,0x15,0x1B,0x1C,0x1D,0x23,0x24,0x25,0x2B,0x2C,0x2D,
             0x33,0x3B,0x3C,0x3D]) | set(range(0x40,0x80)) - {0x76} | set(range(0x80,0xC0))

def parse(unit):
    """instruction starts within unit, or None if it contains anything else"""
    i, starts, has_blk = 0, [], False
    while i < len(unit):
        starts.append(i)
        if unit[i] == 0xED:
            if i + 1 >= len(unit) or unit[i+1] not in BLOCK: return None
            has_blk = True; i += 2
        elif unit[i] in SAFE1: i += 1
        else: return None
    return starts if has_blk else None

def find_runs(rom, limit=0x8000, min_reps=8):
    rom = rom[:limit]; out = {}
    for p in range(2, 9):
        i = 0
        while i + p * min_reps <= len(rom):
            j = i
            while j + p < len(rom) and rom[j] == rom[j + p]: j += 1
            end = j + p                       # run covers [i, end)
            if end - i >= p * min_reps:
                s = i
                if s > 0 and rom[s-1] == 0xED: s += 1          # don't start inside an ED pair
                u = bytes(rom[s:s+p]); st = parse(u)
                if st is None:                                 # unit phase may differ: try other phases
                    for ph in range(p):
                        u = bytes(rom[s+ph:s+ph+p]); st = parse(u)
                        if st is not None: s += ph; break
                if st is not None:
                    for a in range(s, end - p + 1, p):
                        for o in st: out[a + o] = 1
                    out[s + ((end - s) // p) * p] = 1      # first byte after the run (usually the `ret`): a zero-length entry
                i = end
            else: i += 1
    return sorted(out)

if __name__ == "__main__":
    import sys
    r = open(sys.argv[1], "rb").read(); e = find_runs(r)
    print(len(e), "entries;", " ".join("%04X" % x for x in e[:6]), "...")
