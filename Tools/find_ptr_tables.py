"""Find jump tables used through a `GetPtr` helper: helper = `add a,a ; ld e,a ; ld d,0 ; add hl,de ; ld a,(hl) ; inc hl ;
ld h,(hl) ; ld l,a ; ret` (87 5F 16 00 19 7E 23 66 6F C9), sites = `ld hl,TBL ; call HELPER ; jp (hl)` (21 lo hi CD hl hh E9).
Prints game.toml [jump_tables] lines (fixed-slot tables only, bank -1). Entry count = run of plausible code pointers,
ended by the next table / non-pointer. Usage: python -I Tools/find_ptr_tables.py "<ROM file in ROMs/>" """
import re, sys, os
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
r = open(os.path.join(ROOT, "ROMs", sys.argv[1]), "rb").read()
SIG = bytes.fromhex("875f1600197e23666fc9")
helpers = [m.start() for m in re.finditer(re.escape(SIG), r[:0x4000])]
w = lambda x: r[x] | r[x + 1] << 8
tables = {}
for h in helpers:
    for m in re.finditer(re.escape(bytes([0xCD, h & 255, h >> 8, 0xE9])), r[:0x8000]):
        i = m.start()
        if r[i - 3] != 0x21: continue
        t = w(i - 2)
        if 0x0040 <= t < 0x8000 and t not in tables:
            tables[t] = h
starts = sorted(tables)
out = []
for k, t in enumerate(starts):
    n = 0
    if not (0x0040 <= w(t) < 0x8000): t += 2          # 1-based index: entry 0 is not a pointer
    lim = starts[k + 1] if k + 1 < len(starts) else 0x8000
    while t + 2 * n + 1 < len(r) and t + 2 * n < lim and 0x0040 <= w(t + 2 * n) < 0x8000: n += 1
    out.append((t, n))
print("# helpers at", [hex(h) for h in helpers])
print("addr  = [%s]" % ", ".join("0x%04X" % t for t, _ in out))
print("bank  = [%s]" % ", ".join("-1" for _ in out))
print("count = [%s]" % ", ".join(str(n) for _, n in out))
