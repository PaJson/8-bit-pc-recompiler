"""Apply an IPS or BPS patch to a ROM (our own tool; replaces FLIPS for this project).

    python -I Tools/apply_patch.py <patch.ips|patch.bps> <in.sms> <out.sms> [<patch2> ...] [--force]

A patch can also be read straight out of the author's zip: "<archive.zip>::<file in it>" (the file
name alone is enough when it is unique in the archive), e.g.
    "ROMs/Patches/.../PhantasyStar-SMS-EN-2.6.1.zip::ps1jert.en.bps"
"<archive.zip>::" alone lists the archive's patch files.

Several patches are applied one after another (e.g. a translation and then its update). The output
is only written when every patch applied cleanly, and never over the input file.

BPS ("BPS1", byuu's format): the patch carries CRC32s of the source, the target and itself; all three
are checked (a wrong source ROM is refused unless --force). IPS ("PATCH" ... "EOF"): plain and RLE
records, the optional 3-byte truncation size after "EOF". IPS has no checksums, so check the result
yourself (the patch author usually names the source ROM's CRC32).
"""
import sys, zlib, zipfile, os

def crc(b): return zlib.crc32(b) & 0xFFFFFFFF

def read_patch(spec):
    """a patch file, or "<zip>::<member>" (member matched by full name or by its file name)"""
    if '::' not in spec: return open(spec, 'rb').read()
    zpath, member = spec.split('::', 1)
    zf = zipfile.ZipFile(zpath)
    names = [n for n in zf.namelist() if not n.endswith('/')]
    hits = [n for n in names if n == member or os.path.basename(n) == member]
    if not member or len(hits) != 1:
        cands = [n for n in names if n.lower().endswith(('.ips', '.bps'))]
        raise ValueError((f'"{member}" is ' + ('ambiguous' if hits else 'not') + ' in the archive; ' if member else '')
                         + 'patch files in it: ' + ', '.join(cands))
    return zf.read(hits[0])

def apply_ips(p, src):
    if p[:5] != b'PATCH': raise ValueError('not an IPS patch')
    out = bytearray(src); i = 5
    while True:
        if i + 3 > len(p): raise ValueError('IPS: no EOF marker')
        if p[i:i + 3] == b'EOF':
            i += 3
            if i + 3 <= len(p): del out[int.from_bytes(p[i:i + 3], 'big'):]   # truncation extension
            return bytes(out)
        if i + 5 > len(p): raise ValueError('IPS: record header cut off')
        off = int.from_bytes(p[i:i + 3], 'big'); n = int.from_bytes(p[i + 3:i + 5], 'big'); i += 5
        if n == 0:                                                            # RLE record
            if i + 3 > len(p): raise ValueError('IPS: RLE record cut off')
            n = int.from_bytes(p[i:i + 2], 'big'); data = p[i + 2:i + 3] * n; i += 3
        else:
            if i + n > len(p): raise ValueError('IPS: record data cut off')
            data = p[i:i + n]; i += n
        if off + n > len(out): out.extend(b'\0' * (off + n - len(out)))
        out[off:off + n] = data

def apply_bps(p, src, force):
    if p[:4] != b'BPS1': raise ValueError('not a BPS patch')
    if len(p) < 4 + 12: raise ValueError('BPS: too short')
    want_src, want_dst, want_patch = (int.from_bytes(p[k:k + 4], 'little') for k in (len(p) - 12, len(p) - 8, len(p) - 4))
    if crc(p[:-4]) != want_patch: raise ValueError('BPS: the patch file itself is damaged (CRC32 mismatch)')
    if crc(src) != want_src and not force:
        raise ValueError(f'BPS: wrong source ROM (CRC32 {crc(src):08X}, the patch needs {want_src:08X}); --force to apply anyway')
    pos = 4; end = len(p) - 12
    def num():
        nonlocal pos
        data, shift = 0, 1
        while True:
            if pos >= end: raise ValueError('BPS: number cut off')
            x = p[pos]; pos += 1
            data += (x & 0x7F) * shift
            if x & 0x80: return data
            shift <<= 7; data += shift
    src_size, dst_size, meta = num(), num(), num()
    pos += meta                                                               # metadata (ignored)
    if src_size != len(src) and not force: raise ValueError(f'BPS: source size {len(src)}, the patch needs {src_size}')
    out = bytearray(dst_size); o = 0; src_rel = 0; dst_rel = 0
    while pos < end:
        d = num(); action, n = d & 3, (d >> 2) + 1
        if o + n > dst_size: raise ValueError('BPS: writes past the target size')
        if action == 0:                                                       # SourceRead
            if o + n > len(src): raise ValueError('BPS: source read out of range')
            out[o:o + n] = src[o:o + n]
        elif action == 1:                                                     # TargetRead
            if pos + n > end: raise ValueError('BPS: data cut off')
            out[o:o + n] = p[pos:pos + n]; pos += n
        else:
            d2 = num(); delta = -(d2 >> 1) if d2 & 1 else d2 >> 1
            if action == 2:                                                   # SourceCopy
                src_rel += delta
                if src_rel < 0 or src_rel + n > len(src): raise ValueError('BPS: source copy out of range')
                out[o:o + n] = src[src_rel:src_rel + n]; src_rel += n
            else:                                                             # TargetCopy (may overlap)
                dst_rel += delta
                if dst_rel < 0: raise ValueError('BPS: target copy out of range')
                for k in range(n): out[o + k] = out[dst_rel + k]
                dst_rel += n
        o += n
    out = bytes(out)
    if crc(out) != want_dst: raise ValueError(f'BPS: result CRC32 {crc(out):08X}, expected {want_dst:08X}')
    return out

def main(argv):
    force = '--force' in argv; args = [a for a in argv if a != '--force']
    if len(args) < 3:
        print(__doc__); return 2
    patches, src_path, out_path = [args[0]] + args[3:], args[1], args[2]
    if out_path == src_path: print('refusing to overwrite the input ROM'); return 2
    rom = open(src_path, 'rb').read()
    for pp in patches:
        try:
            p = read_patch(pp)
            rom = apply_bps(p, rom, force) if p[:4] == b'BPS1' else apply_ips(p, rom)
        except ValueError as e:
            print(f'{pp}: {e}'); return 1
        print(f'applied {pp}')
    open(out_path, 'wb').write(rom)
    print(f'wrote {out_path}: {len(rom)} bytes, CRC32 {crc(rom):08X}')
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
