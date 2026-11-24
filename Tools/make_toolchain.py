"""make_toolchain.py - copy the C compiler a release ships out of an MSYS2 MinGW installation.

    python -I Tools/make_toolchain.py <out dir> [--msys C:/msys64]

The release builds every game on the player's PC (the game program is made from the player's ROM), so it
ships a C compiler. It ships WHOLE packages (user decision 2026-11-24: no hand-picked files, nothing can
be missing later): the packages ROOTS and everything they depend on, resolved from pacman's database
(Depends On, with virtual names resolved through Provides). Every file of every one of those packages is
copied to the same place under <out>/mingw64/ (MSYS2's gcc finds its headers through the sysroot
<gcc dir>/../../mingw64, so the folder must have that name). Each package brings its own licence folder
(mingw64/share/licenses/<name>/).

<out>/SOURCES.txt lists the packages and versions and where their sources are (GCC and binutils are GPL:
the sources of exactly these versions must stay obtainable).

At the end the copy compiles, links and runs a probe program on its own (PATH = only <out>/mingw64/bin
and the Windows folders), so a broken copy shows up here and not on a player's PC.
"""
import os, re, shutil, stat, subprocess, sys, tempfile

ROOTS = ["mingw-w64-x86_64-gcc", "mingw-w64-x86_64-SDL2"]
# what a game exe links besides the kit libraries (Launcher.exe --build reads Engine/<plat>/build.txt)
LINK_LIBS = ["-lSDL2", "-lgdi32", "-luser32", "-lkernel32", "-lshell32", "-lole32", "-ladvapi32"]


def rm_tree(p):
    """shutil.rmtree that also removes read-only files (MSYS2 installs some read-only)"""
    def fix(func, path, _exc):
        os.chmod(path, stat.S_IWRITE | stat.S_IREAD)
        func(path)
    shutil.rmtree(p, onexc=fix)


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def installed_packages(pacman):
    """name -> (version, [depends], [provides]) of every installed package"""
    out = run([pacman, "-Qi"]).stdout
    pkgs = {}
    for block in out.split("\n\n"):
        f = {}
        key = None
        for line in block.splitlines():
            m = re.match(r"^(\S[^:]*?)\s*:\s?(.*)$", line)
            if m:
                key = m.group(1).strip(); f[key] = m.group(2).strip()
            elif key and line.startswith(" "):
                f[key] += " " + line.strip()
        if "Name" not in f:
            continue
        lst = lambda k: [] if f.get(k, "None") == "None" else f[k].split()
        pkgs[f["Name"]] = (f.get("Version", "?"), lst("Depends On"), lst("Provides"))
    return pkgs


def closure(pkgs, roots):
    provides = {}
    for name, (_, _, prov) in pkgs.items():
        for p in prov:
            provides.setdefault(re.split(r"[<>=]", p)[0], name)
    done, todo = set(), list(roots)
    while todo:
        dep = re.split(r"[<>=]", todo.pop())[0]
        name = dep if dep in pkgs else provides.get(dep)
        if not name:
            sys.exit("dependency %s is not installed" % dep)
        if name in done:
            continue
        done.add(name)
        todo.extend(pkgs[name][1])
    return sorted(done)


def main():
    args = sys.argv[1:]
    if not args or args[0].startswith("--"):
        sys.exit(__doc__)
    top = os.path.abspath(args[0])
    msys = os.path.abspath(args[args.index("--msys") + 1]) if "--msys" in args else "C:\\msys64"
    pacman = os.path.join(msys, "usr", "bin", "pacman.exe")
    if not os.path.isfile(pacman):
        sys.exit("no pacman in %s" % msys)
    pkgs = installed_packages(pacman)
    names = closure(pkgs, ROOTS)

    if os.path.isdir(top):
        rm_tree(top)
    nfiles = 0
    for name in names:
        r = run([pacman, "-Ql", name])
        for line in r.stdout.splitlines():
            path = line.split(" ", 1)[1]
            if path.endswith("/"):
                continue
            if not path.startswith("/mingw64/"):
                sys.exit("%s: file outside /mingw64: %s" % (name, path))
            src = os.path.join(msys, path.lstrip("/").replace("/", os.sep))
            dst = os.path.join(top, path.lstrip("/").replace("/", os.sep))
            if not os.path.isfile(src):
                sys.exit("%s: missing installed file %s (reinstall the package)" % (name, src))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)          # contents only: no read-only flags in the release
            nfiles += 1

    with open(os.path.join(top, "SOURCES.txt"), "w", newline="\r\n") as f:
        f.write("mingw64\\ holds complete MSYS2 MinGW-w64 packages: the C compiler that builds the games.\n"
                "Licences: mingw64\\share\\licenses\\<name>\\. GCC and binutils: GNU GPL v3; the GCC runtime\n"
                "libraries carry the GCC Runtime Library Exception, so programs built with it are not affected.\n"
                "Sources of exactly these versions: https://repo.msys2.org/mingw/sources/ (file\n"
                "<package name without the mingw-w64-x86_64- prefix>-<version>.src.tar.zst), build recipes:\n"
                "https://github.com/msys2/MINGW-packages\n\n")
        for name in names:
            f.write("%s %s\n" % (name, pkgs[name][0]))

    # self test: compile + link + run a probe with nothing but the copy
    tmp = tempfile.mkdtemp(prefix="tc_probe_")
    probe = os.path.join(tmp, "probe.c")
    with open(probe, "w") as f:
        f.write("#include <stdint.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <string.h>\n"
                "#include <setjmp.h>\n#include <SDL2/SDL.h>\n"
                "int main(int argc, char **argv){ (void)argc; (void)argv; static jmp_buf jb; if (setjmp(jb)) return 1;"
                " SDL_version v; SDL_VERSION(&v); printf(\"probe ok %d %d\\n\", (int)strlen(\"abc\"), v.major); return 0; }\n")
    bindir = os.path.join(top, "mingw64", "bin")
    sysroot = os.environ.get("SystemRoot", r"C:\Windows")
    clean = {k: v for k, v in os.environ.items() if k.upper() not in ("PATH", "C_INCLUDE_PATH", "LIBRARY_PATH", "CPATH")}
    clean["PATH"] = os.pathsep.join([bindir, os.path.join(sysroot, "System32"), sysroot])
    exe = os.path.join(tmp, "probe.exe")
    r = run([os.path.join(bindir, "gcc.exe"), "-O2", "-Dmain=SDL_main", probe, "-o", exe, "-lmingw32", "-lSDL2main"] + LINK_LIBS,
            env=clean, cwd=tmp)
    if r.returncode:
        sys.exit("self test: the copy cannot build the probe:\n" + r.stderr)
    r = run([exe], env=clean)
    if "probe ok 3 2" not in r.stdout:
        sys.exit("self test: the probe did not run: %r %r" % (r.stdout, r.stderr))
    shutil.rmtree(tmp, ignore_errors=True)
    size = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(top) for f in fs)
    print("toolchain: %d packages, %d files, %.0f MB -> %s (self test OK)" % (len(names), nfiles, size / 1e6, top))


if __name__ == "__main__":
    main()
