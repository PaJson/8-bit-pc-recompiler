"""make_release.py - put the built games and the launcher into a stand-alone folder (default: Release/).

    python -I Tools/make_release.py [<out dir>] [--with-images]

The result is the intended running environment, without sources or a compiler:

    Release/
      Launcher.exe, SDL2.dll, launcher.ini.sample, README.txt, CREDITS.md
      Games/<slug>/   <Name>Recomp.exe, SDL2.dll, game.toml (only what the launcher needs: name, ROM
                      CRC32, exe, how to make a patched ROM); saves/ and the game's .ini appear when played
      ROMs/           the user's ROMs, any file names (the launcher matches them by CRC32)
      ROMs/Patches/   patch files (IPS / BPS, loose or in the author's .zip): ours (Golvellius Plus) and
                      the PSRP 2.6.1 zips (their terms allow passing them on unchanged, free of charge)
      ROMs/Patched/   ROMs the launcher makes from a base ROM + a patch
      Images/         optional art: background, "<title> (front).jpg", "<title> (logo).png"

Only games with a built exe (Games/<slug>/build/*.exe) and their ROM present are copied. A game whose ROM
is made by a patch gets a recipe in its game.toml: every .bps in ROMs/Patches (loose or inside a zip) is
read, and a BPS whose target CRC32 is that game's ROM gives the base ROM's CRC32 and the patch's CRC32
(the launcher finds both by CRC32, so renamed files work too).

NEVER copied: ROMs, saves, the user's .ini settings, third-party patch files other than those in
REDISTRIBUTABLE (whole zips, byte-identical; CREDITS.md has the official pages). --with-images copies the user's own Images/ (personal use only: SEGA artwork).
Existing files in the output folder (saves, settings, ROMs) are kept; exes are replaced when they changed.
The Games/*/ exes contain the game's program translated to PC code (derived from the ROM): the Release
folder is for this PC, not for sharing (it is git-ignored).
"""
import os, re, shutil, sys, zipfile, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def crc_file(p):
    with open(p, "rb") as f:
        return zlib.crc32(f.read()) & 0xFFFFFFFF


def toml_str(text, key):
    m = re.search(r'^\s*' + re.escape(key) + r'\s*=\s*"([^"]*)"', text, re.M)
    return m.group(1) if m else None


def bps_crcs(data):
    """(source crc, target crc, patch id) of a BPS file, or None. The patch id = the CRC32 stored in its
    last 4 bytes (the CRC32 of the whole file is the same constant for every BPS: data + its own CRC32)"""
    if len(data) < 16 or data[:4] != b"BPS1":
        return None
    src = int.from_bytes(data[-12:-8], "little")
    dst = int.from_bytes(data[-8:-4], "little")
    return src, dst, int.from_bytes(data[-4:], "little")


def scan_patches(pdir):
    """target crc -> (source crc, patch id, patch name, where to get it)"""
    out = {}
    for dp, _, files in os.walk(pdir):
        for f in files:
            p = os.path.join(dp, f)
            if f.lower().endswith(".zip"):
                try:
                    z = zipfile.ZipFile(p)
                except zipfile.BadZipFile:
                    continue
                for info in z.infolist():
                    if info.filename.lower().endswith(".bps"):
                        c = bps_crcs(z.read(info))
                        if c:
                            out.setdefault(c[1], (c[0], c[2], os.path.basename(info.filename), f))
            elif f.lower().endswith(".bps"):
                with open(p, "rb") as fh:
                    c = bps_crcs(fh.read())
                if c:
                    out.setdefault(c[1], (c[0], c[2], f, f))
    return out


def copy_if_changed(src, dst):
    if os.path.isfile(dst) and os.path.getsize(dst) == os.path.getsize(src) and crc_file(dst) == crc_file(src):
        return False
    shutil.copy2(src, dst)
    return True


README = """SMS PC Recompiler - games
=========================

Start Launcher.exe. It lists every game whose ROM is in the ROMs folder.

ROMs
  Put your own ROM files (.sms) into ROMs\\ (subfolders are fine). The file names do not matter: every
  game program is made for one exact ROM and the launcher recognises it by its CRC32 checksum. A ROM
  that no game is made for is listed in grey.

Patched games (translations, Golvellius Plus)
  These need the original ROM in ROMs\\ and the patch file in ROMs\\Patches\\ (the IPS / BPS file, or the
  author's .zip as downloaded). The launcher lists such a game once the base ROM is there; on Play it
  makes the patched ROM (into ROMs\\Patched\\) and starts it. If the patch file is missing, the entry
  says which file to get. CREDITS.md has the official download pages.
  Already included:
  - Golvellius Plus, our own patch (ROMs\\Patches\\Golvellius Plus\\; needs the original
    "Golvellius - Valley of Doom" ROM).
  - Phantasy Star Retranslation 2.6.1 by SMS Power! in 7 languages, the zips exactly as released
    (ROMs\\Patches\\Phantasy Star\\...; needs the original Japanese "Phantasy Star" ROM). Their
    README.md names the team and the terms: free of charge, distributed together in original
    condition, no commercial sale. Please keep the zips unchanged.

Saves and settings
  Each game keeps its saves in Games\\<game>\\saves\\ and its options (the in-game HUD: F1 or pad Start)
  in an .ini file in Games\\<game>\\.

Pictures (optional)
  Images\\ : the first .jpg / .png there is the launcher background; "<title> (front).jpg" = a cover,
  "<title> (logo).png" = a title logo (e.g. "Phantasy Star (front).jpg"). launcher.ini.sample (copy it
  to launcher.ini) lists the launcher settings.

Keys in the launcher: Up / Down select, Enter / pad A play, Esc / pad B back, F11 fullscreen.
"""

# Folders under ROMs/Patches whose zips may be passed on unchanged. PSRP 2.6.1 README ("Disclaimer"):
# "Any enclosed files are offered at no charge and must be distributed together in original condition.
#  These files may not be released for commercial sales without explicit authorisation ..."
REDISTRIBUTABLE = [
    os.path.join("Phantasy Star", "Retranslation 2.6.1 (PSRP, SMS Power)"),
]

ROMS_README = """Your ROMs go here (any file names; the launcher matches them by CRC32).
Patches\\ : patch files (IPS / BPS, loose or in the author's .zip).
Patched\\ : ROMs the launcher makes from a ROM + a patch.
"""


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    with_images = "--with-images" in sys.argv
    out = os.path.abspath(args[0]) if args else os.path.join(ROOT, "Release")
    roms = os.path.join(ROOT, "ROMs")
    for d in ("Games", "ROMs", os.path.join("ROMs", "Patches"), os.path.join("ROMs", "Patched"), "Images"):
        os.makedirs(os.path.join(out, d), exist_ok=True)

    sdl = os.path.join(ROOT, "Launcher", "build", "SDL2.dll")
    launcher = os.path.join(ROOT, "Launcher", "build", "Launcher.exe")
    if not os.path.isfile(launcher) or not os.path.isfile(sdl):
        sys.exit("build the launcher first (Launcher/build.ps1)")
    copy_if_changed(launcher, os.path.join(out, "Launcher.exe"))
    copy_if_changed(sdl, os.path.join(out, "SDL2.dll"))
    copy_if_changed(os.path.join(ROOT, "Launcher", "launcher.ini.sample"), os.path.join(out, "launcher.ini.sample"))
    copy_if_changed(os.path.join(ROOT, "CREDITS.md"), os.path.join(out, "CREDITS.md"))
    with open(os.path.join(out, "README.txt"), "w", newline="\r\n") as f:
        f.write(README)
    with open(os.path.join(out, "ROMs", "README.txt"), "w", newline="\r\n") as f:
        f.write(ROMS_README)

    # our own patch: Golvellius Plus (built by Tools/gv_build_patch.py)
    gp = os.path.join(roms, "Patches", "Golvellius - Valley of Doom", "Golvellius Plus")
    if os.path.isdir(gp):
        dst = os.path.join(out, "ROMs", "Patches", "Golvellius Plus")
        os.makedirs(dst, exist_ok=True)
        for f in os.listdir(gp):
            if f.lower().endswith((".bps", ".ips", ".txt")):
                copy_if_changed(os.path.join(gp, f), os.path.join(dst, f))

    # third-party patches whose terms allow passing them on: copied as downloaded (the whole zip, unchanged)
    for rel in REDISTRIBUTABLE:
        src = os.path.join(roms, "Patches", rel)
        if not os.path.isdir(src):
            continue
        dst = os.path.join(out, "ROMs", "Patches", rel)
        os.makedirs(dst, exist_ok=True)
        for f in os.listdir(src):
            if f.lower().endswith(".zip"):
                copy_if_changed(os.path.join(src, f), os.path.join(dst, f))

    if with_images and os.path.isdir(os.path.join(ROOT, "Images")):
        for f in os.listdir(os.path.join(ROOT, "Images")):
            p = os.path.join(ROOT, "Images", f)
            if os.path.isfile(p):
                copy_if_changed(p, os.path.join(out, "Images", f))

    patches = scan_patches(os.path.join(roms, "Patches"))
    rom_crcs = {}
    for dp, _, files in os.walk(roms):
        if "Patches" in os.path.relpath(dp, roms).split(os.sep):
            continue
        for f in files:
            if f.lower().endswith((".sms", ".gg")):
                rom_crcs.setdefault(crc_file(os.path.join(dp, f)), f)

    done, skipped = [], []
    for slug in sorted(os.listdir(os.path.join(ROOT, "Games"))):
        gdir = os.path.join(ROOT, "Games", slug)
        toml_p = os.path.join(gdir, "game.toml")
        if not os.path.isfile(toml_p):
            continue
        with open(toml_p, encoding="utf-8") as f:
            text = f.read()
        rom = toml_str(text, "rom")
        name = toml_str(text, "display_name")
        bdir = os.path.join(gdir, "build")
        exes = sorted(f for f in os.listdir(bdir) if f.lower().endswith(".exe")) if os.path.isdir(bdir) else []
        rom_p = os.path.normpath(os.path.join(gdir, rom)) if rom else None
        if not exes or not rom_p or not os.path.isfile(rom_p):
            skipped.append((slug, "not built" if not exes else "ROM missing"))
            continue
        crc = crc_file(rom_p)
        rom_name = os.path.basename(rom_p)
        lines = ["# Made by Tools/make_release.py. Only the launcher reads this file.",
                 "[game]",
                 'display_name = "%s"' % (name or os.path.splitext(rom_name)[0]),
                 'crc32        = "%08X"   # the ROM this game program is made for (any file name in ROMs\\)' % crc,
                 'exe          = "%s"' % exes[0],
                 'rom_name     = "%s"   # file name of a ROM the launcher makes (ROMs\\Patched\\)' % rom_name]
        recipe = patches.get(crc)
        if recipe and recipe[0] != crc:
            base_crc, pcrc, pname, pfrom = recipe
            lines += ["# patched ROM: base ROM + patch, both found by CRC32 (ROMs\\ and ROMs\\Patches\\)",
                      'patch_base_crc32 = "%08X"   # %s' % (base_crc, rom_crcs.get(base_crc, "base ROM")),
                      'patch_crc32      = "%08X"   # BPS: the CRC32 in its last 4 bytes; IPS: of the file' % pcrc,
                      'patch_name       = "%s"' % pname,
                      'patch_from       = "%s"   # the file to put into ROMs\\Patches\\ (as downloaded)' % pfrom]
        rdir = os.path.join(out, "Games", slug)
        os.makedirs(rdir, exist_ok=True)
        with open(os.path.join(rdir, "game.toml"), "w", encoding="utf-8", newline="\r\n") as f:
            f.write("\n".join(lines) + "\n")
        changed = copy_if_changed(os.path.join(bdir, exes[0]), os.path.join(rdir, exes[0]))
        copy_if_changed(sdl, os.path.join(rdir, "SDL2.dll"))
        done.append((slug, "%08X" % crc, "patch" if recipe and recipe[0] != crc else "", "copied" if changed else "unchanged"))

    for d in done:
        print("  %-24s %s %-5s %s" % d)
    for s, why in skipped:
        print("  %-24s skipped: %s" % (s, why))
    print("%d games in %s" % (len(done), out))


if __name__ == "__main__":
    main()
