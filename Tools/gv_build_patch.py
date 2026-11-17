"""Build "Golvellius Plus", our own ROM patch, from the user's ROM with WLA-DX.

    python -I Tools/gv_build_patch.py [<Golvellius ROM>] [--no-rom]

Source: Patches/Golvellius/golvellius_plus.asm (our code only). The ROM (No-Intro "Golvellius -
Valley of Doom (USA, Europe, Brazil) (En)", CRC32 A51376FE; found in ROMs/ by CRC when not given)
is padded to 512 KB with $FF and used as the WLA-DX background image; wla-z80 + wlalink write the
patched ROM (header + checksum by .SMSHEADER). Then:
  - checks: 512 KB, header checksum (Tools/sms_checksum.py, the BIOS rule), and lists every changed
    range inside the original 256 KB (so nothing changes there by accident)
  - writes Patches/Golvellius/build/Golvellius Plus.ips (RLE records for runs) and .bps (checks the
    source ROM's CRC32 when applied), and verifies both by applying them with Tools/apply_patch.py
  - writes the patched ROM to ROMs/Patched/ (not with --no-rom), for the recompiled build
    Games/golvellius-plus
  - copies the .ips / .bps + a player's readme (.txt) to ROMs/Patches/Golvellius - Valley of Doom/
    Golvellius Plus (this project)/ (git-ignored), for other emulators and flash carts
WLA-DX is the git submodule External/wla-dx (the user's fork); it is built on first use into
External/build/wla-dx with the MSYS2 tool chain (C:/msys64/mingw64).
"""
import sys, os, shutil, subprocess, zlib
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import apply_patch, sms_checksum

ROOT = os.path.dirname(HERE)
SRC_DIR = os.path.join(ROOT, 'Patches', 'Golvellius')
ASM = 'golvellius_plus.asm'
OUT_DIR = os.path.join(SRC_DIR, 'build')
NAME = 'Golvellius Plus'
ROM_NAME = 'Golvellius - Valley of Doom (USA, Europe, Brazil) (En) [Golvellius Plus].sms'
CRC_ORIG = 0xA51376FE
SIZE = 0x80000
WLA_SRC = os.path.join(ROOT, 'External', 'wla-dx')
WLA_BUILD = os.path.join(ROOT, 'External', 'build', 'wla-dx')
MSYS = r'C:\msys64\mingw64\bin'
REL_DIR = 'Golvellius Plus (this project)'
README = """GOLVELLIUS PLUS
===============

A patch for "Golvellius - Valley of Doom" (Sega Master System): save slots, a title menu and an
OPTIONS page. Every option starts at the original game's behaviour, and passwords still work.

Base ROM:   Golvellius - Valley of Doom (USA, Europe, Brazil) (En).sms
            256 KB, CRC32 %(crc_src)08X (No-Intro). Use the original, unpatched ROM.
Result:     512 KB, CRC32 %(crc_dst)08X, header checksum $%(sum)04X (valid for the real BIOS).

Apply "Golvellius Plus.bps" (it refuses a wrong base ROM) or "Golvellius Plus.ips" with any
IPS / BPS patcher (for example Flips). Only one patch, nothing else to apply first.

Needs battery-backed cartridge RAM (every emulator has it; on hardware a flash cart or board
with 512 KB ROM, the Sega mapper and battery RAM).

WHAT IS NEW
-----------
Title screen: button 1 shows START / OPTIONS. Button 1 = select, button 2 = back.
  START    -> the original NEW GAME / CONTINUE.
  CONTINUE -> PASSWORD (the original password continues) / SAVE (the save slots).
  Once a game is saved, the cursor starts on CONTINUE, then on SAVE, then on the newest save.
Save slots: AUTO + 5.
  - AUTO is written whenever Winkle tells you the password ("GAME SAVED TO AUTO.").
  - Pause on the overworld (the status screen shows "1:SAVE  2:MAP" under its frame), then button 1:
    SAVE GAME into slot 1-5.
  - Game Over asks "SAVE THIS GAME?" under the password (you continue at the start house).
  - SAVE page: button 1 on a slot = LOAD / DELETE. A loaded game starts where it was saved.
  - Each slot shows its screen, play time, gold, power and crystals.
OPTIONS page (stored in the battery RAM; * marks the original game's value):
  TEXT FIXES        PASSWARD, three "GOLD" that should be "FIND", DESGINED in the credits
  MAP FIXES         screen C5 gets its own room, H4 the right cave picture (SMS Power fix)
  SWORD HIT FIX     every new swing can hurt (original: often only knocks back)
  SWORD REACH       the sword's hit area 4 or 8 pixels larger
  CAVE DOWN SPEED   walking down in top-down caves 1.5x / 2x as fast
  KEEP GOLD         continuing no longer halves your gold
  GOLD DROPS, DAMAGE DEALT, DAMAGE RECEIVED   x0 .. x10 (x1 = original)
  OVERWORLD SPEED   walking on the overworld 1.5x / 2x as fast

THE MAP (Pause on the overworld, then button 2): every screen you have seen, in its area's colour
(a dot = you have been inside its cave), your screen blinks. Button 1 on a square: what is there
(the cave's person, item and price, BOUGHT, the advice text; only for caves you have entered).
Arrows on that page jump to the next screen you have seen. Pause on the map = straight back to the
game. The map is saved with each save slot.

Passwords work as before (passwords from the original game too).
"""


def crc(b): return zlib.crc32(b) & 0xFFFFFFFF


def run(cmd, cwd=None, env=None):
    r = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True)
    if r.returncode:
        sys.stdout.write(r.stdout); sys.stdout.write(r.stderr)
        raise SystemExit('failed: %s' % ' '.join(cmd))
    return r.stdout + r.stderr


def ensure_wla():
    """configure (first use, or when the project folder moved) and build wla-z80 + wlalink"""
    if not os.path.isfile(os.path.join(WLA_SRC, 'CMakeLists.txt')):
        raise SystemExit('External/wla-dx is empty: run  git submodule update --init External/wla-dx')
    env = dict(os.environ)
    if os.path.isdir(MSYS): env['PATH'] = MSYS + os.pathsep + env.get('PATH', '')
    cmake = os.path.join(MSYS, 'cmake.exe') if os.path.isfile(os.path.join(MSYS, 'cmake.exe')) else 'cmake'
    cache = os.path.join(WLA_BUILD, 'CMakeCache.txt')
    if os.path.isfile(cache):
        home = [l.split('=', 1)[1].strip() for l in open(cache, encoding='utf-8', errors='replace') if l.startswith('CMAKE_HOME_DIRECTORY:')]
        if not home or os.path.normcase(os.path.abspath(home[0])) != os.path.normcase(os.path.abspath(WLA_SRC)):
            shutil.rmtree(WLA_BUILD)                    # the project moved: configure again
    if not os.path.isfile(cache):
        os.makedirs(WLA_BUILD, exist_ok=True)
        run([cmake, '-S', WLA_SRC, '-B', WLA_BUILD, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=gcc', '-DWLA_BUILD_DOCS=OFF'], env=env)
    run([cmake, '--build', WLA_BUILD, '--target', 'wla-z80', 'wlalink'], env=env)
    b = os.path.join(WLA_BUILD, 'binaries')
    return os.path.join(b, 'wla-z80.exe'), os.path.join(b, 'wlalink.exe')


def find_rom():
    for dp, dn, fn in os.walk(os.path.join(ROOT, 'ROMs')):
        if os.path.basename(dp) in ('Patches',): dn[:] = []; continue
        for f in fn:
            if f.lower().endswith('.sms'):
                p = os.path.join(dp, f)
                if os.path.getsize(p) == 0x40000 and crc(open(p, 'rb').read()) == CRC_ORIG: return p
    raise SystemExit('Golvellius ROM (CRC32 %08X) not found in ROMs/' % CRC_ORIG)


def make_ips(src, dst):
    """records for every changed run; runs of one value >= 16 bytes become RLE records"""
    out = bytearray(b'PATCH')
    def rec(off, data):
        o = 0
        while o < len(data):
            v = data[o]; r = o
            while r < len(data) and data[r] == v and r - o < 0xFFFF: r += 1
            if r - o >= 16:
                out.extend((off + o).to_bytes(3, 'big') + b'\0\0' + (r - o).to_bytes(2, 'big') + bytes([v])); o = r; continue
            e = o                                       # literal up to the next long run
            while e < len(data) and e - o < 0xFFFF:
                v2 = data[e]; r2 = e
                while r2 < len(data) and data[r2] == v2 and r2 - e < 16: r2 += 1
                if r2 - e >= 16: break
                e = r2
            out.extend((off + o).to_bytes(3, 'big') + (e - o).to_bytes(2, 'big') + data[o:e]); o = e
    o = 0
    while o < len(dst):
        if o >= len(src) or src[o] != dst[o]:
            s = o
            while o < len(dst) and (o >= len(src) or src[o] != dst[o]): o += 1
            rec(s, bytes(dst[s:o]))
        else: o += 1
    return bytes(out + b'EOF')                          # (an offset of $454F46 = "EOF" cannot occur below 512 KB)


def make_bps(src, dst):
    """SourceRead where the bytes are unchanged at the same offset, TargetRead for the rest"""
    def num(n):
        b = bytearray()
        while True:
            x = n & 0x7F; n >>= 7
            if n == 0: b.append(0x80 | x); return bytes(b)
            b.append(x); n -= 1
    out = bytearray(b'BPS1') + num(len(src)) + num(len(dst)) + num(0)
    dst_rel = 0
    def changed(o, e):                                  # TargetRead, runs >= 16 as an overlapping TargetCopy
        nonlocal out, dst_rel
        k = o
        while k < e:
            r = k
            while r < e and dst[r] == dst[k]: r += 1
            if r - k >= 16:
                if k + 1 > o: out += num((k + 1 - o - 1) << 2 | 1) + dst[o:k + 1]
                d = k - dst_rel
                out += num((r - k - 2) << 2 | 3) + num(abs(d) << 1 | (d < 0))
                dst_rel = k + (r - k - 1)
                o = k = r
            else: k = r
        if e > o: out += num((e - o - 1) << 2 | 1) + dst[o:e]
    o = 0
    while o < len(dst):
        same = o < len(src) and src[o] == dst[o]
        e = o
        while e < len(dst) and (e < len(src) and src[e] == dst[e]) == same: e += 1
        if same: out += num((e - o - 1) << 2)
        else: changed(o, e)
        o = e
    out += crc(src).to_bytes(4, 'little') + crc(dst).to_bytes(4, 'little')
    out += crc(bytes(out)).to_bytes(4, 'little')
    return bytes(out)


def main(argv):
    args = list(argv); write_rom = True
    if '--no-rom' in args: args.remove('--no-rom'); write_rom = False
    rom_path = args[0] if args else find_rom()
    src = open(rom_path, 'rb').read()
    if crc(src) != CRC_ORIG: raise SystemExit('%s: not the expected ROM (CRC32 %08X)' % (rom_path, CRC_ORIG))
    wla, link = ensure_wla()
    os.makedirs(OUT_DIR, exist_ok=True)
    open(os.path.join(OUT_DIR, 'background.sms'), 'wb').write(src + b'\xFF' * (SIZE - len(src)))
    shutil.copyfile(os.path.join(SRC_DIR, ASM), os.path.join(OUT_DIR, ASM))
    run([wla, '-o', 'gvplus.o', ASM], cwd=OUT_DIR)
    open(os.path.join(OUT_DIR, 'link.txt'), 'w').write('[objects]\ngvplus.o\n')
    log = run([link, '-v', '-S', 'link.txt', 'gvplus.sms'], cwd=OUT_DIR)
    open(os.path.join(OUT_DIR, 'link_log.txt'), 'w').write(log)
    dst = open(os.path.join(OUT_DIR, 'gvplus.sms'), 'rb').read()

    # checks
    if len(dst) != SIZE: raise SystemExit('linked ROM is %d bytes, expected %d' % (len(dst), SIZE))
    c = sms_checksum.compute(dst)
    if c != (dst[0x7FFA] | dst[0x7FFB] << 8): raise SystemExit('header checksum wrong: stored %04X computed %04X' % (dst[0x7FFA] | dst[0x7FFB] << 8, c))
    print('header: size code $%X, checksum $%04X (BIOS rule, whole 512 KB) OK' % (dst[0x7FFF] & 15, c))
    ranges = []; o = 0
    while o < len(src):
        if src[o] != dst[o]:
            s = o
            while o < len(src) and src[o] != dst[o]: o += 1
            ranges.append((s, o - 1))
        else: o += 1
    print('changed inside the original 256 KB: ' + (', '.join('$%05X-$%05X' % r for r in ranges) or 'nothing'))
    used = sum(1 for b in dst[0x40000:] if b != 0xFF)
    print('new banks 16-31: %d bytes not $FF' % used)

    # patches
    ips = make_ips(src, dst); bps = make_bps(src, dst)
    pi = os.path.join(OUT_DIR, NAME + '.ips'); pb = os.path.join(OUT_DIR, NAME + '.bps')
    open(pi, 'wb').write(ips); open(pb, 'wb').write(bps)
    if apply_patch.apply_ips(ips, src) != dst: raise SystemExit('IPS check failed')
    if apply_patch.apply_bps(bps, src, False) != dst: raise SystemExit('BPS check failed')
    print('%s: %d bytes, %s: %d bytes (both re-applied = the linked ROM)' % (os.path.relpath(pi, ROOT), len(ips), os.path.basename(pb), len(bps)))
    print('patched ROM: CRC32 %08X' % crc(dst))
    if write_rom:
        d = os.path.join(ROOT, 'ROMs', 'Patched'); os.makedirs(d, exist_ok=True)
        p = os.path.join(d, ROM_NAME); open(p, 'wb').write(dst)
        print('wrote %s' % os.path.relpath(p, ROOT))

    # ready-to-use copy for other emulators / flash carts (git-ignored like all of ROMs/)
    rel = os.path.join(ROOT, 'ROMs', 'Patches', 'Golvellius - Valley of Doom', REL_DIR)
    os.makedirs(rel, exist_ok=True)
    shutil.copyfile(pi, os.path.join(rel, NAME + '.ips')); shutil.copyfile(pb, os.path.join(rel, NAME + '.bps'))
    with open(os.path.join(rel, NAME + '.txt'), 'w', encoding='utf-8', newline='\r\n') as f:
        f.write(README % {'crc_src': CRC_ORIG, 'crc_dst': crc(dst), 'sum': c})
    print('wrote %s (.ips, .bps, .txt)' % os.path.relpath(rel, ROOT))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
