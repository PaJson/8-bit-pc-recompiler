/*
 * release_build.c - build one game in a RELEASE folder: `Launcher.exe --build <game folder> <rom>`.
 *
 * The release ships no game programs (they are made from the player's ROM) and no CMake / PowerShell
 * build scripts. It ships, per platform, a build kit (Tools/make_release.py):
 *
 *   Engine\<recompiler>.exe               e.g. SmsRecomp.exe: ROM + game.toml -> generated\*.c
 *   Engine\Compiler\mingw64\              whole MSYS2 gcc packages (Tools/make_toolchain.py)
 *   Engine\<platform>\build.txt           how to build: `recompiler`, `cflags`, `libs` (see below)
 *   Engine\<platform>\include\            the headers the generated C includes
 *   Engine\<platform>\lib\                libsmsgg_runner.a, libhooks_<Family>.a, app_icon.o
 *
 * and the game folder's game.toml has, besides the recompiler's own sections, a [release] section:
 * `exe` (the program's file name), `hooks` (the libhooks_<Family>.a it links, "" = none) and
 * `optimization` (O1 / O2 for the generated C; Golvellius' 21 MB file builds at O1).
 *
 * Steps (each line "[n/4] ..." is what the launcher shows while it runs; everything goes to stdout,
 * which the launcher sends to Games\<platform>\<slug>\build_log.txt):
 *   1 recompile: <recompiler> "<rom>" --game game.toml   (cwd = the game folder -> generated\)
 *   2 compile <prefix>_full.c, _dispatch.c, _layout.c at the same time -> build\obj\
 *   3 link -> build\<exe> (+ SDL2.dll copied from the release folder)
 *   4 done
 * The compiler runs with Engine\Compiler\mingw64\bin first in PATH (its DLLs live there).
 * Exit code 0 = the program exists.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include "release_build.h"

static HANDLE s_out;

static void say(const char *fmt, ...)
{
    char buf[8192]; va_list ap; va_start(ap, fmt); int n = vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    DWORD w; WriteFile(s_out, buf, (DWORD)n, &w, NULL);
}

/* a step: into the log and into <game folder>\build_step.txt ("<n>/4 <text>"), which the launcher reads
   for its progress display (the log's last line is often a long command or compiler output) */
static void step(const char *gdir, int n, const char *text)
{
    say("[%d/4] %s\n", n, text);
    char p[MAX_PATH], tmp[MAX_PATH + 4]; snprintf(p, sizeof p, "%s\\build_step.txt", gdir); snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE *f = fopen(tmp, "w");
    if (f) { fprintf(f, "%d/4 %s\n", n, text); fclose(f); MoveFileExA(tmp, p, MOVEFILE_REPLACE_EXISTING); }
}

/* `key = "value"` or `key = value` (first match in the file; section names are not looked at) */
static bool kv(const char *path, const char *key, char *out, size_t n)
{
    FILE *f = fopen(path, "r"); if (!f) return false;
    char line[4096]; bool ok = false; size_t kl = strlen(key);
    while (!ok && fgets(line, sizeof line, f)) {
        char *p = line; while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, key, kl) || (p[kl] != ' ' && p[kl] != '=' && p[kl] != '\t')) continue;
        char *q = strchr(p + kl, '='); if (!q) continue;
        q++; while (*q == ' ' || *q == '\t') q++;
        char *e;
        if (*q == '"') { q++; e = strchr(q, '"'); if (!e) continue; }
        else { e = q + strcspn(q, "#\r\n"); while (e > q && (e[-1] == ' ' || e[-1] == '\t')) e--; }
        snprintf(out, n, "%.*s", (int)(e - q), q); ok = true;
    }
    fclose(f);
    return ok;
}

static bool exists(const char *p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }

/* start a process with our stdout / stderr; returns its handle (NULL = failed) */
static HANDLE spawn(const char *cmd, const char *cwd)
{
    char *c = _strdup(cmd); if (!c) return NULL;
    STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = s_out; si.hStdError = s_out; si.hStdInput = NULL;
    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessA(NULL, c, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi);
    free(c);
    if (!ok) { say("cannot start: %s (error %lu)\n", cmd, GetLastError()); return NULL; }
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

static DWORD finish(HANDLE h)
{
    if (!h) return 1;
    WaitForSingleObject(h, INFINITE);
    DWORD code = 1; GetExitCodeProcess(h, &code); CloseHandle(h);
    return code;
}

int release_build(const char *root, const char *gdir, const char *rom)
{
    s_out = GetStdHandle(STD_OUTPUT_HANDLE);
    char toml[MAX_PATH], plat[64], buildtxt[MAX_PATH], engine[MAX_PATH];
    snprintf(toml, sizeof toml, "%s\\game.toml", gdir);
    {   /* platform = the name of the folder above the game folder (Games\<platform>\<slug>) */
        char tmp[MAX_PATH]; snprintf(tmp, sizeof tmp, "%s", gdir);
        char *s = strrchr(tmp, '\\'); if (s) *s = 0;
        s = strrchr(tmp, '\\'); snprintf(plat, sizeof plat, "%s", s ? s + 1 : "sms");
    }
    snprintf(engine, sizeof engine, "%s\\Engine", root);
    snprintf(buildtxt, sizeof buildtxt, "%s\\%s\\build.txt", engine, plat);
    char prefix[128], exe[200], hooks[128] = "", opt[8] = "O2", recomp[200], cflags[2048], libs[2048];
    if (!kv(toml, "output_prefix", prefix, sizeof prefix) || !kv(toml, "exe", exe, sizeof exe)) {
        say("game.toml has no output_prefix / exe: %s\n", toml); return 1; }
    kv(toml, "hooks", hooks, sizeof hooks);
    kv(toml, "optimization", opt, sizeof opt);
    if (strcmp(opt, "O1") && strcmp(opt, "O2") && strcmp(opt, "O3")) snprintf(opt, sizeof opt, "O2");
    if (!kv(buildtxt, "recompiler", recomp, sizeof recomp) || !kv(buildtxt, "cflags", cflags, sizeof cflags)
        || !kv(buildtxt, "libs", libs, sizeof libs)) { say("missing or incomplete %s\n", buildtxt); return 1; }

    char gcc[MAX_PATH], binpath[MAX_PATH], inc[MAX_PATH], lib[MAX_PATH], recomp_exe[MAX_PATH];
    snprintf(binpath, sizeof binpath, "%s\\Compiler\\mingw64\\bin", engine);
    snprintf(gcc, sizeof gcc, "%s\\gcc.exe", binpath);
    snprintf(inc, sizeof inc, "%s\\%s\\include", engine, plat);
    snprintf(lib, sizeof lib, "%s\\%s\\lib", engine, plat);
    snprintf(recomp_exe, sizeof recomp_exe, "%s\\%s", engine, recomp);
    if (!exists(gcc)) { say("the compiler is missing: %s\n", gcc); return 1; }
    if (!exists(recomp_exe)) { say("the recompiler is missing: %s\n", recomp_exe); return 1; }
    {   /* the compiler's DLLs: its bin folder first in PATH (inherited by every step) */
        static char path[32768]; DWORD n = GetEnvironmentVariableA("PATH", path, sizeof path);
        static char np[32768 + MAX_PATH];
        snprintf(np, sizeof np, "%s;%s", binpath, n && n < sizeof path ? path : "");
        SetEnvironmentVariableA("PATH", np);
    }

    step(gdir, 1, "Recompiling the ROM to C");
    char cmd[8192];
    snprintf(cmd, sizeof cmd, "\"%s\" \"%s\" --game \"%s\"", recomp_exe, rom, toml);
    say("> %s\n", cmd);
    if (finish(spawn(cmd, gdir))) { say("recompiling failed\n"); return 1; }

    char obj[MAX_PATH], bdir[MAX_PATH];
    snprintf(bdir, sizeof bdir, "%s\\build", gdir); CreateDirectoryA(bdir, NULL);
    snprintf(obj, sizeof obj, "%s\\obj", bdir); CreateDirectoryA(obj, NULL);
    step(gdir, 2, "Compiling the C code");
    const char *parts[3] = { "full", "dispatch", "layout" };
    HANDLE hs[3];
    for (int i = 0; i < 3; i++) {
        snprintf(cmd, sizeof cmd, "\"%s\" -%s %s -I\"%s\" -I\"%s\\generated\" -c \"%s\\generated\\%s_%s.c\" -o \"%s\\%s.o\"",
                 gcc, opt, cflags, inc, gdir, gdir, prefix, parts[i], obj, parts[i]);
        say("> %s\n", cmd);
        hs[i] = spawn(cmd, gdir);
    }
    DWORD bad = 0;
    for (int i = 0; i < 3; i++) bad |= finish(hs[i]);
    if (bad) { say("compiling failed\n"); return 1; }

    step(gdir, 3, "Linking the game");
    char out[MAX_PATH]; snprintf(out, sizeof out, "%s\\%s", bdir, exe);
    char hl[200] = "";
    if (hooks[0]) snprintf(hl, sizeof hl, "-lhooks_%s ", hooks);
    snprintf(cmd, sizeof cmd, "\"%s\" -o \"%s\" \"%s\\full.o\" \"%s\\dispatch.o\" \"%s\\layout.o\" \"%s\\app_icon.o\" -L\"%s\" %s-lsmsgg_runner %s",
             gcc, out, obj, obj, obj, lib, lib, hl, libs);
    say("> %s\n", cmd);
    if (finish(spawn(cmd, gdir)) || !exists(out)) { say("linking failed\n"); return 1; }
    char sdl_src[MAX_PATH], sdl_dst[MAX_PATH];
    snprintf(sdl_src, sizeof sdl_src, "%s\\SDL2.dll", root);
    snprintf(sdl_dst, sizeof sdl_dst, "%s\\SDL2.dll", bdir);
    if (!CopyFileA(sdl_src, sdl_dst, FALSE)) { say("cannot copy %s\n", sdl_src); return 1; }
    step(gdir, 4, "Build ready");
    say("%s\n", out);
    return 0;
}
