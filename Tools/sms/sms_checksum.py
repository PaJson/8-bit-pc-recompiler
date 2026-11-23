"""SMS ROM header checksum: check (default) or compute it, the way the export SMS BIOS does.

    python -I Tools/sms/sms_checksum.py <rom.sms> [...]     prints header, size code, stored / computed checksum

Rule (SMS Power "ROM header", same result as the user's fork github.com/PaJson/smssum; own code):
the header "TMR SEGA" sits at $1FF0, $3FF0 or $7FF0; the low nibble of byte +$F is the size code;
the checksum (+$A, little endian) is the 16-bit sum of the bytes from $0000 up to the header
($xFF0, header excluded) and, for 64 KB and more, of every byte from $8000 to the end of the range
the size code names. Codes: $A 8 KB, $B 16 KB, $C 32 KB, $D 48 KB (buggy in BIOSes), $E 64 KB,
$F 128 KB, $0 256 KB, $1 512 KB, $2 1 MB (buggy). Japanese and Game Gear BIOSes do not check it.
Exit code 0 = every ROM's checksum matches.
"""
import sys

SIZES = {0xA: 0x2000, 0xB: 0x4000, 0xC: 0x8000, 0xD: 0xC000, 0xE: 0x10000,
         0xF: 0x20000, 0x0: 0x40000, 0x1: 0x80000, 0x2: 0x100000}
BUGGY = {0xD, 0x2}


def header_at(rom):
    for h in (0x7FF0, 0x3FF0, 0x1FF0):
        if len(rom) >= h + 16 and rom[h:h + 8] == b'TMR SEGA':
            return h
    return None


def compute(rom, h=None):
    """the checksum the BIOS computes (None if there is no header or the size code is unknown)"""
    if h is None: h = header_at(rom)
    if h is None: return None
    code = rom[h + 15] & 15
    n = SIZES.get(code)
    if n is None: return None
    if n > len(rom): raise ValueError('size code $%X names %d KB, the ROM has only %d KB' % (code, n // 1024, len(rom) // 1024))
    s = sum(rom[0:h])
    if n > 0x8000: s += sum(rom[0x8000:n])
    return s & 0xFFFF


def fix(rom):
    """rewrite the stored checksum in a bytearray; returns the value"""
    h = header_at(rom)
    c = compute(rom, h)
    rom[h + 10] = c & 0xFF; rom[h + 11] = c >> 8
    return c


def check(path):
    rom = open(path, 'rb').read()
    h = header_at(rom)
    if h is None:
        print('%s: no TMR SEGA header' % path); return False
    code = rom[h + 15] & 15
    stored = rom[h + 10] | rom[h + 11] << 8
    c = compute(rom, h)
    size = SIZES.get(code)
    print('%s: header $%04X, size code $%X (%s%s), file %d KB, stored $%04X, computed %s -> %s'
          % (path, h, code, '%d KB' % (size // 1024) if size else 'unknown', ', buggy in BIOSes' if code in BUGGY else '',
             len(rom) // 1024, stored, '$%04X' % c if c is not None else '-', 'OK' if c == stored else 'MISMATCH'))
    return c == stored


if __name__ == '__main__':
    if len(sys.argv) < 2: print(__doc__); sys.exit(2)
    ok = all([check(p) for p in sys.argv[1:]])
    sys.exit(0 if ok else 1)
