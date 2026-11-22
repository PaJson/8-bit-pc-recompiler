#!/usr/bin/env python3
"""Create Games/<slug>/ for one ROM: game.toml (from a template), CMakeLists.txt, build/check/play scripts.

  python -I Tools/mkgame.py <slug> "<ROM file name in ROMs/>" "<display name>" [--template ps_reva] [--hooks ps]

Everything generated is plain text and safe to regenerate. The ROM itself stays in ROMs/;
game.toml points at it with a relative path; saves go to Games/<slug>/saves/.
"""
import sys, os, re, zlib, argparse

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

CMAKE = r'''cmake_minimum_required(VERSION 3.20)
project(%(proj)s LANGUAGES C)
set(SMSGG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../Recomp/smsggrecomp" CACHE PATH "SMS/GG framework")
include("${SMSGG_ROOT}/cmake/SmsGame.cmake")
sms_add_game(%(proj)s)
%(extra)s'''

CMAKE_PS = r'''
# Phantasy Star QoL hooks (auto-map, EXP/Meseta/encounter options) shared by all PS builds.
target_sources(%(proj)s PRIVATE ../../Common/PhantasyStar/ps_automap.c ../../Common/PhantasyStar/ps_qol.c ../../Common/PhantasyStar/ps_worldmap.c ../../Common/PhantasyStar/ps_panel.c ../../Common/PhantasyStar/ps_items.c ../../Common/PhantasyStar/ps_fly.c ../../Common/PhantasyStar/ps_sound.c ../../Common/PhantasyStar/ps_fixes.c)
'''

BUILD = r'''# Regenerate C from the ROM, build the runner. Usage: ./build.ps1 [-Jobs 4]
param([int]$Jobs=4)
$ErrorActionPreference='Stop'
$env:PATH="C:\msys64\mingw64\bin;$env:PATH"
Set-Location $PSScriptRoot
$fw = (Resolve-Path (Join-Path $PSScriptRoot '..\..\Recomp\smsggrecomp')).Path
$rom = (Resolve-Path (Join-Path $PSScriptRoot '%(rompath)s')).Path
& (Join-Path $fw 'recompiler\build\SmsRecomp.exe') $rom --game game.toml 2>&1 | Select-String 'cg_emit|error|ERROR'
& (Join-Path $fw 'tools\build_game.ps1') -ProjectRoot $PSScriptRoot -SkipRegenerate -Jobs $Jobs 2>&1 | Select-Object -Last 2
'''

CHECK = r'''# Headless soak test: ./check.ps1 [-Frames 20000] [-Press <press file>]  -> prints dispatch misses
param([int]$Frames=20000, [string]$Press='')
$ErrorActionPreference='Stop'
$env:SMS_NO_SAVE=1
Set-Location $PSScriptRoot
$rom = (Resolve-Path '%(rompath)s').Path
$args2 = @($rom,'--frames',$Frames,'--report','report.json')
if($Press){ $args2 += @('--press-file',(Resolve-Path $Press).Path) }
Set-Location build; Remove-Item dispatch_misses.log -ErrorAction SilentlyContinue
& .\%(proj)s.exe @args2 2>&1 | Select-String 'miss|exec\]|timing|stopped|does not match'
"--- dispatch_misses.log:"; Get-Content dispatch_misses.log -ErrorAction SilentlyContinue
'''

PLAY = r'''# Play. ./play.ps1 [-Scale 3] [-Speed 1.0]   Keys: arrows, Z/X = buttons, Enter = Start, Tab = x4 turbo, Esc = quit
param([int]$Scale = 3, [double]$Speed = 1.0)
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force saves | Out-Null
$env:SMS_SAVE_DIR = (Resolve-Path saves).Path
& .\build\%(proj)s.exe (Resolve-Path '%(rompath)s').Path --window $Scale --speed $Speed
'''

def toml_header(name, rom_file, rom_bytes, template):
    """The comment block at the top of game.toml, made from the ROM itself (not copied from the template)."""
    size = len(rom_bytes)
    banks = "%d x 16 KB banks" % (size // 0x4000) if size % 0x4000 == 0 else "%d x 16 KB banks + %d bytes" % (size // 0x4000, size % 0x4000)
    return ("# game.toml - %s\n# ROM: ROMs/%s\n#      %d KB (%s), CRC32 %08X (the build refuses any other ROM)\n"
            "# Made by Tools/mkgame.py from Tools/templates/%s.game.toml.\n"
            % (name, rom_file, size // 1024, banks, zlib.crc32(rom_bytes) & 0xFFFFFFFF, template))

def set_toml_header(toml, header):
    """Replace the leading comment lines of a game.toml with header; drop the templates' crc32 placeholder."""
    lines = toml.split("\n")
    i = 0
    while i < len(lines) and (lines[i].startswith("#") or not lines[i].strip()): i += 1
    body = "\n".join(lines[i:])
    body = re.sub(r"\n# crc32 = 0x_+[^\n]*", "", body)
    return header + "\n" + body

def qol_hooks_block(rom_bytes):
    """[insn_hooks] for the PS QoL features this ROM's code allows (Tools/ps_find_hooks.py)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("ps_find_hooks", os.path.join(ROOT, "Tools", "ps_find_hooks.py"))
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    return m.block(m.hooks_for(rom_bytes))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("slug"); ap.add_argument("rom"); ap.add_argument("name")
    ap.add_argument("--template", default="ps_reva")
    ap.add_argument("--hooks", default="ps")
    ap.add_argument("--auto-tables", action="store_true", help="detect [jump_tables] with ps_find_tables.py")
    ap.add_argument("--byte-table", action="append", default=[], metavar="ADDR:BANK:COUNT:PAGE",
                    help="byte jump table (entry = low byte, target = PAGE<<8|byte); repeatable")
    ap.add_argument("--table-count", action="append", default=[], metavar="ADDR=N", help="override a table's entry count")
    ap.add_argument("--banked", action="append", default=[], metavar="ADDR:BANK", help="extra function entry in a bank")
    ap.add_argument("--unrolled", action="store_true",
                    help="register every instruction of unrolled block-I/O runs (outi/outd/ini/ldi x>=8, ROM $0000-$7FFF) "
                         "as a function entry: games enter them at computed offsets (Duff's device, e.g. Golvellius VRAM copy)")
    ap.add_argument("--qol-hooks", action="store_true", help="add the PS QoL [insn_hooks] (walk speed, damage "
                    "multipliers) found in this ROM by Tools/ps_find_hooks.py (each feature only if all its sites match)")
    a = ap.parse_args()
    rom_path = os.path.join(ROOT, "ROMs", a.rom)
    if not os.path.isfile(rom_path): sys.exit("ROM not found: " + rom_path)
    d = os.path.join(ROOT, "Games", a.slug); os.makedirs(d, exist_ok=True)
    proj = "".join(p.capitalize() for p in re.split(r"[-_]", a.slug)) + "Recomp"
    prefix = re.sub(r"[^a-z0-9]", "", a.slug.lower())
    rel = "../../ROMs/" + a.rom
    toml = open(os.path.join(ROOT, "Tools", "templates", a.template + ".game.toml"), encoding="utf-8").read()
    toml = set_toml_header(toml, toml_header(a.name, a.rom.replace("\\", "/"), open(rom_path, "rb").read(), a.template))
    toml = re.sub(r'output_prefix\s*=\s*"[^"]*"', 'output_prefix = "%s"' % prefix, toml)
    if re.search(r'display_name\s*=', toml):
        toml = re.sub(r'display_name\s*=\s*"[^"]*"', 'display_name  = "%s"' % a.name, toml)
    else:                               # templates without one (sms_generic): the window title, launcher name
        toml = re.sub(r'(\noutput_prefix\s*=[^\n]*)', lambda m: m.group(1) + '\ndisplay_name  = "%s"' % a.name, toml, count=1)
    toml = re.sub(r'\nrom\s*=\s*"[^"]*"[^\n]*', '\nrom           = "%s"' % rel, toml)
    if a.auto_tables:
        import subprocess
        out = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "Tools", "ps_find_tables.py"), a.rom],
                             capture_output=True, text=True, check=True).stdout.splitlines()
        keep = [l for l in out if l.startswith(("addr", "bank", "count"))]
        notes = [l for l in out if l.startswith("#")]
        toml = re.sub(r"\naddr\s*=.*?\ncount\s*=[^\n]*", "\n" + "\n".join(keep), toml, count=1, flags=re.S)
        toml += "\n# jump-table detection (Tools/ps_find_tables.py):\n" + "\n".join(notes) + "\n"
    if a.byte_table:
        def arr(name):
            m = re.search(r"\n" + name + r"\s*=\s*\[(.*?)\]", toml)
            return m, [x.strip() for x in m.group(1).split(",")]
        n_tables = len(arr("addr")[1])
        add = {"addr": [], "bank": [], "count": []}; page = ["-1"] * n_tables
        for bt in a.byte_table:
            ad, bk, ct, pg = bt.split(":")
            add["addr"].append(ad); add["bank"].append(bk); add["count"].append(ct); page.append(pg)
        for name in ("addr", "bank", "count"):
            m, items = arr(name)
            toml = toml[:m.start()] + "\n%s = [%s]" %(name + " " * (5 - len(name)), ", ".join(items + add[name])) + toml[m.end():]
        m, _ = arr("count")
        toml = toml[:m.end()] + "\npage  = [%s]   # >=0: byte table, target = (page<<8)|byte" % ", ".join(page) + toml[m.end():]
    for tc in a.table_count:          # ADDR=N : override the entry count of the table at ADDR
        ad, n = tc.split("=")
        m = re.search(r"\naddr\s*=\s*\[(.*?)\]", toml); addrs = [int(x, 0) for x in m.group(1).split(",")]
        m2 = re.search(r"\ncount\s*=\s*\[(.*?)\]", toml); counts = [x.strip() for x in m2.group(1).split(",")]
        counts[addrs.index(int(ad, 0))] = n
        toml = toml[:m2.start()] + "\ncount = [%s]" % ", ".join(counts) + toml[m2.end():]
    if a.banked:                      # ADDR:BANK : extra function entries that live in a specific bank
        flat = []
        for b in a.banked: flat += b.split(":")
        toml = toml.replace("\nextra = []", "\nextra = []\nbanked = [%s]   # (addr, bank) pairs" % ", ".join(flat), 1)
    if a.unrolled:
        rom = open(rom_path, "rb").read()[:0x8000]
        ents = []
        for m in re.finditer(rb"(?:\xed[\xa0\xa2\xa3\xa8\xaa\xab]){8,}", rom):
            ents += list(range(m.start(), m.end(), 2))
        if ents:
            toml = toml.replace("\nextra = []", "\nextra = [%s]   # unrolled block-I/O run entries (mkgame --unrolled)"
                                % ", ".join("0x%04X" % e for e in ents), 1)
    if a.qol_hooks:
        toml += "\n" + qol_hooks_block(open(rom_path, "rb").read())
    toml += "\n# generated by: mkgame.py %s\n" % " ".join(sys.argv[1:])
    w = lambda n, t: open(os.path.join(d, n), "w", encoding="utf-8", newline="\n").write(t)
    w("game.toml", toml)
    v = dict(proj=proj, rompath=rel, extra=CMAKE_PS % dict(proj=proj) if a.hooks == "ps" else "")
    w("CMakeLists.txt", CMAKE % v); w("build.ps1", BUILD % v); w("check.ps1", CHECK % v); w("play.ps1", PLAY % v)
    print("created", d, "project", proj, "prefix", prefix)

if __name__ == "__main__":
    main()
