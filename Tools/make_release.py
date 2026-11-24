"""make_release.py - make the release folder (and zip) a player downloads: "<name> v<version>".

    python -I Tools/make_release.py [<out dir>] [--zip] [--msys C:/msys64]

<out dir> (default Release/) gets "<name> v<version>/" (name + version from app_info.toml) and, with
--zip, "<name> v<version>.zip" holding that folder. Needs no ROMs and nothing outside the repository but
an MSYS2 installation with mingw-w64-x86_64-{gcc,cmake,ninja,SDL2} (the GitHub workflow installs those),
so it runs the same on this PC and on GitHub (.github/workflows/release.yml).

It builds, with MSYS2's gcc: the recompiler (Recomp/smsggrecomp/recompiler/build), the launcher
(Launcher/build) and the release kit (Tools/release_kit -> Release/_kit_build), copies the C compiler
(Tools/make_toolchain.py: whole MSYS2 packages) and assembles:

    <name> v<version>/
      Launcher.exe, SDL2.dll        start here
      README.txt, LICENSE.txt, CREDITS.md, launcher.ini.sample
      Images/                       the app's own art (no game art)
      ROMs/README.txt               where the player's ROMs go: ROMs/<platform>/ (sms = .sms, gg = .gg ...)
      ROMs/sms/Patches/             patches the launcher applies itself: our Golvellius Plus
                                    (Patches/sms/Golvellius/dist) and the PSRP 2.6.1 zips, unchanged
                                    (ThirdParty/sms/Patches: their terms allow passing them on)
      Games/sms/<slug>/game.toml    every game folder's game.toml + a [release] section (CRC32 of its ROM,
                                    exe name, hook library, optimisation, patch recipe); no program: the
                                    launcher builds it on the player's PC from the player's ROM
      Engine/SmsRecomp.exe          the recompiler
      Engine/Compiler/              gcc (mingw64/ + SOURCES.txt: package versions, where the sources are)
      Engine/sms/                   build.txt (how release_build.c builds a game), include/ (the headers the
                                    generated C includes), lib/ (libsmsgg_runner.a, libhooks_<Family>.a,
                                    app_icon.o), SmsInterp.exe (title screens before a game is built)
      Engine/LICENSES/              the engine's licences (PolyForm Noncommercial + permissions, MIT parts)

NOTHING derived from a ROM is in it: no game programs, no generated C, no title screens, no ROMs, no
saves, no game art. The folder is rebuilt from scratch on every run (it must be a fresh release folder,
recognised by its release_info.txt; another folder of that name is never deleted).
"""
import os, re, shutil, stat, subprocess, sys, tomllib, zipfile, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINK_LIBS = "-lSDL2 -lgdi32 -luser32 -lkernel32 -lshell32 -lole32 -ladvapi32"
RECOMPILERS = {"sms": "SmsRecomp.exe"}          # platform -> recompiler (Engine/<exe>)


def rm_tree(p):
    """shutil.rmtree that also removes read-only files (MSYS2 installs some read-only)"""
    def fix(func, path, _exc):
        os.chmod(path, stat.S_IWRITE | stat.S_IREAD)
        func(path)
    shutil.rmtree(p, onexc=fix)


def crc_bytes(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def run(cmd, env, cwd=None):
    print("> " + " ".join(cmd))
    r = subprocess.run(cmd, env=env, cwd=cwd)
    if r.returncode:
        sys.exit("failed (%d): %s" % (r.returncode, " ".join(cmd)))


def cmake_build(src, bdir, env, mingw, extra=()):
    if not os.path.isfile(os.path.join(bdir, "CMakeCache.txt")):
        run([os.path.join(mingw, "bin", "cmake.exe"), "-S", src, "-B", bdir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
             "-DCMAKE_C_COMPILER=" + os.path.join(mingw, "bin", "gcc.exe").replace("\\", "/"),
             "-DCMAKE_MAKE_PROGRAM=" + os.path.join(mingw, "bin", "ninja.exe").replace("\\", "/"),
             "-DCMAKE_PREFIX_PATH=" + mingw.replace("\\", "/")] + list(extra), env)
    else:
        run([os.path.join(mingw, "bin", "cmake.exe"), bdir], env)
    run([os.path.join(mingw, "bin", "cmake.exe"), "--build", bdir], env)


def bps_crcs(data):
    """(source crc, target crc, patch id) of a BPS file; patch id = the CRC32 in its last 4 bytes (the
    CRC32 of a whole BPS file is the same constant for every BPS)"""
    if len(data) < 16 or data[:4] != b"BPS1":
        return None
    return (int.from_bytes(data[-12:-8], "little"), int.from_bytes(data[-8:-4], "little"),
            int.from_bytes(data[-4:], "little"))


def scan_patches(pdir):
    """target crc -> (source crc, patch id, patch file name, file to put into ROMs/Patches)"""
    out = {}
    for dp, _, files in os.walk(pdir):
        for f in sorted(files):
            p = os.path.join(dp, f)
            if f.lower().endswith(".zip"):
                with zipfile.ZipFile(p) as z:
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


README = """%(name)s v%(version)s
%(line)s

Native PC versions of 8-bit Sega games (Master System), made on YOUR PC from YOUR OWN ROMs by static
recompilation, with optional extras (maps, options, quality-of-life features). No games are included.
%(repository)s

1. Put your ROM files into ROMs\\sms\\ (see ROMs\\README.txt). Any file names: the launcher recognises
   every ROM by its checksum.
2. Start Launcher.exe. It lists every game it has a profile for and whose ROM it found, and takes each
   game's title screen from your ROM in the background (a few seconds per game).
3. Choose a game and press Enter (pad A). The first time, the launcher builds the game from your ROM:
   it recompiles the ROM to C and compiles that with the C compiler in Engine\\Compiler (about 1-2
   minutes, Golvellius about 5). After that the game starts at once.

The games run in their own window. In a game: F1 (pad Start) opens the options / map overlay, Esc asks
before quitting, Tab = fast forward, F11 = fullscreen.

Patched games (translations, Golvellius Plus)
  They need the original ROM and the patch file in ROMs\\sms\\Patches\\. Included:
  - Golvellius Plus, our own patch (needs the original "Golvellius - Valley of Doom" ROM).
  - Phantasy Star Retranslation 2.6.1 by SMS Power! in 7 languages, the zips exactly as released
    (needs the first Japanese "Phantasy Star" ROM, CRC32 6605D36A; the Japanese Rev 1 does not work).
    Terms (their README.md): free of charge, distributed together in original condition, no
    commercial sale. Please keep the zips unchanged.
  A patched game is listed once its original ROM is in ROMs\\. The launcher makes the patched ROM
  (ROMs\\sms\\Patched\\) when you start such a game.

Saves and settings
  Each game keeps its saves in Games\\sms\\<game>\\saves\\ and its options in an .ini file next to it.

Pictures
  Your own pictures go into Images\\sms\\: "<title> (front).jpg" = a cover, "<title> (logo).png" = a
  title logo (e.g. "Phantasy Star (front).jpg"); they win over the captured title screens
  ("<game> (title).png", delete one to capture it again). launcher.ini.sample (copy it to
  launcher.ini) lists the launcher settings.

Keys in the launcher: Up / Down select, Enter / pad A play, R / pad Y rebuild, Esc / pad B back,
F11 fullscreen.

Not affiliated with or endorsed by Sega. All game names are trademarks of their owners and are used
only to say which game a profile is for. Use ROMs you own.

Licences: our own code MIT (LICENSE.txt); the recompiler engine (smsggrecomp, z80-recomp-core)
PolyForm Noncommercial 1.0.0 (Engine\\LICENSES\\), so the whole may not be sold; the C compiler: see
Engine\\Compiler\\SOURCES.txt; credits: CREDITS.md.
"""

ROMS_README = """Your ROMs go into one folder per console:

  ROMs\\sms\\   Sega Master System ROMs (.sms)
  ROMs\\gg\\    Sega Game Gear ROMs (.gg)        (no Game Gear games have a profile yet)
  (more consoles later get their own folder: nes, gb, ...)

Any file names, subfolders are fine: the launcher recognises every ROM by its CRC32 checksum. A ROM
that no game profile is made for is listed in grey. Start the launcher again after adding ROMs.

ROMs\\<console>\\Patches\\  patch files (IPS / BPS, loose or in the author's .zip)
ROMs\\<console>\\Patched\\  ROMs the launcher makes from a ROM + a patch
"""


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    msys = "C:\\msys64"
    if "--msys" in sys.argv:
        msys = sys.argv[sys.argv.index("--msys") + 1]
        args = [a for a in args if a != msys]
    msys = os.path.abspath(msys)
    mingw = os.path.join(msys, "mingw64")
    with open(os.path.join(ROOT, "app_info.toml"), "rb") as f:
        info = tomllib.load(f)
    if not re.fullmatch(r"\d+\.\d+\.\d+", info.get("version", "")):
        sys.exit("app_info.toml: version must be MAJOR.MINOR.PATCH")
    title = "%s v%s" % (info["name"], info["version"])
    out_parent = os.path.abspath(args[0]) if args else os.path.join(ROOT, "Release")
    rel = os.path.join(out_parent, title)

    env = dict(os.environ, PATH=os.path.join(mingw, "bin") + os.pathsep + os.environ.get("PATH", ""))
    for t in ("gcc.exe", "cmake.exe", "ninja.exe"):
        if not os.path.isfile(os.path.join(mingw, "bin", t)):
            sys.exit("MSYS2 tool missing: %s" % os.path.join(mingw, "bin", t))

    # 1. build: recompiler, launcher, release kit
    eng = os.path.join(ROOT, "Recomp", "smsggrecomp")
    cmake_build(os.path.join(eng, "recompiler"), os.path.join(eng, "recompiler", "build"), env, mingw)
    cmake_build(os.path.join(ROOT, "Launcher"), os.path.join(ROOT, "Launcher", "build"), env, mingw)
    kit = os.path.join(out_parent, "_kit_build")
    cmake_build(os.path.join(ROOT, "Tools", "release_kit"), kit, env, mingw)

    # 2. fresh release folder
    if os.path.exists(rel):
        if not os.path.isfile(os.path.join(rel, "release_info.txt")):
            sys.exit("%s exists and is not a release folder made by this script: not deleted" % rel)
        rm_tree(rel)
    os.makedirs(rel)
    with open(os.path.join(rel, "release_info.txt"), "w", newline="\r\n") as f:
        commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
        dirty = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
        f.write("%s\nversion %s\nsource %s (commit %s%s)\n" % (info["name"], info["version"], info.get("repository", ""),
                                                              commit or "?", ", with local changes" if dirty else ""))

    def cp(src, dst):
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)

    cp(os.path.join(ROOT, "Launcher", "build", "Launcher.exe"), os.path.join(rel, "Launcher.exe"))
    cp(os.path.join(mingw, "bin", "SDL2.dll"), os.path.join(rel, "SDL2.dll"))
    cp(os.path.join(ROOT, "Launcher", "launcher.ini.sample"), os.path.join(rel, "launcher.ini.sample"))
    cp(os.path.join(ROOT, "CREDITS.md"), os.path.join(rel, "CREDITS.md"))
    cp(os.path.join(ROOT, "LICENSE"), os.path.join(rel, "LICENSE.txt"))
    with open(os.path.join(rel, "README.txt"), "w", newline="\r\n") as f:
        f.write(README % dict(info, line="=" * len(title)))
    for f in os.listdir(os.path.join(ROOT, "Images")):
        p = os.path.join(ROOT, "Images", f)
        if os.path.isfile(p) and f.lower().endswith((".png", ".ico")):
            cp(p, os.path.join(rel, "Images", f))
    for d in RECOMPILERS:                        # ROMs\<platform>\ for every platform the release can build
        os.makedirs(os.path.join(rel, "ROMs", d), exist_ok=True)
    with open(os.path.join(rel, "ROMs", "README.txt"), "w", newline="\r\n") as f:
        f.write(ROMS_README)

    # 3. engine: recompiler, compiler, kit per platform, licences
    e = os.path.join(rel, "Engine")
    for plat, exe in RECOMPILERS.items():
        cp(os.path.join(eng, "recompiler", "build", exe), os.path.join(e, exe))
    run([sys.executable, "-I", os.path.join(ROOT, "Tools", "make_toolchain.py"), os.path.join(e, "Compiler"), "--msys", msys], env)
    with open(os.path.join(kit, "kit_flags.txt")) as f:
        defs = [d for d in f.read().strip().split(";") if d]
    k = os.path.join(e, "sms")
    for f in ("libsmsgg_runner.a", "app_icon.o") + tuple(x for x in os.listdir(kit) if re.fullmatch(r"libhooks_\w+\.a", x)):
        cp(os.path.join(kit, f), os.path.join(k, "lib", f))
    cp(os.path.join(kit, "SmsInterp.exe"), os.path.join(k, "SmsInterp.exe"))
    hdr = {}
    for d in (os.path.join(eng, "external", "z80-recomp-core", "include"), os.path.join(eng, "runner")):
        for f in os.listdir(d):
            if f.endswith(".h"):
                if f in hdr:
                    sys.exit("header name used twice: %s (%s, %s)" % (f, hdr[f], d))
                hdr[f] = d
                cp(os.path.join(d, f), os.path.join(k, "include", f))
    with open(os.path.join(k, "build.txt"), "w", newline="\r\n") as f:
        f.write("# How Launcher.exe --build (Launcher/release_build.c) builds a game of this platform.\n"
                "# Made by Tools/make_release.py from the release kit (Tools/release_kit: the runner's compile definitions).\n"
                "recompiler = %s\ncflags = -DNDEBUG %s\nlibs = %s\n" % (RECOMPILERS["sms"], " ".join("-D" + d for d in defs), LINK_LIBS))
    lic = os.path.join(e, "LICENSES")
    cp(os.path.join(eng, "LICENSE"), os.path.join(lic, "smsggrecomp", "LICENSE"))
    cp(os.path.join(eng, "PERMISSIONS.md"), os.path.join(lic, "smsggrecomp", "PERMISSIONS.md"))
    core = os.path.join(eng, "external", "z80-recomp-core")
    cp(os.path.join(core, "LICENSE"), os.path.join(lic, "z80-recomp-core", "LICENSE"))
    cp(os.path.join(core, "PERMISSIONS.md"), os.path.join(lic, "z80-recomp-core", "PERMISSIONS.md"))
    shutil.copytree(os.path.join(core, "LICENSES"), os.path.join(lic, "z80-recomp-core", "LICENSES"))
    cp(os.path.join(eng, "runner", "audio", "LICENSE.emu2413"), os.path.join(lic, "emu2413", "LICENSE"))
    cp(os.path.join(eng, "runner", "external", "superzazu", "LICENSE"), os.path.join(lic, "superzazu-z80", "LICENSE"))
    with open(os.path.join(eng, "runner", "external", "stb", "stb_image.h"), encoding="utf-8", errors="replace") as f:
        stb = f.read()
    i = stb.rfind("This software is available under 2 licenses")
    if i < 0:
        sys.exit("stb_image.h: licence text not found")
    os.makedirs(os.path.join(lic, "stb_image"), exist_ok=True)
    with open(os.path.join(lic, "stb_image", "LICENSE.txt"), "w", newline="\r\n") as f:
        f.write("stb_image.h (Sean Barrett), the licence text at the end of the header:\n\n" + stb[i:])

    # 4. patches the launcher can apply
    gp_src = os.path.join(ROOT, "Patches", "sms", "Golvellius", "dist")
    for f in os.listdir(gp_src):
        cp(os.path.join(gp_src, f), os.path.join(rel, "ROMs", "sms", "Patches", "Golvellius Plus", f))
    tp = os.path.join(ROOT, "ThirdParty", "sms", "Patches")
    for dp, _, files in os.walk(tp):
        for f in files:
            if f.lower().endswith(".zip"):
                cp(os.path.join(dp, f), os.path.join(rel, "ROMs", "sms", "Patches", os.path.relpath(dp, tp), f))
    cp(os.path.join(ROOT, "ThirdParty", "README.md"), os.path.join(rel, "ROMs", "sms", "Patches", "THIRD-PARTY.md"))
    os.makedirs(os.path.join(rel, "ROMs", "sms", "Patched"), exist_ok=True)
    patches = scan_patches(os.path.join(rel, "ROMs", "sms", "Patches"))

    # 5. game profiles
    games = []
    for plat in sorted(os.listdir(os.path.join(ROOT, "Games"))):
        pdir = os.path.join(ROOT, "Games", plat)
        if not os.path.isdir(pdir):
            continue
        for slug in sorted(os.listdir(pdir)):
            gdir = os.path.join(pdir, slug)
            tp_ = os.path.join(gdir, "game.toml")
            if not os.path.isfile(tp_):
                continue
            games.append(release_game(plat, slug, gdir, tp_, rel, patches))
    for g in games:
        print("  %-24s %s  %-8s %-16s %s%s" % g)
    print("%d game profiles" % len(games))

    smoke_test(rel)

    if "--zip" in flags:
        zpath = os.path.join(out_parent, title + ".zip")
        tmp = zpath + ".tmp"
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for dp, _, files in os.walk(rel):
                for f in sorted(files):
                    p = os.path.join(dp, f)
                    z.write(p, os.path.join(title, os.path.relpath(p, rel)))
        os.replace(tmp, zpath)
        print("zip: %s (%.0f MB)" % (zpath, os.path.getsize(zpath) / 1e6))
    size = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(rel) for f in fs)
    print("release: %s (%.0f MB)" % (rel, size / 1e6))


def smoke_rom():
    """A 32 KB test ROM of our own (no game in it): di / im 1 / ld sp / ei, then an endless loop; IRQ +
    NMI handlers return; a valid TMR SEGA header (Master System, export, 32 KB) with its checksum."""
    rom = bytearray(0x8000)
    rom[0x0000:0x000B] = bytes([0xF3, 0xED, 0x56, 0x31, 0xF0, 0xDF, 0xFB, 0xC3, 0x00, 0x01, 0x00])
    rom[0x0038:0x003B] = bytes([0xFB, 0xED, 0x4D])          # ei ; reti
    rom[0x0066:0x0068] = bytes([0xED, 0x45])                # retn
    rom[0x0100:0x0102] = bytes([0x18, 0xFE])                # jr $
    rom[0x7FF0:0x7FF8] = b"TMR SEGA"
    s = sum(rom[0:0x7FF0]) & 0xFFFF
    rom[0x7FFA:0x7FFC] = s.to_bytes(2, "little")
    rom[0x7FFF] = 0x4C
    return bytes(rom)


def smoke_test(rel):
    """Build a test game in the finished release exactly like a player's PC does (Launcher.exe --build,
    PATH = only the Windows folders: everything must come from the release), run it, run SmsInterp.exe on
    it; then remove it again. Fails the release if anything is missing."""
    gdir = os.path.join(rel, "Games", "sms", "_smoketest")
    if os.path.exists(gdir):
        rm_tree(gdir)
    os.makedirs(gdir)
    rom = os.path.join(gdir, "smoketest.sms")
    data = smoke_rom()
    with open(rom, "wb") as f:
        f.write(data)
    with open(os.path.join(gdir, "game.toml"), "w", newline="\r\n") as f:
        f.write('[game]\noutput_prefix = "smoketest"\ndisplay_name = "smoke test"\nplatform = "sms"\nrom = "smoketest.sms"\n\n'
                '[release]\ncrc32 = "%08X"\nexe = "SmokeTest.exe"\nhooks = ""\noptimization = "O1"\n' % crc_bytes(data))
    sysroot = os.environ.get("SystemRoot", r"C:\Windows")
    clean = {k: v for k, v in os.environ.items() if k.upper() not in ("PATH", "C_INCLUDE_PATH", "LIBRARY_PATH", "CPATH", "MSYSTEM")}
    clean["PATH"] = os.pathsep.join([os.path.join(sysroot, "System32"), sysroot])
    clean["SMS_NO_SAVE"] = "1"
    r = subprocess.run([os.path.join(rel, "Launcher.exe"), "--build", gdir, rom], env=clean, capture_output=True, text=True)
    log = r.stdout + r.stderr
    exe = os.path.join(gdir, "build", "SmokeTest.exe")
    if r.returncode or not os.path.isfile(exe):
        sys.exit("smoke test: the release cannot build a game:\n" + log[-4000:])
    for cmd in ([exe, rom, "--frames", "120"], [os.path.join(rel, "Engine", "sms", "SmsInterp.exe"), rom, "--frames", "120"]):
        r = subprocess.run(cmd, env=clean, capture_output=True, text=True, timeout=120)
        out = r.stdout + r.stderr
        if r.returncode or "stopped after 120 frames" not in out:
            sys.exit("smoke test: %s did not run:\n%s" % (os.path.basename(cmd[0]), out[-3000:]))
    rm_tree(gdir)
    print("smoke test: a game built and ran with only the release on PATH (OK)")


def release_game(plat, slug, gdir, toml_path, rel, patches):
    with open(toml_path, encoding="utf-8") as f:
        text = f.read()
    m = re.search(r'^\s*crc32\s*=\s*"([0-9A-Fa-f]{8})"', text, re.M) or re.search(r"\bCRC32 ([0-9A-F]{8})\b", text)
    if not m:
        sys.exit("%s: no ROM CRC32 (a `crc32 = \"...\"` key or a \"CRC32 XXXXXXXX\" comment)" % toml_path)
    crc = int(m.group(1), 16)
    rm = re.search(r'^\s*rom\s*=\s*"([^"]*)"', text, re.M)
    rom_name = os.path.basename(rm.group(1)) if rm else slug + ".sms"
    if rm:                                      # a local ROM (developer PC): must be the one the CRC32 says
        rp = os.path.normpath(os.path.join(gdir, rm.group(1)))
        if os.path.isfile(rp):
            with open(rp, "rb") as f:
                got = crc_bytes(f.read())
            if got != crc:
                sys.exit("%s: game.toml says CRC32 %08X, the ROM %s is %08X" % (toml_path, crc, rp, got))
    with open(os.path.join(gdir, "CMakeLists.txt"), encoding="utf-8") as f:
        cml = f.read()
    pm = re.search(r"^\s*project\(\s*(\w+)", cml, re.M)
    if not pm:
        sys.exit("%s/CMakeLists.txt: no project()" % gdir)
    fams = sorted(set(re.findall(r"Common/%s/(\w+)/" % re.escape(plat), cml)))
    if len(fams) > 1:
        sys.exit("%s: hook code from several Common folders: %s" % (gdir, fams))
    opt = "O2"
    for f in ("build.ps1", "CMakeLists.txt"):
        p = os.path.join(gdir, f)
        if os.path.isfile(p):
            om = re.search(r"Optimization\s+(O[123])|SMSGG_OPTIMIZATION\s+\"?(O[123])", open(p, encoding="utf-8").read())
            if om:
                opt = om.group(1) or om.group(2)
                break
    # the recompiler needs the key but gets the ROM on its command line (the launcher finds it by CRC32):
    # the developer tree's relative path becomes just the file name
    text = re.sub(r'^(\s*)rom(\s*=\s*)"[^"]*"', lambda mo: '%srom%s"%s"   # not read: the launcher passes the ROM it found by CRC32'
                  % (mo.group(1), mo.group(2), rom_name), text, count=1, flags=re.M)
    lines = ["", "# ---- added by Tools/make_release.py ----",
             "[release]",
             'crc32        = "%08X"   # the ROM this profile is made for (any file name in ROMs\\)' % crc,
             'exe          = "%s.exe"   # the program the launcher builds into build\\' % pm.group(1),
             'hooks        = "%s"   # Engine\\%s\\lib\\libhooks_<this>.a ("" = none)' % (fams[0] if fams else "", plat),
             'optimization = "%s"   # gcc -O level of the generated C' % opt,
             'rom_name     = "%s"   # file name of a ROM the launcher makes (ROMs\\%s\\Patched\\)' % (rom_name, plat)]
    recipe = patches.get(crc)
    if recipe and recipe[0] != crc:
        base, pid, pname, pfrom = recipe
        lines += ['patch_base_crc32 = "%08X"   # the base ROM (found by CRC32 in ROMs\\)' % base,
                  'patch_crc32      = "%08X"   # BPS: the CRC32 in its last 4 bytes; IPS: of the file' % pid,
                  'patch_name       = "%s"' % pname,
                  'patch_from       = "%s"   # the file to put into ROMs\\%s\\Patches\\ (as downloaded)' % (pfrom, plat)]
    rdir = os.path.join(rel, "Games", plat, slug)
    os.makedirs(rdir, exist_ok=True)
    with open(os.path.join(rdir, "game.toml"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(text.rstrip("\n") + "\n" + "\n".join(lines) + "\n")
    if fams:                                    # data files our hook code reads from Common/<plat>/<Family>/
        cdir = os.path.join(ROOT, "Common", plat, fams[0])
        for f in os.listdir(cdir):
            if f.endswith(".txt"):
                shutil.copy2(os.path.join(cdir, f), os.path.join(rdir, f))
    return (slug, "%08X" % crc, opt, fams[0] if fams else "-", "patch: " if recipe and recipe[0] != crc else "", recipe[2] if recipe and recipe[0] != crc else "")


if __name__ == "__main__":
    main()
