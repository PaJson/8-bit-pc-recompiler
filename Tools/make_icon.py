"""Make the Windows icon (Images/Icon.ico) from the app's own pictures.

    python -I Tools/make_icon.py [<Images folder>] [--small Icon.png] [--large IconLarge.png] [--split 128]

Sizes below --split (16, 20, 24, 32, 40, 48, 64, 72, 80, 96) are made from the small picture (the
compact "8 BIT", readable at 16 px), the others (128, 256) from the large one ("8-BIT PC RECOMP").
Each picture is cropped to a square around its visible pixels (+2 %), then shrunk by area
averaging with premultiplied alpha (no dark or coloured edges). The .ico holds 32-bit BMP images
below 256 px and a PNG at 256 px (the usual Windows layout). Without --large every size uses the
small picture. The launcher and every game exe embed the result (Launcher/CMakeLists.txt,
the engine's cmake/SmsGame.cmake). Standard library only; shared with the 16-bit project.
"""
import os, struct, sys, zlib

SIZES = (16, 20, 24, 32, 40, 48, 64, 72, 80, 96, 128, 256)


def read_png(path):
    """8-bit RGB / RGBA PNG (tRNS colour key for RGB) -> (w, h, list of RGBA bytes)."""
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG")
    pos, idat, trns = 8, b"", None
    while pos < len(d):
        n, = struct.unpack(">I", d[pos:pos + 4]); t = d[pos + 4:pos + 8]; c = d[pos + 8:pos + 8 + n]; pos += 12 + n
        if t == b"IHDR":
            w, h, bd, ct, _, _, il = struct.unpack(">IIBBBBB", c)
        elif t == b"IDAT":
            idat += c
        elif t == b"tRNS":
            trns = c
    if bd != 8 or ct not in (2, 6) or il:
        sys.exit(f"{path}: only 8-bit RGB / RGBA, non-interlaced PNGs are supported")
    bpp = 3 if ct == 2 else 4
    raw = zlib.decompress(idat); st = w * bpp
    out = bytearray(w * h * 4); prev = bytearray(st); i = 0
    key = struct.unpack(">HHH", trns) if (ct == 2 and trns and len(trns) == 6) else None
    for y in range(h):
        f = raw[i]; line = bytearray(raw[i + 1:i + 1 + st]); i += 1 + st
        if f == 1:
            for x in range(bpp, st): line[x] = (line[x] + line[x - bpp]) & 255
        elif f == 2:
            for x in range(st): line[x] = (line[x] + prev[x]) & 255
        elif f == 3:
            for x in range(st): line[x] = (line[x] + ((line[x - bpp] if x >= bpp else 0) + prev[x]) // 2) & 255
        elif f == 4:
            for x in range(st):
                a = line[x - bpp] if x >= bpp else 0; b = prev[x]; c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        o = y * w * 4
        if bpp == 4:
            out[o:o + w * 4] = line
        else:
            for x in range(w):
                r, g, b = line[x * 3:x * 3 + 3]
                out[o + x * 4:o + x * 4 + 4] = bytes((r, g, b, 0 if key == (r, g, b) else 255))
    return w, h, out


def crop_square(w, h, px, margin=0.02):
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        row = px[y * w * 4 + 3:(y + 1) * w * 4:4]
        xs = [x for x in range(w) if row[x] > 8]
        if xs:
            y0 = min(y0, y); y1 = y; x0 = min(x0, xs[0]); x1 = max(x1, xs[-1])
    if x1 < 0:
        sys.exit("the picture is completely transparent")
    s = max(x1 - x0 + 1, y1 - y0 + 1); s += 2 * int(round(s * margin))
    cx, cy = (x0 + x1 + 1) // 2, (y0 + y1 + 1) // 2
    ox, oy = cx - s // 2, cy - s // 2
    out = [0.0] * (s * s * 4)                       # premultiplied floats
    for y in range(s):
        sy = oy + y
        if not 0 <= sy < h: continue
        for x in range(s):
            sx = ox + x
            if not 0 <= sx < w: continue
            o = (sy * w + sx) * 4; a = px[o + 3] / 255.0
            if a:
                d = (y * s + x) * 4
                out[d] = px[o] * a; out[d + 1] = px[o + 1] * a; out[d + 2] = px[o + 2] * a; out[d + 3] = a
    return s, out


def weights(src, dst):
    """area-average weights: for each output index, [(input index, weight)]"""
    f = src / dst; res = []
    for i in range(dst):
        a, b = i * f, (i + 1) * f; lst = []
        j = int(a)
        while j < b and j < src:
            ov = min(b, j + 1) - max(a, j)
            if ov > 0: lst.append((j, ov / f))
            j += 1
        res.append(lst)
    return res


def shrink(s, img, t):
    """square premultiplied image s x s -> t x t (area average, separable)"""
    wt = weights(s, t)
    tmp = [0.0] * (s * t * 4)                       # s rows x t columns
    for y in range(s):
        row = y * s * 4; trow = y * t * 4
        for x, lst in enumerate(wt):
            r = g = b = a = 0.0
            for j, wgt in lst:
                o = row + j * 4; r += img[o] * wgt; g += img[o + 1] * wgt; b += img[o + 2] * wgt; a += img[o + 3] * wgt
            d = trow + x * 4; tmp[d] = r; tmp[d + 1] = g; tmp[d + 2] = b; tmp[d + 3] = a
    out = [0.0] * (t * t * 4)
    for y, lst in enumerate(wt):
        for x in range(t):
            r = g = b = a = 0.0
            for j, wgt in lst:
                o = (j * t + x) * 4; r += tmp[o] * wgt; g += tmp[o + 1] * wgt; b += tmp[o + 2] * wgt; a += tmp[o + 3] * wgt
            d = (y * t + x) * 4; out[d] = r; out[d + 1] = g; out[d + 2] = b; out[d + 3] = a
    return out


def to_rgba(t, img):
    out = bytearray(t * t * 4)
    for i in range(t * t):
        a = img[i * 4 + 3]
        if a > 1e-6:
            for c in range(3): out[i * 4 + c] = max(0, min(255, int(round(img[i * 4 + c] / a))))
        out[i * 4 + 3] = max(0, min(255, int(round(a * 255))))
    return out


def png_bytes(t, rgba):
    raw = b"".join(b"\0" + bytes(rgba[y * t * 4:(y + 1) * t * 4]) for y in range(t))
    def ch(k, c): return struct.pack(">I", len(c)) + k + c + struct.pack(">I", zlib.crc32(k + c) & 0xFFFFFFFF)
    return b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", t, t, 8, 6, 0, 0, 0)) + ch(b"IDAT", zlib.compress(raw, 9)) + ch(b"IEND", b"")


def bmp_bytes(t, rgba):
    """32-bit DIB for an icon: header with double height, BGRA bottom-up, AND mask (1 = transparent)"""
    mask_row = ((t + 31) // 32) * 4
    hdr = struct.pack("<IiiHHIIiiII", 40, t, t * 2, 1, 32, 0, t * t * 4 + mask_row * t, 0, 0, 0, 0)
    px = bytearray()
    for y in range(t - 1, -1, -1):
        for x in range(t):
            r, g, b, a = rgba[(y * t + x) * 4:(y * t + x) * 4 + 4]; px += bytes((b, g, r, a))
    mask = bytearray()
    for y in range(t - 1, -1, -1):
        row = bytearray(mask_row)
        for x in range(t):
            if rgba[(y * t + x) * 4 + 3] == 0: row[x // 8] |= 0x80 >> (x % 8)
        mask += row
    return hdr + bytes(px) + bytes(mask)


def main():
    args = sys.argv[1:]
    opt = {"--small": "Icon.png", "--large": "IconLarge.png", "--split": "128"}
    pos = []
    i = 0
    while i < len(args):
        if args[i] in opt and i + 1 < len(args): opt[args[i]] = args[i + 1]; i += 2
        else: pos.append(args[i]); i += 1
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    images = pos[0] if pos else os.path.join(root, "Images")
    small = os.path.join(images, opt["--small"]); large = os.path.join(images, opt["--large"])
    split = int(opt["--split"])
    if not os.path.isfile(small): sys.exit(f"missing {small}")
    if not os.path.isfile(large): large = small
    base = {}                                        # picture -> its square at 256 px (premultiplied)
    for p in {small, large}:
        w, h, px = read_png(p); s, sq = crop_square(w, h, px)
        print(f"{os.path.basename(p)}: {w}x{h}, square crop {s}x{s}")
        base[p] = (256, shrink(s, sq, 256) if s > 256 else sq) if s >= 256 else (s, sq)
    entries = []
    for t in SIZES:
        s, sq = base[small if t < split else large]
        rgba = to_rgba(t, shrink(s, sq, t) if t != s else sq)
        entries.append((t, png_bytes(t, rgba) if t >= 256 else bmp_bytes(t, rgba)))
    data = struct.pack("<HHH", 0, 1, len(entries)); off = 6 + 16 * len(entries); body = b""
    for t, b in entries:
        data += struct.pack("<BBBBHHII", t % 256, t % 256, 0, 0, 1, 32, len(b), off + len(body)); body += b
    out = os.path.join(images, "Icon.ico")
    tmp = out + ".tmp"
    with open(tmp, "wb") as f: f.write(data + body)
    os.replace(tmp, out)
    print(f"wrote {out}: {len(entries)} sizes ({', '.join(str(t) for t, _ in entries)}), {len(data) + len(body)} bytes")


if __name__ == "__main__":
    main()
