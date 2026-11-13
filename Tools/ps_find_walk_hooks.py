#!/usr/bin/env python3
"""Find the Phantasy Star "walk speed x2" hook sites in any PS ROM and print the [insn_hooks] block.

  python -I Tools/ps_find_walk_hooks.py "<ROM file in ROMs/ or a path>"

The six sites (Rev A addresses, see Common/PhantasyStar/ps_qol.c) are located by byte patterns taken
from the original code; '..' bytes are wildcards (absolute jump targets, which move between versions).
Every pattern must match exactly once in $0000-$7FFF, otherwise nothing is printed (and the game is
better left without the hooks than with wrong ones). The 2.5 retranslation rewrote this code (it has
its own walk-speed option), so it is expected not to match.
"""
import sys, os, re

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
# name, pattern (hex, '..' = any byte), hook offset in pattern, resume offset (relative to hook)
SITES = [
    ("ps_walk_frames",   "3a 0e c3 b7 3e 0f 28 02 3e 07 32 65 c2 11 01 00 3a 0e c3 b7 28 01 1c", 0, 13),
    ("ps_walk_pixels",   "3a 0e c3 b7 3e 0f 28 02 3e 07 32 65 c2 11 01 00 3a 0e c3 b7 28 01 1c", 13, 10),
    ("ps_walk_animcp",   "fe 0f c2 .. .. 79 b7 c2 .. .. fd 7e 12 fd 77 13", 0, 2),
    ("ps_walk_animtmr",  "fd 36 0e 07 fd 7e 12 87 87 47", 0, 4),
    ("ps_walk_follow_x", "b7 20 01 3d fd 86 02 fd 77 02 c9 d6 02 20 01 3d fd 86 04", 0, 10),
    ("ps_walk_follow_y", "b7 20 01 3d fd 86 02 fd 77 02 c9 d6 02 20 01 3d fd 86 04", 11, 11),
]

def find(rom, pat):
    rx = re.compile(b"".join(b"." if t == ".." else re.escape(bytes([int(t, 16)])) for t in pat.split()), re.S)
    return [m.start() for m in rx.finditer(rom[:0x8000])]

def main():
    p = sys.argv[1]
    if not os.path.isfile(p): p = os.path.join(ROOT, "ROMs", p)
    rom = open(p, "rb").read()
    rows = []
    for name, pat, off, res in SITES:
        hits = find(rom, pat)
        if len(hits) != 1:
            print("# %s: pattern found %d times - no hooks for this ROM" % (name, len(hits))); return 1
        rows.append((hits[0] + off, name, hits[0] + off + res))
    print('[insn_hooks]   # walk speed x2 (Tools/ps_find_walk_hooks.py)')
    print("addr   = [%s]" % ", ".join("0x%04X" % r[0] for r in rows))
    print("func   = [%s]" % ", ".join('"%s"' % r[1] for r in rows))
    print("resume = [%s]" % ", ".join("0x%04X" % r[2] for r in rows))
    return 0

sys.exit(main())
