#!/usr/bin/env python3
"""List every .sms ROM in ROMs/ with size, CRC32, MD5, SMS header info and bank count.
Run:  python -I Tools/rom_inventory.py [rom_dir]"""
import sys, os, zlib, hashlib, glob

REGION = {3: "SMS Japan", 4: "SMS Export", 5: "GG Japan", 6: "GG Export", 7: "GG International"}

def header(rom):
    # SMS header may sit at 0x7FF0 (normal), 0x3FF0 or 0x1FF0
    for off in (0x7FF0, 0x3FF0, 0x1FF0):
        if rom[off:off + 8] == b"TMR SEGA":
            cks = rom[off + 10] | (rom[off + 11] << 8)
            region = rom[off + 15] >> 4
            size = rom[off + 15] & 15
            return off, cks, REGION.get(region, str(region)), size
    return None

def main():
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ROMs")
    for p in sorted(glob.glob(os.path.join(d, "*.sms"))):
        rom = open(p, "rb").read()
        # strip a 512-byte copier header if present
        copier = len(rom) % 16384 == 512
        if copier: rom = rom[512:]
        crc = zlib.crc32(rom) & 0xFFFFFFFF
        h = header(rom)
        print(f"{os.path.basename(p)}")
        print(f"   size={len(rom)} ({len(rom)/16384:.2f} banks){' copier-hdr' if copier else ''} crc32={crc:08X} md5={hashlib.md5(rom).hexdigest()}")
        print(f"   header={'@%04X cks=%04X %s sizecode=%X' % h if h else 'NONE'}")

if __name__ == "__main__":
    main()
