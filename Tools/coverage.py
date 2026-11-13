#!/usr/bin/env python3
"""Function coverage of a recompiled game.

  SMS_COVERAGE=cov1.txt <game exe> <rom> --frames N ...      (one or more runs, any input)
  python -I Tools/coverage.py <slug> cov1.txt [cov2.txt ...] [--list]

Reads Games/<slug>/generated/*_funcs.h (every recompiled function, func_XXXX or func_XXXX_bN)
and the runner's coverage dumps ("BANK OFFSET" per executed function entry), prints how many
functions ran and (with --list) the ones that never ran - the code no test has exercised yet.
"""
import sys, os, re, glob

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    listing = "--list" in sys.argv
    slug, files = args[0], args[1:]
    hdr = glob.glob(os.path.join(ROOT, "Games", slug, "generated", "*_funcs.h"))[0]
    funcs = []
    for m in re.finditer(r"void func_([0-9A-F]{4})(?:_b(\d+))?\(void\);", open(hdr).read()):
        addr = int(m.group(1), 16)
        bank = int(m.group(2)) if m.group(2) else (addr >> 14 if addr < 0xC000 else -1)
        funcs.append((bank, addr))
    hit = set()
    for fn in files:
        for ln in open(fn):
            b, o = ln.split()
            b = int(b); o = int(o, 16)
            hit.add((b, o if b < 0 else o & 0x3FFF))
    key = lambda f: (f[0], f[1] if f[0] < 0 else f[1] & 0x3FFF)
    done = [f for f in funcs if key(f) in hit]
    miss = [f for f in funcs if key(f) not in hit]
    print("%s: %d of %d functions executed (%.1f%%), %d never ran"
          % (slug, len(done), len(funcs), 100.0 * len(done) / max(1, len(funcs)), len(miss)))
    if listing:
        for b, a in sorted(miss, key=lambda f: (f[0], f[1])):
            print("  never: %04X bank %d" % (a, b))

main()
