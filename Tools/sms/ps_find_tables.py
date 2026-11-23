#!/usr/bin/env python3
"""Find the computed-jump tables of a Phantasy Star (SMS) ROM by matching Rev A's code idioms.

  python -I Tools/sms/ps_find_tables.py "<ROM path under ROMs/, e.g. sms/Phantasy Star (Japan).sms>"      -> prints a [jump_tables] block

Idioms (all verified against Rev A, ps1disasm-master):
  * GetPtrAndJump helper (jp (hl) via HL+2*A):   87 85 6F 8C 95 67 7E 23 66 6F E9
    call sites: `ld hl,TBL ; call/jp/jp cc GetPtrAndJump` -- paired with Rev A's sites by order,
    so each gets Rev A's table offset (+0/+2) and entry count.
  * VInt mode table: `ld a,($C208) ; and $1F ; ld hl,TBL ; add a,l ...`   (12 entries)
  * `$4769` helper (`ld de,TBL-2 ; call helper`):   6F 26 00 29 19 7E 23 66 6F E9   (182 entries)
  * Sound driver tables inside the sound bank: `ld hl,TBL ; add hl,bc ; ld a,(hl)` idioms (5 and 18 entries).
Entry counts come from Rev A (the games are the same program); a wrong count shows up as dispatch
misses (too few) in the soak test.
"""
import re, sys, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
REV_A = "sms/Phantasy Star (USA, Europe) (Rev A).sms"
GPJ_SIG = bytes.fromhex("87856f8c95677e23666fe9")
HELP_SIG = bytes.fromhex("6f26002919" "7e23666fe9")

# Rev A reference: (ld-hl operand, table address used in game.toml, entry count) per GetPtrAndJump site, in ROM order.
REVA_SITES = [(0x00BE, 0x00BE, 20), (0x1912, 0x1912, 5), (0x1A8A, 0x1A8A, 18), (0x12C4, 0x12C4, 8), (0x1A66, 0x1A66, 18),
              (0x1C97, 0x1C97, 5), (0x1DFE, 0x1DFE, 19), (0x21FB, 0x21FB, 3), (0x220A, 0x220A, 64), (0x3C2A, 0x3C2A, 20),
              (0x5825, 0x5827, 22), (0x63B6, 0x63B8, 11)]
REVA_COND = (0x621D, 0x621F, 5)     # `ld hl,$621D ; jp nz,GetPtrAndJump`


def rd(name):
    return open(os.path.join(ROOT, "ROMs", name), "rb").read()


def gpj_sites(r, g):
    """all `ld hl,X ; call/jp/jp cc g` in the fixed area, in ROM order -> [(pos, X)]"""
    pat = rb"\x21(..)[\xCD\xC3\xC2\xCA\xD2\xDA\xE2\xEA\xF2\xFA\xC4\xCC\xD4\xDC\xE4\xEC\xF4\xFC]" + bytes([g & 255, g >> 8])
    return [(m.start(), int.from_bytes(m.group(1), "little")) for m in re.finditer(pat, r[:0x8000], re.S)]


def main():
    name = sys.argv[1]
    r = rd(name)
    ref = rd(REV_A)
    g_ref = ref.find(GPJ_SIG)
    g = r.find(GPJ_SIG)
    if g < 0 or g > 0x7FFF:
        sys.exit("GetPtrAndJump helper not found")
    ref_sites = gpj_sites(ref, g_ref)
    sites = gpj_sites(r, g)
    # reference: unconditional+conditional sites in Rev A in ROM order; map each to (offset, count)
    ref_meta = {}
    for a, c in zip([s[1] for s in ref_sites], range(len(ref_sites))):
        pass
    table = {x: (t, n) for x, t, n in REVA_SITES + [REVA_COND]}
    out = []   # (addr, bank, count, note)
    if len(sites) != len(ref_sites):
        print("# WARNING: %d GetPtrAndJump sites vs %d in Rev A" % (len(sites), len(ref_sites)))
    for (pos_r, x_r), (pos, x) in zip(ref_sites, sites):
        if x_r not in table:
            print("# unknown ref site %04X" % x_r); continue
        t_ref, n = table[x_r]
        out.append((x + (t_ref - x_r), -1, n, "GPJ site %04X (RevA %04X)" % (pos, pos_r)))
    # VInt table
    m = re.search(rb"\x3a\x08\xc2\xe6\x1f\x21(..)\x85\x6f\x8c\x95\x67", r[:0x8000], re.S)
    if m: out.append((int.from_bytes(m.group(1), "little"), -1, 12, "VInt mode table"))
    else: print("# VInt table not found")
    # $4769-style helper
    h = r.find(HELP_SIG)
    if 0 <= h < 0x8000:
        for m in re.finditer(rb"\x11(..)\xCD" + bytes([h & 255, h >> 8]), r[:0x8000], re.S):
            out.append((int.from_bytes(m.group(1), "little") + 2, -1, 182, "helper %04X site %04X" % (h, m.start())))
    else: print("# $4769 helper not found")
    # sound tables
    s1 = re.search(rb"\xd5\x87\x4f\x06\x00\x21(..)\x09\x7e\x23\x66\x6f\xe9", r, re.S)
    s2 = re.search(rb"\xd6\xee\x21(..)\x87\x4f\x06\x00\x09\x4e\x23\x66\x69\xe9", r, re.S)
    for tag, m, n in (("snd1", s1, 5), ("snd2", s2, 18)):
        if not m: print("# %s table not found" % tag); continue
        bank = m.start() // 0x4000
        out.append((int.from_bytes(m.group(1), "little"), bank, n, "sound %s (bank %d)" % (tag, bank)))
    print("[jump_tables]")
    print("addr  = [" + ", ".join("0x%04X" % o[0] for o in out) + "]")
    print("bank  = [" + ", ".join(str(o[1]) for o in out) + "]")
    print("count = [" + ", ".join(str(o[2]) for o in out) + "]")
    for o in out: print("#  0x%04X bank %2d count %3d  %s" % o)


main()
