"""Phantasy Star (SMS): draw dungeon floors from the ROM like the HUD map (whole floor), side by side, with marked cells.
   draw_floors.py <rom> <out.ppm> <floor>[:<cell>,<cell>...] ..."""
import sys, re
rom = open(sys.argv[1], "rb").read()
LO = rom.find(bytes.fromhex("11111111111111111111A000010111A1"), 15 * 0x4000)
m = re.search(rb"\x21\xFF\xFF\x36\x03\x21(..)\x11\x06\x00", rom[:0x8000], re.S)
o = 3 * 0x4000 + (m.group(1)[0] | m.group(1)[1] << 8) - 0x8000
objs = {}
while rom[o] != 0xFF:
    objs[(rom[o], rom[o + 1])] = rom[o + 4]; o += 7
CS, GAP = 24, 24
specs = []
for a in sys.argv[3:]:
    f, _, cl = a.partition(":")
    specs.append((int(f, 16), [int(c, 16) for c in cl.split(",") if c]))
W = len(specs) * (16 * CS + GAP) - GAP + 2 * 8
H = 16 * CS + 16
img = [[(12, 12, 18)] * W for _ in range(H)]
def rect(x0, y0, x1, y1, c):
    for y in range(max(0, y0), min(H, y1)):
        row = img[y]
        for x in range(max(0, x0), min(W, x1)): row[x] = c
def hexc(v): return ((v >> 16) & 255, (v >> 8) & 255, v & 255)
FLOOR, WALL, SECRET = hexc(0x3C6048), hexc(0x707A98), hexc(0x8C5CB4)
DOOR, LOCKED, MAGIC, STAIRS = hexc(0xB07A34), hexc(0xE86A20), hexc(0x40C8E8), hexc(0xF0F0F0)
PIT_RIM, CHEST, EVENT, EXIT, MARK = hexc(0xC03C3C), hexc(0xF0C030), hexc(0xF04848), hexc(0x40E070), hexc(0xFFE040)
for k, (f, marks) in enumerate(specs):
    ox, oy = 8 + k * (16 * CS + GAP), 8
    c = [n for x in rom[LO + 128 * f:LO + 128 * f + 128] for n in (x >> 4, x & 15)]
    for p in range(256):
        sx, sy, e, q = ox + (p & 15) * CS, oy + (p >> 4) * CS, CS - 3, CS // 4
        t = c[p]; tt = t & 7 if t >= 8 and t != 8 else t
        if t == 1: rect(sx, sy, sx + e, sy + e, WALL); continue
        rect(sx, sy, sx + e, sy + e, FLOOR)
        if tt in (2, 3):                                  # stairs: triangle up / down
            for i in range(e // 2):
                w = 2 * i + 2; r = i if tt == 2 else e // 2 - 1 - i
                rect(sx + (e - w) // 2, sy + e // 4 + r, sx + (e - w) // 2 + w, sy + e // 4 + r + 1, STAIRS)
        elif tt in (4, 5, 6):
            rect(sx, sy, sx + e, sy + e, WALL)
            rect(sx + q, sy + q, sx + e - q, sy + e - q, DOOR if tt == 4 else LOCKED if tt == 5 else MAGIC)
        elif t == 7:
            rect(sx, sy, sx + e, sy + e, SECRET)
        elif t == 8:
            kind = objs.get((f, p))
            if kind is None:
                rect(sx + 3, sy + 3, sx + e - 3, sy + e - 3, PIT_RIM); rect(sx + 6, sy + 6, sx + e - 6, sy + e - 6, (0, 0, 0))
            else:
                rect(sx + q, sy + q, sx + e - q, sy + e - q, CHEST if kind <= 1 else EVENT)
        if t >= 0xA: rect(sx, sy, sx + 6, sy + 6, EXIT)     # leads out of the dungeon
        if p in marks:                                    # marked cell: yellow frame
            for d in range(3):
                rect(sx - 3 + d, sy - 3 + d, sx + e + 3 - d, sy - 2 + d, MARK); rect(sx - 3 + d, sy + e + 2 - d, sx + e + 3 - d, sy + e + 3 - d, MARK)
                rect(sx - 3 + d, sy - 3 + d, sx - 2 + d, sy + e + 3 - d, MARK); rect(sx + e + 2 - d, sy - 3 + d, sx + e + 3 - d, sy + e + 3 - d, MARK)
with open(sys.argv[2], "wb") as fo:
    fo.write(b"P6\n%d %d\n255\n" % (W, H))
    fo.write(bytes(v for row in img for px in row for v in px))
