/*
 * launcher.c - game launcher for the recompiled Master System games of this project.
 *
 * Lists every game folder Games/<slug>/ (game.toml) whose ROM is present in ROMs/, plus .sms files in
 * ROMs/ that no game folder is made for ("no build profile"). ROMs are matched by CRC32 (every .sms /
 * .gg in ROMs/ and 3 subfolder levels, not ROMs/Patches), so their file names do not matter. The CRC32
 * of a game folder = `crc32 = "XXXXXXXX"` in its game.toml (Release layout, Tools/make_release.py) or
 * the CRC32 of the file its `rom = "..."` names (developer tree).
 * Patched ROMs: a game.toml with `patch_base_crc32`, `patch_crc32` (+ `patch_name`, `patch_from`,
 * `rom_name`) whose ROM is missing but whose base ROM is present is listed as "Play makes the patched
 * ROM first": the IPS / BPS patch is found by its CRC32 in ROMs/Patches (any name, loose or inside a
 * .zip), applied, checked against `crc32` and written to ROMs/Patched/<rom_name>. Without the patch
 * file the entry says which file to put there.
 * A game is started like its play.ps1 does: <exe> "<rom>" --window <scale>, working directory = the
 * game folder, SMS_SAVE_DIR = Games/<slug>/saves, no console window (output -> Games/<slug>/run.log), focus
 * handed to the game's window before the launcher hides; exe = Games/<slug>/build/<`exe = "..."` of
 * game.toml> (release) or the .exe in Games/<slug>/build. A game that is not built yet is built first,
 * output in Games/<slug>/build_log.txt (this recompiles the ROM and can take minutes): in the developer
 * tree by its build.ps1, in a release folder (Tools/make_release.py: no build scripts, but Engine\ with
 * the recompiler, a C compiler and the prebuilt runner) by `Launcher.exe --build <game folder> <rom>`
 * (release_build.c). The launcher hides while a game runs.
 *
 * Background: `background = <path>` in launcher.ini, else the app's Images/Background.png (committed,
 * like Logo.png, Icon.png, IconLarge.png, Icon.ico), else another picture in Images/, else a generic
 * background drawn here. Over any background but the app's own, the PRESS START screen shows
 * Images/Logo.png. Transparent borders of every picture are trimmed when it is loaded.
 * Game art (optional, the user's own, in Images/ or Images/<platform>/, see runner/art.h):
 * <title> (front).jpg|png = the cover shown beside the list for the selected game (background
 * dimmed behind it, cross-fade), <title> (logo).png = its title logo (white outline added) above the
 * details. <title> is matched against the game's name / ROM file name ("Phantasy Star" = every
 * Phantasy Star entry); `art.<slug> = <title>` in launcher.ini picks a title by hand. Files named
 * "<x> (<kind>)" are never taken as the background.
 * Without a cover, a game shows its own title screen: Images/<platform>/<display name>
 * (title).png, captured from the player's ROM by the game exe (--title-shot) in the background, one
 * game at a time (title_capture_tick); a game not built yet by Engine\<platform>\SmsInterp.exe where
 * that exists (release: the engine's generic interpreter, no game code in it); drawn pixel-sharp. Delete the file to capture it again;
 * game.toml `title_frame = N` picks the frame by hand. Test env: LAUNCHER_NO_TITLES=1 (no capture),
 * LAUNCHER_CAPTURE_ONLY=1 (capture every missing title without a window, then quit).
 * Text uses the engine HUD font (runner/hud.c). Images are decoded with GDI+ (part of Windows).
 *
 * Start screen: the background with a blinking PRESS START; Enter / click / pad Start shows the game
 * list (fades in), Esc quits. In the list: Enter / pad A / double click play, R / pad Y rebuild,
 * Esc / pad B / Back return to the start screen (while a build runs, Esc quits). launcher.ini:
 * `splash = 0` skips it, `press_start_y = 50` = its height (% of the window, centre of the text).
 * Keys: Up/Down (PgUp/PgDn, Home/End) select, Enter play (build first if needed), R rebuild,
 * F11 / Alt+Enter fullscreen. Gamepad: D-pad / stick, A play, Y rebuild.
 * Mouse: wheel, click selects, double click plays.
 *
 * launcher.ini (project root, optional): background = <image path>, panel = right | left,
 * window_scale = 3 (the games' window scale), fullscreen = 0 | 1.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "hud.h"
#include "art.h"
#include "release_build.h"

#ifndef APP_NAME                                /* from app_info.toml (Launcher/CMakeLists.txt) */
#define APP_NAME "8-bit PC Recompiler"
#endif
#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

/* ---------------- GDI+ flat API (declared here: mingw's gdiplus headers are C++) ---------------- */
typedef struct { UINT32 GdiplusVersion; void *DebugEventCallback; BOOL SuppressBackgroundThread; BOOL SuppressExternalCodecs; } GpStartupInput;
typedef struct { INT X, Y, Width, Height; } GpRect;
typedef struct { UINT Width, Height; INT Stride; INT PixelFormat; void *Scan0; UINT_PTR Reserved; } GpBitmapData;
int WINAPI GdiplusStartup(ULONG_PTR *token, const GpStartupInput *input, void *output);
int WINAPI GdipCreateBitmapFromFile(const WCHAR *filename, void **bitmap);
int WINAPI GdipGetImageWidth(void *image, UINT *width);
int WINAPI GdipGetImageHeight(void *image, UINT *height);
int WINAPI GdipBitmapLockBits(void *bitmap, const GpRect *rect, UINT flags, INT format, GpBitmapData *data);
int WINAPI GdipBitmapUnlockBits(void *bitmap, GpBitmapData *data);
int WINAPI GdipDisposeImage(void *image);
#define GP_FORMAT_32BPP_ARGB 0x26200A
#define GP_LOCK_READ 1

static uint32_t *load_image(const char *path, int *w, int *h)
{
    static ULONG_PTR token;
    if (!token) { GpStartupInput in = { 1, NULL, FALSE, FALSE }; if (GdiplusStartup(&token, &in, NULL) != 0) return NULL; }
    WCHAR wp[MAX_PATH];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, MAX_PATH) && !MultiByteToWideChar(CP_ACP, 0, path, -1, wp, MAX_PATH)) return NULL;
    void *bmp = NULL;
    if (GdipCreateBitmapFromFile(wp, &bmp) != 0 || !bmp) return NULL;
    UINT iw = 0, ih = 0; GdipGetImageWidth(bmp, &iw); GdipGetImageHeight(bmp, &ih);
    uint32_t *px = NULL;
    GpRect r = { 0, 0, (INT)iw, (INT)ih }; GpBitmapData d;
    if (iw && ih && GdipBitmapLockBits(bmp, &r, GP_LOCK_READ, GP_FORMAT_32BPP_ARGB, &d) == 0) {
        px = (uint32_t *)malloc((size_t)iw * ih * 4);
        if (px) for (UINT y = 0; y < ih; y++) memcpy(px + (size_t)y * iw, (uint8_t *)d.Scan0 + (size_t)y * d.Stride, (size_t)iw * 4);
        GdipBitmapUnlockBits(bmp, &d);
    }
    GdipDisposeImage(bmp);
    *w = (int)iw; *h = (int)ih;
    return px;
}

/* ---------------- paths, ini ---------------- */
static char g_root[MAX_PATH];                 /* project root (has Games/ and ROMs/) */
static char g_bg_path[MAX_PATH];
static bool g_panel_left;
static int  g_game_scale = 3;
static bool g_fullscreen;
static bool g_splash = true;                   /* PRESS START screen before the list */
static int  g_press_y = 50;                    /* its height: centre of the text, % of the window height */
static Uint32 g_splash_t, g_list_t;            /* start of the splash / of the list (fade-in) */
#define MAX_ART_MAP 32
static char g_art_slug[MAX_ART_MAP][64], g_art_title[MAX_ART_MAP][160]; static int g_nart_map;

static bool is_dir(const char *p) { DWORD a = GetFileAttributesA(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static bool is_file(const char *p) { DWORD a = GetFileAttributesA(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

static void find_root(void)
{
    char p[MAX_PATH]; GetModuleFileNameA(NULL, p, MAX_PATH);
    for (int up = 0; up < 6; up++) {
        char *s = strrchr(p, '\\'); if (!s) break; *s = 0;
        char a[MAX_PATH], b[MAX_PATH];
        snprintf(a, sizeof a, "%s\\Games", p); snprintf(b, sizeof b, "%s\\ROMs", p);
        if (is_dir(a) && is_dir(b)) { snprintf(g_root, sizeof g_root, "%s", p); return; }
    }
    GetCurrentDirectoryA(MAX_PATH, g_root);
}

static void trim(char *s) { char *e = s + strlen(s); while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0; char *b = s; while (*b == ' ' || *b == '\t') b++; memmove(s, b, strlen(b) + 1); }

static void load_ini(void)
{
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\launcher.ini", g_root);
    FILE *f = fopen(p, "r"); if (!f) return;
    char line[600];
    while (fgets(line, sizeof line, f)) {
        char *c = strchr(line, ';'); if (c) *c = 0;
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = 0; char *k = line, *v = eq + 1; trim(k); trim(v);
        if (!strcmp(k, "background")) {
            if (v[0] && (v[1] == ':' || v[0] == '\\' || v[0] == '/')) snprintf(g_bg_path, sizeof g_bg_path, "%s", v);
            else if (v[0]) snprintf(g_bg_path, sizeof g_bg_path, "%s\\%s", g_root, v);
        } else if (!strcmp(k, "panel")) g_panel_left = !strcmp(v, "left");
        else if (!strcmp(k, "window_scale")) { int s = atoi(v); if (s >= 1 && s <= 8) g_game_scale = s; }
        else if (!strcmp(k, "fullscreen")) g_fullscreen = atoi(v) != 0;
        else if (!strcmp(k, "splash")) g_splash = atoi(v) != 0;
        else if (!strcmp(k, "press_start_y")) { int y = atoi(v); if (y >= 0 && y <= 100) g_press_y = y; }
        else if (!strncmp(k, "art.", 4) && k[4] && v[0] && g_nart_map < MAX_ART_MAP) {
            snprintf(g_art_slug[g_nart_map], sizeof g_art_slug[0], "%s", k + 4);
            snprintf(g_art_title[g_nart_map], sizeof g_art_title[0], "%s", v); g_nart_map++;
        }
    }
    fclose(f);
}

/* The app's own art (Images\, committed): Background.png = the launcher background (it contains the
   app's logo), Logo.png = the logo alone (shown on the PRESS START screen over any other background),
   Icon.png / IconLarge.png -> Icon.ico (Tools/make_icon.py, embedded in the exes). */
static char g_app_logo[MAX_PATH];                  /* Images\Logo.png, "" = none */
static bool g_bg_is_app;                           /* the background is Images\Background.* */

/* launcher.ini `background =`, else Images\Background.(png|jpg|jpeg|bmp), else the first other picture
   in Images\ that is not the app's logo / icon nor game art ("<title> (front|full|logo|back|title)") */
static void find_background(void)
{
    { char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\Images\\Logo.png", g_root); if (is_file(p)) snprintf(g_app_logo, sizeof g_app_logo, "%s", p); }
    static const char *const ext[] = { "png", "jpg", "jpeg", "bmp" };
    if (!(g_bg_path[0] && is_file(g_bg_path))) {
        g_bg_path[0] = 0;
        for (int i = 0; i < 4 && !g_bg_path[0]; i++) {
            char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\Images\\Background.%s", g_root, ext[i]);
            if (is_file(p)) snprintf(g_bg_path, sizeof g_bg_path, "%s", p);
        }
    }
    for (int i = 0; i < 4 && !g_bg_path[0]; i++) {
        char pat[MAX_PATH]; snprintf(pat, sizeof pat, "%s\\Images\\*.%s", g_root, ext[i]);
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            const char *f = fd.cFileName, *dot = strrchr(f, '.');
            size_t stem = dot ? (size_t)(dot - f) : strlen(f);
            if (!_strnicmp(f, "Logo.", 5) || !_strnicmp(f, "Icon", 4)) continue;
            if (stem > 0 && f[stem - 1] == ')' && (strstr(f, " (front)") || strstr(f, " (full)") || strstr(f, " (logo)") || strstr(f, " (back)") || strstr(f, " (title)"))) continue;
            snprintf(g_bg_path, sizeof g_bg_path, "%s\\Images\\%s", g_root, f); break;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    const char *b = strrchr(g_bg_path, '\\'); b = b ? b + 1 : g_bg_path;
    g_bg_is_app = !_strnicmp(b, "Background.", 11);
}

/* ---------------- files, CRC32 ---------------- */
static uint32_t crc32_buf(uint32_t crc, const uint8_t *p, size_t n)
{
    static uint32_t tab[256];
    if (!tab[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; tab[i] = c; }
    crc = ~crc;
    while (n--) crc = tab[(crc ^ *p++) & 255] ^ (crc >> 8);
    return ~crc;
}

/* whole file into memory (at most max bytes; larger = NULL) */
static uint8_t *read_file(const char *path, size_t max, size_t *size)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0 || (size_t)n > max) { fclose(f); return NULL; }
    uint8_t *b = (uint8_t *)malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) *size = (size_t)n;
    return b;
}

static bool has_ext(const char *f, const char *ext) { size_t l = strlen(f), e = strlen(ext); return l > e && !_stricmp(f + l - e, ext); }

/* ROM index: every .sms / .gg in ROMs/ (3 subfolder levels, not ROMs/Patches) with its CRC32. Games are
   matched by CRC32, so the file names do not matter. */
typedef struct { char path[MAX_PATH]; uint32_t crc; } RomFile;
static RomFile *g_romidx; static int g_nromidx, g_caproms;

static void index_roms(const char *dir, int depth)
{
    char pat[MAX_PATH]; snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth > 0 && _stricmp(fd.cFileName, "Patches")) index_roms(p, depth - 1);
            continue;
        }
        if (!has_ext(fd.cFileName, ".sms") && !has_ext(fd.cFileName, ".gg")) continue;
        size_t n; uint8_t *b = read_file(p, 8u << 20, &n);
        if (!b) continue;
        if (g_nromidx == g_caproms) { g_caproms = g_caproms ? g_caproms * 2 : 64; g_romidx = (RomFile *)realloc(g_romidx, sizeof(RomFile) * (size_t)g_caproms); }
        RomFile *r = &g_romidx[g_nromidx++];
        GetFullPathNameA(p, MAX_PATH, r->path, NULL);
        r->crc = crc32_buf(0, b, n);
        free(b);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
static const RomFile *rom_by_crc(uint32_t crc) { for (int i = 0; i < g_nromidx; i++) if (g_romidx[i].crc == crc) return &g_romidx[i]; return NULL; }
static const RomFile *rom_by_path(const char *p) { for (int i = 0; i < g_nromidx; i++) if (!_stricmp(g_romidx[i].path, p)) return &g_romidx[i]; return NULL; }

/* ---------------- patches: IPS / BPS files in ROMs/Patches, loose or inside a .zip ---------------- */
typedef struct { char path[MAX_PATH], entry[260]; uint32_t crc; } PatchFile;   /* entry "" = a loose file */
static PatchFile *g_patches; static int g_npatches, g_cappatches; static bool g_patches_done;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* the central directory of a zip: calls fn for each entry (name, crc, method, csize, usize, local header offset) */
typedef bool (*ZipFn)(void *ctx, const char *name, uint32_t crc, int method, uint32_t csize, uint32_t usize, uint32_t lofs);
static void zip_walk(const uint8_t *z, size_t n, ZipFn fn, void *ctx)
{
    if (n < 22) return;
    size_t e = n - 22, stop = n > 22 + 65535 ? n - 22 - 65535 : 0;
    while (rd32(z + e) != 0x06054b50) { if (e == stop) return; e--; }
    uint32_t cnt = rd16(z + e + 10), cd = rd32(z + e + 16);
    for (uint32_t i = 0; i < cnt && cd + 46 <= n; i++) {
        if (rd32(z + cd) != 0x02014b50) return;
        uint32_t nl = rd16(z + cd + 28), xl = rd16(z + cd + 30), cl = rd16(z + cd + 32);
        if (cd + 46 + nl > n) return;
        char name[260]; snprintf(name, sizeof name, "%.*s", (int)nl, (const char *)z + cd + 46);
        if (!fn(ctx, name, rd32(z + cd + 16), rd16(z + cd + 10), rd32(z + cd + 20), rd32(z + cd + 24), rd32(z + cd + 42))) return;
        cd += 46 + nl + xl + cl;
    }
}

static PatchFile *add_patch(void)
{
    if (g_npatches == g_cappatches) { g_cappatches = g_cappatches ? g_cappatches * 2 : 32; g_patches = (PatchFile *)realloc(g_patches, sizeof(PatchFile) * (size_t)g_cappatches); }
    PatchFile *p = &g_patches[g_npatches++]; memset(p, 0, sizeof *p); return p;
}
static uint8_t *zip_extract(const uint8_t *z, size_t n, const char *name, size_t *outn);

/* a patch's identity: IPS = CRC32 of the file; BPS = the CRC32 stored in its last 4 bytes (the CRC32 of
   a whole BPS file is the same constant for every BPS: data followed by its own CRC32) */
static uint32_t patch_id(const char *name, const uint8_t *d, size_t n)
{
    if (has_ext(name, ".bps") && n >= 16 && !memcmp(d, "BPS1", 4)) return rd32(d + n - 4);
    return crc32_buf(0, d, n);
}

typedef struct { const char *path; const uint8_t *z; size_t n; } ZipList;
static bool zip_list_fn(void *ctx, const char *name, uint32_t crc, int method, uint32_t csize, uint32_t usize, uint32_t lofs)
{
    (void)method; (void)csize; (void)usize; (void)lofs;
    ZipList *zl = (ZipList *)ctx;
    if (!has_ext(name, ".ips") && !has_ext(name, ".bps")) return true;
    uint32_t id = crc;
    if (has_ext(name, ".bps")) {                     /* the id is inside: unpack it */
        size_t dn; uint8_t *d = zip_extract(zl->z, zl->n, name, &dn);
        if (!d) return true;
        id = patch_id(name, d, dn); free(d);
    }
    PatchFile *p = add_patch();
    snprintf(p->path, sizeof p->path, "%s", zl->path); snprintf(p->entry, sizeof p->entry, "%s", name); p->crc = id;
    return true;
}
static void index_patches(const char *dir, int depth)
{
    char pat[MAX_PATH]; snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (depth > 0) index_patches(p, depth - 1); continue; }
        size_t n; uint8_t *b;
        if (has_ext(fd.cFileName, ".zip")) {
            if ((b = read_file(p, 64u << 20, &n)) != NULL) { ZipList zl = { p, b, n }; zip_walk(b, n, zip_list_fn, &zl); free(b); }
        } else if (has_ext(fd.cFileName, ".ips") || has_ext(fd.cFileName, ".bps")) {
            if ((b = read_file(p, 16u << 20, &n)) != NULL) { PatchFile *q = add_patch(); snprintf(q->path, sizeof q->path, "%s", p); q->crc = patch_id(fd.cFileName, b, n); free(b); }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
static const PatchFile *patch_by_crc(uint32_t crc)
{
    if (!g_patches_done) {                       /* ROMs\<platform>\Patches of every platform */
        g_npatches = 0; g_patches_done = true;
        char pat[MAX_PATH]; snprintf(pat, sizeof pat, "%s\\ROMs\\*", g_root);
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
                char d[MAX_PATH]; snprintf(d, sizeof d, "%s\\ROMs\\%s\\Patches", g_root, fd.cFileName); index_patches(d, 4);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    for (int i = 0; i < g_npatches; i++) if (g_patches[i].crc == crc) return &g_patches[i];
    return NULL;
}

/* stb_image's zlib (compiled into art.c) */
int stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);

typedef struct { const char *want; const uint8_t *z; size_t n; uint8_t *out; size_t outn; } ZipGet;
static bool zip_get_fn(void *ctx, const char *name, uint32_t crc, int method, uint32_t csize, uint32_t usize, uint32_t lofs)
{
    ZipGet *g = (ZipGet *)ctx;
    if (strcmp(name, g->want)) return true;
    if (lofs + 30 > g->n || rd32(g->z + lofs) != 0x04034b50) return false;
    size_t d = lofs + 30 + rd16(g->z + lofs + 26) + rd16(g->z + lofs + 28);
    if (d + csize > g->n || usize > (64u << 20)) return false;
    uint8_t *o = (uint8_t *)malloc(usize ? usize : 1);
    if (!o) return false;
    bool ok = false;
    if (method == 0 && csize == usize) { memcpy(o, g->z + d, usize); ok = true; }
    else if (method == 8) ok = stbi_zlib_decode_noheader_buffer((char *)o, (int)usize, (const char *)g->z + d, (int)csize) == (int)usize;
    if (ok && crc32_buf(0, o, usize) == crc) { g->out = o; g->outn = usize; } else free(o);
    return false;
}
static uint8_t *zip_extract(const uint8_t *z, size_t n, const char *name, size_t *outn)
{
    ZipGet g = { name, z, n, NULL, 0 };
    zip_walk(z, n, zip_get_fn, &g);
    *outn = g.outn;
    return g.out;
}
static uint8_t *patch_load(const PatchFile *p, size_t *n)
{
    if (!p->entry[0]) return read_file(p->path, 16u << 20, n);
    size_t zn; uint8_t *z = read_file(p->path, 64u << 20, &zn);
    if (!z) return NULL;
    uint8_t *d = zip_extract(z, zn, p->entry, n);
    free(z);
    return d;
}

/* IPS: "PATCH", records (3-byte offset, 2-byte size, data | size 0: 2-byte count + value), "EOF" [+ 3-byte size] */
static uint8_t *apply_ips(const uint8_t *src, size_t sn, const uint8_t *p, size_t pn, size_t *on)
{
    if (pn < 8 || memcmp(p, "PATCH", 5)) return NULL;
    size_t cap = sn, n = sn; uint8_t *o = (uint8_t *)malloc(cap ? cap : 1); if (!o) return NULL;
    memcpy(o, src, sn);
    size_t i = 5;
    for (;;) {
        if (i + 3 > pn) { free(o); return NULL; }
        uint32_t ofs = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8 | p[i + 2]; i += 3;
        if (ofs == 0x454F46) { if (i + 3 <= pn) n = (size_t)p[i] << 16 | (size_t)p[i + 1] << 8 | p[i + 2]; break; }
        if (i + 2 > pn) { free(o); return NULL; }
        size_t len = (size_t)p[i] << 8 | p[i + 1]; i += 2;
        bool rle = len == 0;
        if (rle) { if (i + 3 > pn) { free(o); return NULL; } len = (size_t)p[i] << 8 | p[i + 1]; i += 2; }
        else if (i + len > pn) { free(o); return NULL; }
        if (ofs + len > cap) { size_t c2 = ofs + len; uint8_t *o2 = (uint8_t *)realloc(o, c2); if (!o2) { free(o); return NULL; } memset(o2 + cap, 0, c2 - cap); o = o2; cap = c2; }
        if (rle) { memset(o + ofs, p[i], len); i += 1; } else { memcpy(o + ofs, p + i, len); i += len; }
        if (ofs + len > n) n = ofs + len;
    }
    if (n > cap) { uint8_t *o2 = (uint8_t *)realloc(o, n); if (!o2) { free(o); return NULL; } memset(o2 + cap, 0, n - cap); o = o2; }
    *on = n;
    return o;
}

/* BPS (beat): "BPS1", sizes, metadata, actions, CRC32 of source / target / patch (all checked) */
static bool bps_num(const uint8_t *p, size_t end, size_t *i, uint64_t *v)
{
    uint64_t d = 0, s = 1;
    for (;;) {
        if (*i >= end || s > ((uint64_t)1 << 56)) return false;
        uint8_t x = p[(*i)++];
        d += (x & 0x7F) * s;
        if (x & 0x80) break;
        s <<= 7; d += s;
    }
    *v = d; return true;
}
static uint8_t *apply_bps(const uint8_t *src, size_t sn, const uint8_t *p, size_t pn, size_t *on, const char **err)
{
    if (pn < 16 || memcmp(p, "BPS1", 4)) { *err = "not a BPS file"; return NULL; }
    if (crc32_buf(0, p, pn - 4) != rd32(p + pn - 4)) { *err = "the patch file is damaged"; return NULL; }
    if (crc32_buf(0, src, sn) != rd32(p + pn - 12)) { *err = "the patch is for another ROM"; return NULL; }
    size_t i = 4, end = pn - 12; uint64_t ss, ts, ms;
    if (!bps_num(p, end, &i, &ss) || !bps_num(p, end, &i, &ts) || !bps_num(p, end, &i, &ms) || ss != sn || ts > (64u << 20) || i + ms > end) { *err = "bad BPS header"; return NULL; }
    i += (size_t)ms;
    uint8_t *o = (uint8_t *)malloc(ts ? (size_t)ts : 1); if (!o) { *err = "out of memory"; return NULL; }
    size_t out = 0; int64_t sr = 0, tr = 0;
    while (i < end) {
        uint64_t d; if (!bps_num(p, end, &i, &d)) break;
        size_t len = (size_t)(d >> 2) + 1; int cmd = (int)(d & 3);
        if (out + len > ts) break;
        if (cmd == 0) { if (out + len > sn) break; memcpy(o + out, src + out, len); }
        else if (cmd == 1) { if (i + len > end) break; memcpy(o + out, p + i, len); i += len; }
        else {
            uint64_t v; if (!bps_num(p, end, &i, &v)) break;
            int64_t off = (int64_t)(v >> 1) * ((v & 1) ? -1 : 1);
            if (cmd == 2) { sr += off; if (sr < 0 || (uint64_t)sr + len > sn) break; memcpy(o + out, src + sr, len); sr += (int64_t)len; }
            else { tr += off; if (tr < 0 || (uint64_t)tr >= out) break; for (size_t k = 0; k < len; k++) o[out + k] = o[tr++]; }   /* may overlap: byte by byte */
        }
        out += len;
    }
    if (i != end || out != ts || crc32_buf(0, o, out) != rd32(p + pn - 8)) { free(o); *err = "applying the patch failed"; return NULL; }
    *on = out;
    return o;
}

/* ---------------- games ---------------- */
enum { ST_READY, ST_NOT_BUILT, ST_PATCH, ST_NEEDS_PATCH, ST_NO_PROFILE };
typedef struct {
    char slug[64], platform[32], name[160], dir[MAX_PATH], rom[MAX_PATH], exe[MAX_PATH], romfile[200];
    char cover[MAX_PATH], logo[MAX_PATH];        /* optional art (Images/), "" = none */
    char title[MAX_PATH];                        /* Images\<platform>\<name> (title).png: captured from the ROM */
    int  title_frame;                            /* game.toml `title_frame = N`: take that frame (0 = find it) */
    int  state;
    uint32_t crc;                                /* the ROM this build is made for */
    /* a patched ROM the launcher can make (game.toml patch_* keys): base ROM + patch, out = ROMs/Patched/<rom_name> */
    uint32_t base_crc, patch_crc; char patch_name[160], patch_from[260], rom_name[200], base_rom[MAX_PATH];
} Game;
static Game *g_games; static int g_ngames, g_capgames;

static Game *add_game(void)
{
    if (g_ngames == g_capgames) { g_capgames = g_capgames ? g_capgames * 2 : 32; g_games = (Game *)realloc(g_games, sizeof(Game) * (size_t)g_capgames); }
    Game *g = &g_games[g_ngames++]; memset(g, 0, sizeof *g); return g;
}

/* `key = "value"` from a toml file (first occurrence) */
static bool toml_str(const char *path, const char *key, char *out, size_t n)
{
    FILE *f = fopen(path, "r"); if (!f) return false;
    char line[1024]; bool ok = false; size_t kl = strlen(key);
    while (!ok && fgets(line, sizeof line, f)) {
        char *p = line; while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, key, kl) || (p[kl] != ' ' && p[kl] != '=' && p[kl] != '\t')) continue;
        char *q = strchr(p, '"'); if (!q) continue;
        char *e = strchr(q + 1, '"'); if (!e) continue;
        *e = 0; snprintf(out, n, "%s", q + 1); ok = true;
    }
    fclose(f);
    return ok;
}

static void find_exe(Game *g, const char *toml)
{
    char exe[200];                                   /* release: `exe = "<file>"` = the program in build\ */
    if (toml_str(toml, "exe", exe, sizeof exe)) {
        snprintf(g->exe, sizeof g->exe, "%s\\build\\%s", g->dir, exe);
        if (!is_file(g->exe)) g->exe[0] = 0;
        return;
    }
    char pat[MAX_PATH]; snprintf(pat, sizeof pat, "%s\\build\\*.exe", g->dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    g->exe[0] = 0;
    if (h != INVALID_HANDLE_VALUE) { snprintf(g->exe, sizeof g->exe, "%s\\build\\%s", g->dir, fd.cFileName); FindClose(h); }
}

static int cmp_games(const void *a, const void *b)
{
    const Game *x = (const Game *)a, *y = (const Game *)b;
    if ((x->state == ST_NO_PROFILE) != (y->state == ST_NO_PROFILE)) return x->state == ST_NO_PROFILE ? 1 : -1;
    return _stricmp(x->name, y->name);
}

/* ROMs no game folder is made for (by CRC32): listed grey */
static void add_unprofiled_roms(void)
{
    for (int r = 0; r < g_nromidx; r++) {
        const RomFile *rf = &g_romidx[r];
        bool used = false;
        for (int i = 0; i < g_ngames && !used; i++) used = g_games[i].state != ST_NO_PROFILE && (g_games[i].crc == rf->crc || g_games[i].base_crc == rf->crc);
        if (used) continue;
        const char *base = strrchr(rf->path, '\\'); base = base ? base + 1 : rf->path;
        if (!has_ext(base, ".sms")) continue;
        Game *g = add_game();
        snprintf(g->rom, sizeof g->rom, "%s", rf->path);
        snprintf(g->romfile, sizeof g->romfile, "%s", base);
        snprintf(g->name, sizeof g->name, "%.*s", (int)(strlen(base) - 4), base);
        g->crc = rf->crc;
        g->state = ST_NO_PROFILE;
    }
}

static bool toml_hex(const char *toml, const char *key, uint32_t *v)
{
    char s[32]; if (!toml_str(toml, key, s, sizeof s)) return false;
    char *e; unsigned long x = strtoul(s[0] == '0' && (s[1] == 'x' || s[1] == 'X') ? s + 2 : s, &e, 16);
    if (*e || e == s) return false;
    *v = (uint32_t)x; return true;
}

/* unquoted integer value `key = 123` */
static bool toml_int(const char *path, const char *key, int *v)
{
    FILE *f = fopen(path, "r"); if (!f) return false;
    char line[1024]; bool ok = false; size_t kl = strlen(key);
    while (!ok && fgets(line, sizeof line, f)) {
        char *p = line; while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, key, kl) || (p[kl] != ' ' && p[kl] != '=' && p[kl] != '\t')) continue;
        char *q = strchr(p, '='); if (!q) continue;
        char *e; long x = strtol(q + 1, &e, 10);
        if (e != q + 1) { *v = (int)x; ok = true; }
    }
    fclose(f);
    return ok;
}

static bool g_any_logo;
static bool g_ts_idle;                         /* title capture: nothing left to do (until the next scan) */
/* Covers: the user's own "<title> (front)" first; without one, the game's title screen captured from
   its ROM (Images\<platform>\<display name> (title).png, made by the game exe's --title-shot, see
   title_capture_tick; exact file name, never a prefix match: every build has its own title). */
static void find_art(Game *g)
{
    char dir[MAX_PATH]; snprintf(dir, sizeof dir, "%s\\Images", g_root);
    const char *name = g->name; char stem[200];
    snprintf(stem, sizeof stem, "%s", g->romfile); char *dot = strrchr(stem, '.'); if (dot) *dot = 0;
    for (int i = 0; i < g_nart_map; i++) if (g->slug[0] && !_stricmp(g_art_slug[i], g->slug)) name = g_art_title[i];
    if (!art_find(dir, name, "front", g->cover, sizeof g->cover)) art_find(dir, stem, "front", g->cover, sizeof g->cover);
    if (!art_find(dir, name, "logo", g->logo, sizeof g->logo)) art_find(dir, stem, "logo", g->logo, sizeof g->logo);
    if (g->logo[0]) g_any_logo = true;
    g->title[0] = 0;
    if (g->state == ST_NO_PROFILE) return;
    char fn[200]; snprintf(fn, sizeof fn, "%s", g->name);
    for (char *c = fn; *c; c++) if (strchr("<>:\"/\\|?*", *c) || (unsigned char)*c < 32) *c = '_';
    snprintf(g->title, sizeof g->title, "%s\\%s\\%s (title).png", dir, g->platform[0] ? g->platform : "sms", fn);
    if (!g->cover[0] && is_file(g->title)) snprintf(g->cover, sizeof g->cover, "%s", g->title);
}

static void scan_games(void)
{
    g_ngames = 0;
    g_nromidx = 0; g_patches_done = false;
    { char roms[MAX_PATH]; snprintf(roms, sizeof roms, "%s\\ROMs", g_root); index_roms(roms, 4); }
    /* Games\<platform>\<slug>\game.toml (platform = sms, gg, nes, gb ...) */
    char ppat[MAX_PATH]; snprintf(ppat, sizeof ppat, "%s\\Games\\*", g_root);
    WIN32_FIND_DATAA pd; HANDLE ph = FindFirstFileA(ppat, &pd);
    if (ph != INVALID_HANDLE_VALUE) do {
    if (!(pd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || pd.cFileName[0] == '.') continue;
    char plat[32]; snprintf(plat, sizeof plat, "%s", pd.cFileName);
    char pat[MAX_PATH]; snprintf(pat, sizeof pat, "%s\\Games\\%s\\*", g_root, plat);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
            char dir[MAX_PATH], toml[MAX_PATH], rom[MAX_PATH], romfull[MAX_PATH] = "";
            snprintf(dir, sizeof dir, "%s\\Games\\%s\\%s", g_root, plat, fd.cFileName);
            snprintf(toml, sizeof toml, "%s\\game.toml", dir);
            if (!is_file(toml)) continue;
            /* the ROM: by `crc32 = "..."` (Release layout) or by the CRC32 of the file `rom = "..."` names
               (developer tree); then found in ROMs/ by its CRC32 under any file name */
            uint32_t crc = 0; bool have_crc = toml_hex(toml, "crc32", &crc);
            if (toml_str(toml, "rom", rom, sizeof rom)) {
                char rp[MAX_PATH]; snprintf(rp, sizeof rp, "%s\\%s", dir, rom);
                for (char *c = rp; *c; c++) if (*c == '/') *c = '\\';
                GetFullPathNameA(rp, MAX_PATH, romfull, NULL);
                if (!is_file(romfull)) romfull[0] = 0;
                if (!have_crc && romfull[0]) {
                    const RomFile *rf = rom_by_path(romfull);
                    if (rf) { crc = rf->crc; have_crc = true; }
                    else { size_t n; uint8_t *b = read_file(romfull, 8u << 20, &n); if (b) { crc = crc32_buf(0, b, n); have_crc = true; free(b); } }
                }
            }
            if (!have_crc) continue;
            const RomFile *rf = rom_by_crc(crc);
            Game tmp; memset(&tmp, 0, sizeof tmp);
            tmp.crc = crc;
            if (rf) snprintf(tmp.rom, sizeof tmp.rom, "%s", rf->path);
            else if (romfull[0]) snprintf(tmp.rom, sizeof tmp.rom, "%s", romfull);
            int st = -1;
            if (!tmp.rom[0]) {                                /* no ROM: can the launcher make it from a patch? */
                if (!toml_hex(toml, "patch_base_crc32", &tmp.base_crc) || !toml_hex(toml, "patch_crc32", &tmp.patch_crc)) continue;
                const RomFile *base = rom_by_crc(tmp.base_crc);
                if (!base) continue;                          /* not even the base ROM: not listed */
                snprintf(tmp.base_rom, sizeof tmp.base_rom, "%s", base->path);
                toml_str(toml, "patch_name", tmp.patch_name, sizeof tmp.patch_name);
                toml_str(toml, "patch_from", tmp.patch_from, sizeof tmp.patch_from);
                if (!toml_str(toml, "rom_name", tmp.rom_name, sizeof tmp.rom_name)) snprintf(tmp.rom_name, sizeof tmp.rom_name, "%s.sms", fd.cFileName);
                st = patch_by_crc(tmp.patch_crc) ? ST_PATCH : ST_NEEDS_PATCH;
            }
            Game *g = add_game(); *g = tmp;
            snprintf(g->slug, sizeof g->slug, "%s", fd.cFileName);
            snprintf(g->platform, sizeof g->platform, "%s", plat);
            snprintf(g->dir, sizeof g->dir, "%s", dir);
            const char *rn = g->rom[0] ? g->rom : g->rom_name;
            const char *base = strrchr(rn, '\\'); snprintf(g->romfile, sizeof g->romfile, "%s", base ? base + 1 : rn);
            if (!toml_str(toml, "display_name", g->name, sizeof g->name)) {
                snprintf(g->name, sizeof g->name, "%s", g->romfile);
                char *dot = strrchr(g->name, '.'); if (dot) *dot = 0;
            }
            find_exe(g, toml);
            g->state = st >= 0 ? st : g->exe[0] ? ST_READY : ST_NOT_BUILT;
            toml_int(toml, "title_frame", &g->title_frame);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    } while (FindNextFileA(ph, &pd));
    if (ph != INVALID_HANDLE_VALUE) FindClose(ph);
    add_unprofiled_roms();
    g_any_logo = false; g_ts_idle = false;
    for (int i = 0; i < g_ngames; i++) find_art(&g_games[i]);
    qsort(g_games, (size_t)g_ngames, sizeof(Game), cmp_games);
}

/* ---------------- processes (game / build) ---------------- */
static PROCESS_INFORMATION g_proc;
static int  g_proc_kind;                      /* 0 none, 1 game, 2 build */
static char g_proc_slug[64];
static char g_status[200];                    /* message line */
static uint32_t g_status_col = 0xFFA0A0B0;
static char g_build_log[MAX_PATH];
static bool g_play_after_build;
/* build progress (drawn under the status line): elapsed time + a bar. Release builds (release_build.c)
   report 4 steps; step 2 (compiling) is long and silent, so its share of the bar follows an estimate
   from the size of the generated <prefix>_full.c (~12 s per MB measured here, at -O1 and -O2) and stops
   at 97 % of the step until the step really ends. Developer builds: ninja's [n/m]. */
static Uint32 g_build_t0, g_step_t0;
static int g_build_step;                      /* release build step 1..4, 0 = none / developer build */
static float g_build_frac;                    /* 0..1 */
static double g_compile_est_ms;
static char g_build_dir[MAX_PATH];

static bool g_wait_win; static Uint32 g_wait_win_until;   /* game started, its window not focused yet */

/* the visible top-level window of a process (the game's main window) */
typedef struct { DWORD pid; HWND found; } FindWin;
static BOOL CALLBACK find_win_cb(HWND h, LPARAM lp)
{
    FindWin *f = (FindWin *)lp; DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    char cls[64] = "";
    GetClassNameA(h, cls, sizeof cls);                 /* never a console window (Windows reports the client's pid for those) */
    if (!strcmp(cls, "ConsoleWindowClass") || !strcmp(cls, "PseudoConsoleWindow")) return TRUE;
    if (pid == f->pid && IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) { f->found = h; return FALSE; }
    return TRUE;
}
static HWND process_window(DWORD pid) { FindWin f = { pid, NULL }; EnumWindows(find_win_cb, (LPARAM)&f); return f.found; }
static HWND sdl_hwnd(SDL_Window *w)
{
    SDL_SysWMinfo info; SDL_VERSION(&info.version);
    return w && SDL_GetWindowWMInfo(w, &info) ? info.info.win.window : NULL;
}
static void take_focus(HWND h) { if (h) { if (IsIconic(h)) ShowWindow(h, SW_RESTORE); SetForegroundWindow(h); } }

static bool start_game(const Game *g)
{
    char saves[MAX_PATH]; snprintf(saves, sizeof saves, "%s\\saves", g->dir);
    CreateDirectoryA(saves, NULL);
    char cmd[3 * MAX_PATH];
    const char *extra = getenv("LAUNCHER_GAME_EXTRA");                  /* test: extra runner arguments */
    snprintf(cmd, sizeof cmd, "\"%s\" \"%s\" --window %d %s", g->exe, g->rom, g_game_scale, extra ? extra : "");
    /* The game exes are console programs: started plainly, Windows opened a console (terminal) window
       for each, which took the focus from the game's window (user report). No console now; the game's
       text output (statistics, dispatch misses) goes to <game folder>\run.log instead. */
    char logp[MAX_PATH]; snprintf(logp, sizeof logp, "%s\\run.log", g->dir);
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE log = CreateFileA(logp, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (log != INVALID_HANDLE_VALUE) { si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = log; si.hStdError = log; si.hStdInput = NULL; }
    SetEnvironmentVariableA("SMS_SAVE_DIR", saves);
    char pid[16]; snprintf(pid, sizeof pid, "%lu", (unsigned long)GetCurrentProcessId());
    SetEnvironmentVariableA("SMS_FRONTEND_PID", pid);              /* the game hands the focus back when it closes */
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, log != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW, NULL, g->dir, &si, &g_proc);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    SetEnvironmentVariableA("SMS_SAVE_DIR", NULL);
    SetEnvironmentVariableA("SMS_FRONTEND_PID", NULL);
    if (!ok) { snprintf(g_status, sizeof g_status, "Could not start %s", g->exe); g_status_col = 0xFFFF6060; return false; }
    g_proc_kind = 1; snprintf(g_proc_slug, sizeof g_proc_slug, "%s", g->slug);
    fprintf(stderr, "[launcher] started %s\n", g->slug);
    /* Focus: Windows lets only the foreground program bring a window to the front. Hiding the launcher
       right away gave the foreground to the window behind it (e.g. a terminal) before the game's window
       existed, so the game came up without the focus (user report). Now the launcher allows the game to
       take the foreground, stays visible until the game's window is there, gives it the focus itself and
       only then hides (main loop, g_wait_win). */
    AllowSetForegroundWindow(g_proc.dwProcessId);
    g_wait_win = true; g_wait_win_until = SDL_GetTicks() + 15000;
    snprintf(g_status, sizeof g_status, "Starting %s ...", g->name); g_status_col = 0xFFFFE040;
    return true;
}

/* how a game is built here: its build.ps1 (developer tree) or the release kit (Engine\<platform>\build.txt) */
static bool has_build_script(const Game *g) { char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\build.ps1", g->dir); return is_file(p); }
static bool has_release_kit(const Game *g)
{
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\Engine\\%s\\build.txt", g_root, g->platform[0] ? g->platform : "sms");
    return is_file(p);
}

static bool start_build(const Game *g, bool play_after)
{
    { char sf[MAX_PATH]; snprintf(sf, sizeof sf, "%s\\build_step.txt", g->dir); DeleteFileA(sf); }   /* release build: a fresh step file */
    snprintf(g_build_log, sizeof g_build_log, "%s\\build_log.txt", g->dir);
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE log = CreateFileA(g_build_log, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log == INVALID_HANDLE_VALUE) { snprintf(g_status, sizeof g_status, "Cannot write %s", g_build_log); g_status_col = 0xFFFF6060; return false; }
    char cmd[4 * MAX_PATH];
    if (has_build_script(g))
        snprintf(cmd, sizeof cmd, "powershell -NoProfile -ExecutionPolicy Bypass -File \"%s\\build.ps1\"", g->dir);
    else {                                           /* release: this exe in build mode (release_build.c) */
        char self[MAX_PATH]; GetModuleFileNameA(NULL, self, sizeof self);
        snprintf(cmd, sizeof cmd, "\"%s\" --build \"%s\" \"%s\"", self, g->dir, g->rom);
    }
    STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = log; si.hStdError = log; si.hStdInput = NULL;
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, g->dir, &si, &g_proc);
    CloseHandle(log);
    if (!ok) { snprintf(g_status, sizeof g_status, "Could not start the build"); g_status_col = 0xFFFF6060; return false; }
    g_proc_kind = 2; snprintf(g_proc_slug, sizeof g_proc_slug, "%s", g->slug);
    g_build_t0 = g_step_t0 = SDL_GetTicks(); g_build_step = 0; g_build_frac = 0; g_compile_est_ms = 0;
    snprintf(g_build_dir, sizeof g_build_dir, "%s", g->dir);
    g_play_after_build = play_after;
    snprintf(g_status, sizeof g_status, "Building %s ...", g->name); g_status_col = 0xFFFFE040;
    return true;
}

/* last non-empty line of the build log (shown while building) */
static void build_log_tail(char *out, size_t n)
{
    out[0] = 0;
    HANDLE f = CreateFileA(g_build_log, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz; GetFileSizeEx(f, &sz);
    char buf[2048]; DWORD got = 0;
    LONGLONG off = sz.QuadPart > (LONGLONG)sizeof buf - 1 ? sz.QuadPart - (LONGLONG)sizeof buf + 1 : 0;
    LARGE_INTEGER o; o.QuadPart = off; SetFilePointerEx(f, o, NULL, FILE_BEGIN);
    ReadFile(f, buf, sizeof buf - 1, &got, NULL); CloseHandle(f);
    buf[got] = 0;
    char *end = buf + got;
    while (end > buf && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) *--end = 0;
    char *s = end; while (s > buf && s[-1] != '\n' && s[-1] != '\r') s--;
    snprintf(out, n, "%s", s);
}

/* the build's stage from the log's last line: ninja's "[n/m] Building / Linking ...", else the
   recompiler / CMake steps before it */
/* estimated compile time of the generated C (release build step 2) from the size of <prefix>_full.c */
static double compile_estimate_ms(void)
{
    char toml[MAX_PATH], prefix[128], full[MAX_PATH];
    snprintf(toml, sizeof toml, "%s\\game.toml", g_build_dir);
    if (!toml_str(toml, "output_prefix", prefix, sizeof prefix)) return 60000;
    snprintf(full, sizeof full, "%s\\generated\\%s_full.c", g_build_dir, prefix);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExA(full, GetFileExInfoStandard, &fa)) return 60000;
    double mb = ((double)fa.nFileSizeHigh * 4294967296.0 + fa.nFileSizeLow) / 1e6;
    return mb * 12000.0 + 3000.0;
}

static void build_stage(const char *tail, char *out, size_t n)
{
    int a, b;
    const char *br = strchr(tail, '[');
    if (tail[0] == '[' && sscanf(tail, "[%d/%d]", &a, &b) == 2 && b == 4 && strstr(tail, "] ")) {   /* release build: its own words */
        snprintf(out, n, "Building: %s", strstr(tail, "] ") + 2);
        if (a != g_build_step) {
            g_build_step = a; g_step_t0 = SDL_GetTicks();
            if (a == 2) g_compile_est_ms = compile_estimate_ms();
        }
        return;
    }
    if (tail[0] == '>') { out[0] = 0; return; }        /* release build: a command line, keep the stage shown */
    if (br && sscanf(br, "[%d/%d]", &a, &b) == 2 && b > 0) {
        g_build_frac = (float)a / (float)b;
        if (strstr(br, "Linking")) snprintf(out, n, "Building: linking the game (%d/%d)", a, b);
        else snprintf(out, n, "Building: compiling (%d/%d)", a, b);
        return;
    }
    if (strstr(tail, "Build ready") || strstr(tail, "-- Build files") || strstr(tail, "-- Generating") || strstr(tail, "-- Configuring"))
        snprintf(out, n, "Building: preparing the compiler ...");
    else snprintf(out, n, "Building: recompiling the ROM to C ...");
}

/* ---------------- drawing ---------------- */
static SDL_Window *g_win; static SDL_Renderer *g_ren;
static SDL_Texture *g_bg_tex; static int g_bg_w, g_bg_h; static bool g_bg_generic;
static SDL_Texture *g_ui_tex; static uint32_t *g_ui; static int g_ui_w, g_ui_h;
static int g_sel, g_top;
/* the game whose cover / logo is shown: follows g_sel only when no navigation key / button is held and the
   selection has rested 120 ms (holding a direction scrolled through every cover = flicker; user report) */
static int g_art_sel = -1;
static int g_rows = 10;                        /* visible list rows (set while drawing): one page */

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    if (t <= 0) return a;
    if (t >= 1) return b;
    int r = (int)(((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t));
    int g = (int)(((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t));
    int bl = (int)((a & 255) + (((int)(b & 255) - (int)(a & 255)) * t));
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}
static void add_px(uint32_t *px, int w, int h, int x, int y, uint32_t col, float a)
{
    if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
    uint32_t *p = &px[(size_t)y * w + x];
    int r = (int)(((*p >> 16) & 255) + ((col >> 16) & 255) * a); if (r > 255) r = 255;
    int g = (int)(((*p >> 8) & 255) + ((col >> 8) & 255) * a);   if (g > 255) g = 255;
    int b = (int)((*p & 255) + (col & 255) * a);                  if (b > 255) b = 255;
    *p = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }

/* generic background (no logos): night sky, pixel skyline, glowing horizon, perspective grid floor,
   a gold/cyan ring with circuit traces around a pixel game pad, and the title */
static void draw_generic_bg(uint32_t *px, int w, int h, int ccx, int cx_area_w)
{
    int hz = (int)(h * 0.64f);
    for (int y = 0; y < h; y++) {
        uint32_t c = y < hz ? mix(0xFF020816, 0xFF0B3263, (float)y / hz) : mix(0xFF0A2850, 0xFF020611, (float)(y - hz) / (h - hz));
        for (int x = 0; x < w; x++) px[(size_t)y * w + x] = c;
    }
    for (int i = 0; i < w * h / 2600; i++) {                     /* stars */
        uint32_t r = hash32((uint32_t)i * 7919u + 17u);
        int x = (int)(r % (uint32_t)w), y = (int)((r >> 12) % (uint32_t)(hz - h / 10 > 1 ? hz - h / 10 : 1));
        add_px(px, w, h, x, y, 0xFF9FD8FF, 0.15f + (float)((r >> 24) & 63) / 120.f);
    }
    int unit = w / 320 > 1 ? w / 320 : 1;                        /* skyline pixel size */
    for (int x = 0, i = 0; x < w; i++) {
        uint32_t r = hash32((uint32_t)i * 2654435761u + 99u);
        int bw = (int)(unit * (6 + r % 14)), bh = (int)(h * (0.04f + (float)((r >> 8) % 100) / 100.f * 0.17f));
        if (abs(x + bw / 2 - ccx) < w / 7) bh = bh * 2 / 5;   /* lower behind the ring */
        for (int y = hz - bh; y < hz; y++)
            for (int xx = x; xx < x + bw && xx < w; xx++) {
                float edge = (xx == x || y == hz - bh) ? 0.35f : 0.f;
                px[(size_t)y * w + xx] = mix(0xFF0A2445, 0xFF2D6FB0, edge + (float)(hz - y) / (h * 0.6f));
            }
        for (int wy = hz - bh + unit * 2; wy < hz - unit * 2; wy += unit * 3)          /* windows */
            for (int wx = x + unit * 2; wx < x + bw - unit; wx += unit * 3) {
                uint32_t q = hash32((uint32_t)(wx * 31 + wy * 7));
                if ((q & 7) > 1) continue;
                uint32_t col = (q >> 8) & 1 ? 0xFFE0952F : 0xFF4FB8FF;
                for (int a = 0; a < unit; a++) for (int b = 0; b < unit; b++) add_px(px, w, h, wx + a, wy + b, col, 0.55f);
            }
        x += bw + unit * (int)(1 + (r >> 20) % 3);
    }
    for (int y = hz - h / 12; y < hz + h / 20 && y < h; y++) {   /* horizon glow */
        float d = fabsf((float)(y - hz)) / (h / 14.f), a = 0.55f * expf(-d * d * 3.f);
        for (int x = 0; x < w; x++) add_px(px, w, h, x, y, 0xFF2FA8FF, a);
    }
    for (int x = 0; x < w; x++) px[(size_t)hz * w + x] = 0xFF9FE8FF;
    for (int k = 1; k < 14; k++) {                                /* grid: horizontal lines */
        float t = (float)k / 14.f; int y = hz + (int)((h - hz) * t * t);
        for (int x = 0; x < w; x++) add_px(px, w, h, x, y, 0xFF2F9FE8, 0.15f + 0.45f * t);
    }
    int vx = ccx;                                       /* grid: lines to the vanishing point */
    for (int k = -24; k <= 24; k++) {
        float bx = vx + k * (w / 14.f);
        for (int y = hz + 1; y < h; y++) {
            float t = (float)(y - hz) / (h - hz);
            int x = (int)(vx + (bx - vx) * t);
            add_px(px, w, h, x, y, 0xFF2F9FE8, 0.12f + 0.5f * t);
        }
    }
    float cx = (float)ccx, cy = h * 0.40f, R = h * 0.25f;   /* ring */
    for (int y = (int)(cy - R * 1.3f); y < (int)(cy + R * 1.3f); y++)
        for (int x = (int)(cx - R * 1.3f); x < (int)(cx + R * 1.3f); x++) {
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
            float gold = fabsf(d - R) < R * 0.035f ? 1.f : 0.f;
            float cyan = expf(-((d - R * 0.88f) * (d - R * 0.88f)) / (R * R * 0.0008f));
            float glow = expf(-((d - R) * (d - R)) / (R * R * 0.02f)) * 0.35f;
            if (d < R * 0.85f) px[(size_t)y * w + x] = mix(px[(size_t)y * w + x], 0xFF061630, 0.75f);
            if (gold > 0) px[(size_t)y * w + x] = mix(0xFFB8862E, 0xFFF0D078, 1.f - (float)(y - (cy - R)) / (2 * R));
            add_px(px, w, h, x, y, 0xFF3FD0FF, cyan * 0.9f + glow);
        }
    for (int k = 0; k < 18; k++) {                                /* circuit traces */
        float a = (float)k / 18.f * 6.2831853f + 0.17f, len = R * (0.18f + (float)(hash32((uint32_t)k) % 100) / 100.f * 0.25f);
        for (float s = 0; s < len; s += 0.5f) {
            float rr = R * 1.04f + s;
            add_px(px, w, h, (int)(cx + cosf(a) * rr), (int)(cy + sinf(a) * rr), 0xFF3FB0FF, 0.6f);
        }
        float rr = R * 1.04f + len;
        for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++)
            if (dx * dx + dy * dy <= 5) add_px(px, w, h, (int)(cx + cosf(a) * rr) + dx, (int)(cy + sinf(a) * rr) + dy, 0xFF8FE0FF, 0.8f);
    }
    static const char *const pad[10] = {                           /* pixel game pad */
        "...XXXXXXXXXXXXXXXX...",
        "..XXXXXXXXXXXXXXXXXX..",
        ".XX..XXXXXXXXXXXXXXXX.",
        "XX.dd.XXXXXXXXXX.r.XXX",
        "X.dddddXXXXXXXXX.r.r.X",
        "X.dddddXXXXXXXX.b.r.XX",
        "XX.dd.XXXXXXXXXX.b.XXX",
        "XXX..XXXXXXXXXXXXXXXXX",
        ".XXXXXXX......XXXXXXX.",
        "..XXXXX........XXXXX..",
    };
    int ps = (int)(R * 1.25f / 22.f); if (ps < 1) ps = 1;
    int px0 = (int)cx - 11 * ps, py0 = (int)cy - 5 * ps;
    for (int r = 0; r < 10; r++)
        for (int c = 0; c < 22; c++) {
            char ch = pad[r][c]; if (ch == '.') continue;
            uint32_t col = ch == 'X' ? 0xFF1E2C44 : ch == 'd' ? 0xFF0A0F1A : ch == 'r' ? 0xFFE0402F : 0xFF2F7FE0;
            for (int yy = 0; yy < ps; yy++) for (int xx = 0; xx < ps; xx++) {
                int X = px0 + c * ps + xx, Y = py0 + r * ps + yy;
                if (X >= 0 && Y >= 0 && X < w && Y < h) px[(size_t)Y * w + X] = (yy == 0 && ch == 'X') ? 0xFF3A5478 : col;
            }
        }
    /* title (with Images\Logo.png the PRESS START screen shows that instead) */
    if (g_app_logo[0]) return;
    const char *t1 = "PC RECOMPILER", *t2 = "8-BIT CONSOLE CLASSICS";
    int s1 = cx_area_w * 7 / 10 / (6 * (int)strlen(t1)); if (s1 < 2) s1 = 2;
    int s2 = s1 * 6 / 10; if (s2 < 2) s2 = 2;
    int x1 = (int)cx - 6 * s1 * (int)strlen(t1) / 2, y1 = (int)(h * 0.70f);
    int x2 = (int)cx - 6 * s2 * (int)strlen(t2) / 2, y2 = y1 + 9 * s1;
    for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++)
        if (dx || dy) { hud_text_scaled(px, w, h, x1 + dx * s1 / 3, y1 + dy * s1 / 3, t1, 0xFF0A1E3C, s1); hud_text_scaled(px, w, h, x2 + dx * s2 / 3, y2 + dy * s2 / 3, t2, 0xFF0A1E3C, s2); }
    hud_text_scaled(px, w, h, x1, y1, t1, 0xFFF4DC8C, s1);
    hud_text_scaled(px, w, h, x2, y2, t2, 0xFF6FD8FF, s2);
}

static void make_background(void)
{
    int w, h; SDL_GetRendererOutputSize(g_ren, &w, &h);
    if (g_bg_tex && g_bg_generic && w == g_bg_w && h == g_bg_h) return;
    if (g_bg_tex && !g_bg_generic) return;
    if (g_bg_tex) SDL_DestroyTexture(g_bg_tex);
    g_bg_tex = NULL; g_bg_generic = false;
    if (g_bg_path[0]) {
        int iw, ih; uint32_t *img = load_image(g_bg_path, &iw, &ih);
        if (img) {
            g_bg_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, iw, ih);
            if (g_bg_tex) { SDL_UpdateTexture(g_bg_tex, NULL, img, iw * 4); SDL_SetTextureScaleMode(g_bg_tex, SDL_ScaleModeLinear); g_bg_w = iw; g_bg_h = ih; }
            free(img);
            if (g_bg_tex) return;
        }
    }
    uint32_t *px = (uint32_t *)malloc((size_t)w * h * 4);
    if (!px) return;
    int area = w * 58 / 100;                                     /* composition beside the panel */
    draw_generic_bg(px, w, h, g_panel_left ? w - area / 2 : area / 2, area);
    g_bg_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (g_bg_tex) SDL_UpdateTexture(g_bg_tex, NULL, px, w * 4);
    free(px);
    g_bg_w = w; g_bg_h = h; g_bg_generic = true;
}

/* s fitted into `chars` characters; a cut is shown as "..": at the end, or with `middle` in the middle,
   so both ends stay readable (names that differ only at the end: "Golvellius ... [Golvellius Plus]",
   "Phantasy Star (Retranslation 2.6.1 EN)" / "... EN literal)") */
static void fit_text(char *out, size_t n, const char *s, int chars, bool middle)
{
    int len = (int)strlen(s);
    if (chars < 1) chars = 1;
    if (len <= chars || chars < 8) { snprintf(out, n, "%.*s", chars, s); return; }
    int keep = chars - 2, tail = middle ? keep / 2 : 0, head = keep - tail;
    while (head > 1 && s[head - 1] == ' ') head--;               /* no space right before ".." */
    const char *t = s + len - tail;
    while (tail > 1 && *t == ' ') { t++; tail--; }                /* nor right after it */
    snprintf(out, n, "%.*s..%.*s", head, s, tail, t);
}

static void text(int x, int y, const char *s, uint32_t col, int sc)
{
    hud_text_scaled(g_ui, g_ui_w, g_ui_h, x + sc / 2 + 1, y + sc / 2 + 1, s, 0xC0000000u, sc);   /* shadow */
    hud_text_scaled(g_ui, g_ui_w, g_ui_h, x, y, s, col, sc);
}
static void fill(int x0, int y0, int x1, int y1, uint32_t argb)
{
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < g_ui_h; y++)
        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < g_ui_w; x++) g_ui[(size_t)y * g_ui_w + x] = argb;
}

typedef struct { int x0, y0, x1, y1, row_y, row_h, rows, sc, logo_y, logo_h; } Layout;
static Layout layout(int w, int h)
{
    Layout L; int sc = w >= 2400 ? 4 : w >= 1600 ? 3 : 2; if (h < 600) sc = 2; if (w < 900) sc = 1;
    int pw = w * 40 / 100;
    L.sc = sc;
    L.x0 = g_panel_left ? w * 2 / 100 : w - pw - w * 2 / 100; L.x1 = L.x0 + pw;
    L.y0 = h * 4 / 100; L.y1 = h - h * 4 / 100;
    L.row_h = 11 * sc;
    L.row_y = L.y0 + 30 * sc;
    L.logo_h = g_any_logo ? 40 * sc : 0;                         /* logo box above the details */
    L.logo_y = L.y1 - 64 * sc - L.logo_h;
    int bottom = L.y1 - 64 * sc - (L.logo_h ? L.logo_h + 6 * sc : 0);
    L.rows = (bottom - L.row_y) / L.row_h; if (L.rows < 1) L.rows = 1;
    return L;
}

static uint32_t state_col(int s) { return s == ST_READY ? 0xFF7CE07C : s == ST_NOT_BUILT ? 0xFFFFE040 : s == ST_PATCH ? 0xFF60D0FF : s == ST_NEEDS_PATCH ? 0xFFE09050 : 0xFF808898; }

static void draw_ui(void)
{
    int w, h; SDL_GetRendererOutputSize(g_ren, &w, &h);
    if (!g_ui_tex || w != g_ui_w || h != g_ui_h) {
        if (g_ui_tex) SDL_DestroyTexture(g_ui_tex);
        free(g_ui);
        g_ui = (uint32_t *)malloc((size_t)w * h * 4); g_ui_w = w; g_ui_h = h;
        g_ui_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        SDL_SetTextureBlendMode(g_ui_tex, SDL_BLENDMODE_BLEND);
    }
    if (!g_ui) return;
    memset(g_ui, 0, (size_t)w * h * 4);
    Layout L = layout(w, h); int sc = L.sc;
    /* panel */
    fill(L.x0, L.y0, L.x1, L.y1, 0xC0081428);
    fill(L.x0, L.y0, L.x1, L.y0 + sc, 0xFF60D0FF); fill(L.x0, L.y1 - sc, L.x1, L.y1, 0xFF60D0FF);
    fill(L.x0, L.y0, L.x0 + sc, L.y1, 0xFF60D0FF); fill(L.x1 - sc, L.y0, L.x1, L.y1, 0xFF60D0FF);
    int tx = L.x0 + 8 * sc;
    text(tx, L.y0 + 8 * sc, "GAMES", 0xFF60D0FF, sc * 2);
    char cnt[32]; snprintf(cnt, sizeof cnt, "%d", g_ngames);
    text(L.x1 - 8 * sc - 6 * 2 * sc * (int)strlen(cnt), L.y0 + 8 * sc, cnt, 0xFFA0A0B0, sc * 2);
    if (g_ngames == 0) {
        text(tx, L.row_y, "No games found.", 0xFFFFFFFF, sc);
        text(tx, L.row_y + 12 * sc, "Put your ROMs into the ROMs folder", 0xFFA0A0B0, sc);
        text(tx, L.row_y + 22 * sc, "(see ROMs\\README.txt), then start", 0xFFA0A0B0, sc);
        text(tx, L.row_y + 32 * sc, "the launcher again.", 0xFFA0A0B0, sc);
    }
    g_rows = L.rows > 1 ? L.rows : 1;
    if (g_sel < g_top) g_top = g_sel;
    if (g_sel >= g_top + L.rows) g_top = g_sel - L.rows + 1;
    if (g_top < 0) g_top = 0;
    int name_chars = (L.x1 - L.x0 - 24 * sc) / (6 * sc);
    for (int i = 0; i < L.rows && g_top + i < g_ngames; i++) {
        const Game *g = &g_games[g_top + i];
        int y = L.row_y + i * L.row_h;
        bool sel = g_top + i == g_sel;
        if (sel) fill(L.x0 + 3 * sc, y - 2 * sc, L.x1 - 3 * sc, y + 9 * sc, 0xE0204A80);
        char nm[200]; fit_text(nm, sizeof nm, g->name, name_chars, true);
        uint32_t col = g->state == ST_NO_PROFILE ? 0xFF8890A0 : sel ? 0xFFFFE040 : 0xFFFFFFFF;
        if (g->state != ST_NO_PROFILE) fill(tx, y + sc, tx + 4 * sc, y + 5 * sc, state_col(g->state));   /* status marker */
        text(tx + 7 * sc, y, nm, col, sc);
    }
    if (g_top > 0) text(L.x1 - 14 * sc, L.row_y - 11 * sc, "^", 0xFF60D0FF, sc);
    if (g_top + L.rows < g_ngames) text(L.x1 - 14 * sc, L.row_y + L.rows * L.row_h - 2 * sc, "v", 0xFF60D0FF, sc);
    /* details */
    int dy = L.y1 - 60 * sc;
    int sep = L.logo_h ? L.logo_y - 4 * sc : dy - 4 * sc;
    fill(L.x0 + 3 * sc, sep, L.x1 - 3 * sc, sep + sc / 2 + 1, 0x8060D0FF);
    if (g_sel >= 0 && g_sel < g_ngames) {
        const Game *g = &g_games[g_sel];
        char line[260];
        int chars = (L.x1 - L.x0 - 16 * sc) / (6 * sc);
        int nl = (int)strlen(g->name);                            /* full name, up to 2 lines */
        int cut = nl;
        if (nl > chars) { cut = chars; while (cut > chars / 2 && g->name[cut] != ' ') cut--; if (cut <= chars / 2) cut = chars; }
        snprintf(line, sizeof line, "%.*s", cut, g->name); text(tx, dy, line, 0xFFFFFFFF, sc);
        if (nl > cut) { const char *r = g->name + cut; while (*r == ' ') r++; fit_text(line, sizeof line, r, chars, true); text(tx, dy + 10 * sc, line, 0xFFFFFFFF, sc); }
        const char *st = g->state == ST_NO_PROFILE ? "No build profile for this ROM yet."
                       : g->state == ST_READY ? "Ready to play."
                       : g->state == ST_PATCH ? "Play makes the patched ROM first."
                       : g->state == ST_NEEDS_PATCH ? "Needs the patch file in the Patches folder:"
                       : "Not built yet: Play builds it first.";
        if (g_proc_kind == 2 && !strcmp(g->slug, g_proc_slug)) st = "This takes a few minutes.";   /* being built */
        fit_text(line, sizeof line, st, chars, false);
        text(tx, dy + 22 * sc, line, state_col(g->state), sc);
        if (g->state == ST_NEEDS_PATCH && !g_status[0]) {          /* which file, in the status line */
            fit_text(line, sizeof line, g->patch_from[0] ? g->patch_from : g->patch_name, chars, true);
            text(tx, dy + 33 * sc, line, 0xFFA0A0B0, sc);
        }
    }
    if (g_proc_kind == 2) {                                        /* building: status + elapsed time + bar */
        Uint32 el = (SDL_GetTicks() - g_build_t0) / 1000;
        char tm[24]; snprintf(tm, sizeof tm, "%u:%02u", el / 60, el % 60);
        int tw = 6 * sc * (int)strlen(tm);
        char line[260]; fit_text(line, sizeof line, g_status, (L.x1 - L.x0 - 24 * sc - tw) / (6 * sc), false);
        text(tx, dy + 33 * sc, line, g_status_col, sc);
        text(L.x1 - 8 * sc - tw, dy + 33 * sc, tm, 0xFFA0A0B0, sc);
        int bx0 = tx, bx1 = L.x1 - 8 * sc, by0 = dy + 43 * sc, by1 = by0 + 3 * sc;
        fill(bx0, by0, bx1, by1, 0xFF28406C);
        int fx = bx0 + (int)((bx1 - bx0) * (g_build_frac < 0 ? 0 : g_build_frac > 1 ? 1 : g_build_frac));
        if (fx > bx0) fill(bx0, by0, fx, by1, 0xFF60D0FF);
        int span = (bx1 - bx0) / 6, pos = (int)((SDL_GetTicks() / 4) % (Uint32)(bx1 - bx0 + span)) - span;   /* "alive": a light sweeping over the bar */
        int s0 = bx0 + pos, s1 = s0 + span;
        if (s0 < bx0) s0 = bx0;
        if (s1 > bx1) s1 = bx1;
        if (s1 > s0 && s0 < fx) fill(s0, by0, s1 < fx ? s1 : fx, by1, 0xFFC8F0FF);   /* over the done part */
        if (s1 > s0 && s1 > fx) fill(s0 > fx ? s0 : fx, by0, s1, by1, 0xFF5A88C8);   /* over the rest */
    }
    else if (g_status[0]) { char line[260]; fit_text(line, sizeof line, g_status, (L.x1 - L.x0 - 16 * sc) / (6 * sc), false); text(tx, dy + 33 * sc, line, g_status_col, sc); }
    {   /* key help: a shorter wording when the panel is narrow */
        int hc = (L.x1 - L.x0 - 16 * sc) / (6 * sc);
        const char *help = g_proc_kind == 2 ? "Please wait ...   ESC QUIT" : "ENTER/A PLAY   R/Y REBUILD   ESC/B BACK";
        if ((int)strlen(help) > hc) help = g_proc_kind == 2 ? "Please wait  ESC QUIT" : "ENTER/A PLAY R/Y REBUILD ESC/B BACK";
        if ((int)strlen(help) > hc && g_proc_kind != 2) help = "ENTER PLAY R REBUILD ESC BACK";
        char line[80]; fit_text(line, sizeof line, help, hc, false);
        text(tx, L.y1 - 13 * sc, line, 0xFF8890A0, sc);
    }
    SDL_UpdateTexture(g_ui_tex, NULL, g_ui, w * 4);
}

/* ---------------- game art: covers and logos as scaled textures (cached), cross-fade ---------------- */
typedef struct { char path[MAX_PATH]; bool logo, failed; int bw, bh; SDL_Texture *tex; float dw, dh; Uint32 used; } ArtTex;
#define N_ART 24
static ArtTex g_art[N_ART]; static Uint32 g_art_clock;
static bool g_no_fade;                                   /* test shots: no fade */

/* the picture fitted into bw x bh output pixels (logos with the white outline); loaded on first use */
static ArtTex *art_tex(const char *path, bool logo, int bw, int bh, bool load)
{
    if (!path || !path[0] || bw < 8 || bh < 8) return NULL;
    ArtTex *lru = &g_art[0];
    for (int i = 0; i < N_ART; i++) {
        ArtTex *a = &g_art[i];
        if (a->path[0] && a->bw == bw && a->bh == bh && a->logo == logo && !strcmp(a->path, path)) { a->used = ++g_art_clock; return a->failed ? NULL : a; }
        if (a->used < lru->used) lru = a;
    }
    if (!load) return NULL;
    if (lru->tex) SDL_DestroyTexture(lru->tex);
    memset(lru, 0, sizeof *lru);
    snprintf(lru->path, sizeof lru->path, "%s", path); lru->logo = logo; lru->bw = bw; lru->bh = bh; lru->used = ++g_art_clock;
    int w = 0, h = 0; uint32_t *img = load_image(path, &w, &h);
    if (!img) img = art_load(path, &w, &h);
    if (img) {                                           /* trim fully transparent borders (logos with margins) */
        int x0 = w, y0 = h, x1 = -1, y1 = -1;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if ((img[(size_t)y * w + x] >> 24) > 8) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; y1 = y; }
        if (x1 >= 0 && (x0 > 0 || y0 > 0 || x1 < w - 1 || y1 < h - 1)) {
            int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
            uint32_t *c = (uint32_t *)malloc((size_t)cw * ch * 4);
            if (c) {
                for (int y = 0; y < ch; y++) memcpy(c + (size_t)y * cw, img + (size_t)(y + y0) * w + x0, (size_t)cw * 4);
                free(img); img = c; w = cw; h = ch;
            }
        }
    }
    if (img && logo) {
        int r = (w > h ? w : h) / 100, ow, oh; if (r < 2) r = 2;
        uint32_t *o = art_outline(img, w, h, r, 0xFFFFFFFFu, &ow, &oh);
        free(img); img = o; w = ow; h = oh;
    }
    if (!img || w < 1 || h < 1) { free(img); lru->failed = true; return NULL; }
    double sx = (double)bw / w, sy = (double)bh / h, sc = sx < sy ? sx : sy;
    lru->dw = (float)(w * sc); lru->dh = (float)(h * sc);
    int tw = w, th = h; uint32_t *px = img;
    if (sc < 1) {                                        /* shrink in software (area average): sharp */
        tw = (int)(w * sc + 0.5); th = (int)(h * sc + 0.5); if (tw < 1) tw = 1; if (th < 1) th = 1;
        px = art_shrink(img, w, h, tw, th);
    } else if (strstr(path, " (title).")) {              /* captured title screen: pixel art, keep it crisp */
        int k = (int)ceil(sc);                           /* whole-pixel enlargement, then area average */
        tw = (int)(w * sc + 0.5); th = (int)(h * sc + 0.5);
        uint32_t *up = (uint32_t *)malloc((size_t)w * k * h * k * 4);
        if (up) {
            for (int y = 0; y < h * k; y++)
                for (int x = 0; x < w * k; x++) up[(size_t)y * w * k + x] = img[(size_t)(y / k) * w + x / k];
            if (w * k == tw && h * k == th) px = up;
            else { px = art_shrink(up, w * k, h * k, tw, th); free(up); }
        }
        if (!px) { px = img; tw = w; th = h; }
    }
    if (px) {
        lru->tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, tw, th);
        if (lru->tex) { SDL_UpdateTexture(lru->tex, NULL, px, tw * 4); SDL_SetTextureBlendMode(lru->tex, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(lru->tex, SDL_ScaleModeLinear); }
    }
    if (px != img) free(px);
    free(img);
    if (!lru->tex) { lru->failed = true; return NULL; }
    return lru;
}

typedef struct { char cur[MAX_PATH], prev[MAX_PATH]; Uint32 t; } Fade;
static Fade g_fade_cover, g_fade_logo;
#define FADE_MS 180
static void fade_set(Fade *f, const char *path)
{
    if (!strcmp(f->cur, path)) return;
    snprintf(f->prev, sizeof f->prev, "%s", f->cur); snprintf(f->cur, sizeof f->cur, "%s", path);
    f->t = g_no_fade ? 0 : SDL_GetTicks();
}
static float fade_a(const Fade *f)
{
    if (g_no_fade || !f->t) return 1.f;
    Uint32 d = SDL_GetTicks() - f->t;
    return d >= FADE_MS ? 1.f : (float)d / FADE_MS;
}

/* the cover box: the area beside the panel, 86 % wide, 84 % of the window height */
static void cover_box(int w, int h, const Layout *L, int *bx, int *by, int *bw, int *bh)
{
    int ax0 = g_panel_left ? L->x1 : 0, ax1 = g_panel_left ? w : L->x0;
    *bw = (ax1 - ax0) * 86 / 100; *bh = h * 84 / 100;
    *bx = ax0 + ((ax1 - ax0) - *bw) / 2; *by = (h - *bh) / 2;
}
static void logo_box(const Layout *L, int *bx, int *by, int *bw, int *bh)
{
    *bx = L->x0 + 8 * L->sc; *bw = L->x1 - L->x0 - 16 * L->sc; *by = L->logo_y; *bh = L->logo_h;
}

static void draw_cover(const char *path, float a, int bx, int by, int bw, int bh, int sc)
{
    if (a <= 0.001f) return;
    ArtTex *t = art_tex(path, false, bw, bh, true);
    if (!t) return;
    SDL_FRect d = { bx + (bw - t->dw) / 2, by + (bh - t->dh) / 2, t->dw, t->dh };
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    for (int i = 12; i >= 1; i--) {                       /* soft shadow, a little lower */
        float e = (float)(i * sc);
        SDL_FRect r = { d.x - e, d.y - e + 3 * sc, d.w + 2 * e, d.h + 2 * e };
        SDL_SetRenderDrawColor(g_ren, 0, 0, 0, (Uint8)(14 * a));
        SDL_RenderFillRectF(g_ren, &r);
    }
    SDL_FRect o = { d.x - 1, d.y - 1, d.w + 2, d.h + 2 };
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, (Uint8)(170 * a));
    SDL_RenderFillRectF(g_ren, &o);
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_NONE);
    SDL_SetTextureAlphaMod(t->tex, (Uint8)(255 * a));
    SDL_RenderCopyF(g_ren, t->tex, NULL, &d);
}
static void draw_logo(const char *path, float a, int bx, int by, int bw, int bh)
{
    if (a <= 0.001f) return;
    ArtTex *t = art_tex(path, true, bw, bh, true);
    if (!t) return;
    SDL_FRect d = { bx + (bw - t->dw) / 2, by + (bh - t->dh) / 2, t->dw, t->dh };
    SDL_SetTextureAlphaMod(t->tex, (Uint8)(255 * a));
    SDL_RenderCopyF(g_ren, t->tex, NULL, &d);
}

/* idle: prepare one not yet cached picture per call (every game's cover and logo at the current size) */
static void art_warm(void)
{
    int w, h; SDL_GetRendererOutputSize(g_ren, &w, &h);
    Layout L = layout(w, h);
    int cx, cy, cw, ch, lx, ly, lw, lh;
    cover_box(w, h, &L, &cx, &cy, &cw, &ch); logo_box(&L, &lx, &ly, &lw, &lh);
    for (int i = 0; i < g_ngames; i++) {
        const Game *g = &g_games[i];
        if (g->cover[0] && !art_tex(g->cover, false, cw, ch, false)) { art_tex(g->cover, false, cw, ch, true); return; }
        if (g->logo[0] && L.logo_h && !art_tex(g->logo, true, lw, lh, false)) { art_tex(g->logo, true, lw, lh, true); return; }
    }
}

/* PRESS START, blinking, in the middle of the window */
static void draw_splash(void)
{
    int w, h; SDL_GetRendererOutputSize(g_ren, &w, &h);
    if (!g_ui_tex || w != g_ui_w || h != g_ui_h) draw_ui();              /* (re)creates the buffer */
    if (!g_ui) return;
    memset(g_ui, 0, (size_t)w * h * 4);
    Uint32 t = SDL_GetTicks() - g_splash_t;
    if (t % 1100 < 750) {
        const char *msg = "PRESS START";
        int sc = layout(w, h).sc * 3, tw = 6 * sc * (int)strlen(msg) - sc, x = (w - tw) / 2, y = h * g_press_y / 100 - 7 * sc / 2;
        if (y < sc) y = sc;
        if (y > h - 8 * sc) y = h - 8 * sc;
        for (int dy = -1; dy <= 1; dy++)                                   /* dark outline */
            for (int dx = -1; dx <= 1; dx++)
                if (dx || dy) hud_text_scaled(g_ui, w, h, x + dx * (sc / 2 + 1), y + dy * (sc / 2 + 1), msg, 0xE0000814u, sc);
        hud_text_scaled(g_ui, w, h, x, y, msg, 0xFFFFE040, sc);
    }
    {   /* version, bottom right */
        int sc = layout(w, h).sc;
        const char *v = "v" APP_VERSION;
        hud_text_scaled(g_ui, w, h, w - 6 * sc * (int)strlen(v) - 6 * sc, h - 12 * sc, v, 0xA0A0A0B0, sc);
    }
    SDL_UpdateTexture(g_ui_tex, NULL, g_ui, w * 4);
}
static void leave_splash(void) { g_splash = false; g_list_t = g_no_fade ? 0 : SDL_GetTicks(); }
static void enter_splash(void) { g_splash = true; g_splash_t = SDL_GetTicks(); }   /* back from the list */

static void render(void)
{
    make_background();
    if (g_splash) draw_splash(); else draw_ui();
    int w, h; SDL_GetRendererOutputSize(g_ren, &w, &h);
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);
    if (g_bg_tex) {
        SDL_Rect src = { 0, 0, g_bg_w, g_bg_h };
        if (!g_bg_generic) {                                       /* cover: fill the window, crop the rest */
            double sw = (double)w / g_bg_w, sh = (double)h / g_bg_h, s = sw > sh ? sw : sh;
            int cw = (int)(w / s), ch = (int)(h / s);
            src.x = (g_bg_w - cw) / 2; src.y = (g_bg_h - ch) / 2; src.w = cw; src.h = ch;
        }
        SDL_RenderCopy(g_ren, g_bg_tex, &src, NULL);
    }
    if (g_splash) {
        if (g_app_logo[0] && !g_bg_is_app) {                       /* the app's logo above PRESS START */
            int area = w * 58 / 100;                               /* generated background: its composition */
            int bw = g_bg_generic ? area * 90 / 100 : w * 62 / 100, bh = g_bg_generic ? h * 22 / 100 : h * 30 / 100;
            ArtTex *t = art_tex(g_app_logo, false, bw, bh, true);
            if (t) {
                float y = h * g_press_y / 100.f - h * 0.06f - t->dh, x = (w - t->dw) / 2;
                if (g_bg_generic) { y = h * 0.66f; x = (g_panel_left ? w - area / 2 : area / 2) - t->dw / 2; }
                if (y < h * 0.03f) y = h * 0.03f;
                SDL_FRect d = { x, y, t->dw, t->dh };
                SDL_SetTextureAlphaMod(t->tex, 255);
                SDL_RenderCopyF(g_ren, t->tex, NULL, &d);
            }
        }
        if (g_ui_tex) { SDL_SetTextureAlphaMod(g_ui_tex, 255); SDL_RenderCopy(g_ren, g_ui_tex, NULL, NULL); }
        SDL_RenderPresent(g_ren);
        return;
    }
    Layout L = layout(w, h);
    const Game *sel = (g_art_sel >= 0 && g_art_sel < g_ngames) ? &g_games[g_art_sel] : NULL;
    fade_set(&g_fade_cover, sel ? sel->cover : "");
    fade_set(&g_fade_logo, sel ? sel->logo : "");
    {   /* cover: background dimmed behind it, cross-fade from the previous one */
        float a = fade_a(&g_fade_cover);
        float dim = (g_fade_cover.cur[0] ? a : 0.f) + (g_fade_cover.prev[0] ? 1.f - a : 0.f);
        if (dim > 0.001f) {
            SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(g_ren, 2, 6, 16, (Uint8)(120 * dim));
            SDL_RenderFillRect(g_ren, NULL);
            SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_NONE);
        }
        int bx, by, bw, bh; cover_box(w, h, &L, &bx, &by, &bw, &bh);
        draw_cover(g_fade_cover.prev, 1.f - a, bx, by, bw, bh, L.sc);
        draw_cover(g_fade_cover.cur, a, bx, by, bw, bh, L.sc);
    }
    if (g_ui_tex) {                                               /* the list fades in after the splash */
        Uint32 d = g_list_t ? SDL_GetTicks() - g_list_t : 1000;
        SDL_SetTextureAlphaMod(g_ui_tex, d >= 250 ? 255 : (Uint8)(255 * d / 250));
        SDL_RenderCopy(g_ren, g_ui_tex, NULL, NULL);
    }
    if (L.logo_h) {
        float a = fade_a(&g_fade_logo);
        int bx, by, bw, bh; logo_box(&L, &bx, &by, &bw, &bh);
        draw_logo(g_fade_logo.prev, 1.f - a, bx, by, bw, bh);
        draw_logo(g_fade_logo.cur, a, bx, by, bw, bh);
    }
    SDL_RenderPresent(g_ren);
}

/* ---------------- actions ---------------- */
static void set_error(const char *msg) { snprintf(g_status, sizeof g_status, "%s", msg); g_status_col = 0xFFFF6060; }

/* base ROM + patch -> ROMs/Patched/<rom_name>, checked against the build's CRC32; then rescans */
static bool make_patched_rom(Game *g)
{
    const PatchFile *pf = patch_by_crc(g->patch_crc);
    if (!pf) { set_error("The patch file is missing."); return false; }
    size_t sn, pn, on = 0; const char *err = "applying the patch failed";
    uint8_t *src = read_file(g->base_rom, 8u << 20, &sn), *p = patch_load(pf, &pn), *out = NULL;
    if (src && p) out = has_ext(pf->entry[0] ? pf->entry : pf->path, ".bps") ? apply_bps(src, sn, p, pn, &on, &err) : apply_ips(src, sn, p, pn, &on);
    else err = !src ? "cannot read the base ROM" : "cannot read the patch file";
    free(src); free(p);
    if (out && crc32_buf(0, out, on) != g->crc) { free(out); out = NULL; err = "the patched ROM is not the one this build is made for"; }
    if (!out) { char m[200]; snprintf(m, sizeof m, "Patch failed: %s.", err); set_error(m); return false; }
    char dir[MAX_PATH], path[MAX_PATH], tmp[MAX_PATH + 4];
    snprintf(dir, sizeof dir, "%s\\ROMs\\%s", g_root, g->platform[0] ? g->platform : "sms"); CreateDirectoryA(dir, NULL);
    { size_t dl = strlen(dir); snprintf(dir + dl, sizeof dir - dl, "\\Patched"); } CreateDirectoryA(dir, NULL);
    snprintf(path, sizeof path, "%s\\%s", dir, g->rom_name); snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    bool ok = f && fwrite(out, 1, on, f) == on;
    if (f) ok = fclose(f) == 0 && ok;
    free(out);
    if (!ok || !MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { DeleteFileA(tmp); set_error("Cannot write the patched ROM (ROMs\\<platform>\\Patched)."); return false; }
    char slug[64]; snprintf(slug, sizeof slug, "%s", g->slug);
    scan_games();
    for (int i = 0; i < g_ngames; i++) if (!strcmp(g_games[i].slug, slug)) g_sel = i;
    return g_sel >= 0 && g_sel < g_ngames && g_games[g_sel].state != ST_PATCH && g_games[g_sel].state != ST_NEEDS_PATCH;
}

static void play_selected(bool force_build)
{
    if (g_proc_kind || g_sel < 0 || g_sel >= g_ngames) return;
    Game *g = &g_games[g_sel];
    if (g->state == ST_NO_PROFILE) { set_error("Cannot play this one yet."); return; }
    if (g->state == ST_NEEDS_PATCH) { char m[200]; snprintf(m, sizeof m, "Put %s into ROMs\\<platform>\\Patches first.", g->patch_name[0] ? g->patch_name : "the patch"); set_error(m); return; }
    if (g->state == ST_PATCH) {
        if (!make_patched_rom(g)) return;
        g = &g_games[g_sel];
        if (force_build) return;
    }
    if ((force_build || g->state == ST_NOT_BUILT) && !has_build_script(g) && !has_release_kit(g)) {
        set_error("Cannot build: no build.ps1 and no Engine folder of a release here.");
        return;
    }
    if (force_build || g->state == ST_NOT_BUILT) { start_build(g, !force_build); return; }
    g_status[0] = 0;
    start_game(g);                                 /* the launcher hides once the game window has the focus */
}

static void poll_process(void)
{
    if (!g_proc_kind) return;
    if (g_proc_kind == 2) {
        char tail[200], st[200], sf[MAX_PATH];
        snprintf(sf, sizeof sf, "%s\\build_step.txt", g_build_dir);   /* release build: its current step */
        FILE *f = fopen(sf, "r");
        if (f) {
            char line[200] = ""; int a = 0, b = 0, used = 0;
            if (fgets(line, sizeof line, f) && sscanf(line, "%d/%d %n", &a, &b, &used) == 2 && used > 0) {
                trim(line + used);
                snprintf(tail, sizeof tail, "[%d/%d] %s", a, b, line + used);
                build_stage(tail, st, sizeof st);
                snprintf(g_status, sizeof g_status, "%s", st); g_status_col = 0xFFFFE040;
            }
            fclose(f);
        }
        else {                                           /* developer build: the log's last line */
            build_log_tail(tail, sizeof tail);
            if (tail[0]) { build_stage(tail, st, sizeof st); if (st[0]) { snprintf(g_status, sizeof g_status, "%s", st); g_status_col = 0xFFFFE040; } }
        }
        if (g_build_step) {                           /* release build: 1 recompile 0-5 %, 2 compile 5-95 %, 3 link, 4 done */
            float t = (float)(SDL_GetTicks() - g_step_t0);
            if (g_build_step == 1) g_build_frac = 0.05f * (t / (t + 3000.0f));
            else if (g_build_step == 2) { float f = g_compile_est_ms > 0 ? t / (float)g_compile_est_ms : 0; g_build_frac = 0.05f + 0.90f * (f < 0.97f ? f : 0.97f); }
            else if (g_build_step == 3) g_build_frac = 0.95f + 0.04f * (t / (t + 2000.0f));
            else g_build_frac = 1.0f;
        }
    }
    if (WaitForSingleObject(g_proc.hProcess, 0) != WAIT_OBJECT_0) return;
    DWORD code = 1; GetExitCodeProcess(g_proc.hProcess, &code);
    CloseHandle(g_proc.hProcess); CloseHandle(g_proc.hThread);
    int kind = g_proc_kind; g_proc_kind = 0;
    char slug[64]; snprintf(slug, sizeof slug, "%s", g_proc_slug);
    scan_games();
    for (int i = 0; i < g_ngames; i++) if (!strcmp(g_games[i].slug, slug)) g_sel = i;
    if (kind == 1) {
        SDL_ShowWindow(g_win); SDL_RaiseWindow(g_win); take_focus(sdl_hwnd(g_win)); g_wait_win = false;
        fprintf(stderr, "[launcher] game ended; foreground = launcher: %s\n", GetForegroundWindow() == sdl_hwnd(g_win) ? "yes" : "no");
        return;
    }
    Game *g = (g_sel >= 0 && g_sel < g_ngames) ? &g_games[g_sel] : NULL;
    if (code != 0 || !g || g->state != ST_READY) { snprintf(g_status, sizeof g_status, "Build failed - see build_log.txt in the game folder (%s)", slug); g_status_col = 0xFFFF6060; return; }
    snprintf(g_status, sizeof g_status, "Build finished."); g_status_col = 0xFF7CE07C;
    if (g_play_after_build) { g_status[0] = 0; start_game(g); }
}

/* ---------------- title screens: captured from the ROMs in the background ----------------
   A game without the user's cover and without its title file gets one: its exe (not built yet: the
   generic Engine\<platform>\SmsInterp.exe of a release, if there) runs headless with --title-shot (finds the title screen in the first 60 s; game.toml `title_frame = N` takes that
   frame instead; never reads or writes saves) and writes Images\<platform>\<name> (title).png. One at a
   time, never while a game or a build runs; ~2 s each. A failure is not retried in this session. */
static PROCESS_INFORMATION g_ts_proc; static bool g_ts_running; static char g_ts_slug[64];
#define MAX_TS_FAIL 64
static char g_ts_failed[MAX_TS_FAIL][64]; static int g_ts_nfailed;

static bool ts_failed(const char *slug)
{
    for (int i = 0; i < g_ts_nfailed; i++) if (!strcmp(g_ts_failed[i], slug)) return true;
    return false;
}

static void title_capture_tick(void)
{
    if (g_ts_running) {
        if (WaitForSingleObject(g_ts_proc.hProcess, 0) != WAIT_OBJECT_0) return;
        DWORD code = 1; GetExitCodeProcess(g_ts_proc.hProcess, &code);
        CloseHandle(g_ts_proc.hProcess); CloseHandle(g_ts_proc.hThread);
        g_ts_running = false;
        for (int i = 0; i < g_ngames; i++) {
            Game *g = &g_games[i];
            if (strcmp(g->slug, g_ts_slug)) continue;
            if (code == 0 && is_file(g->title)) { if (!g->cover[0]) snprintf(g->cover, sizeof g->cover, "%s", g->title); }
            else if (g_ts_nfailed < MAX_TS_FAIL) snprintf(g_ts_failed[g_ts_nfailed++], sizeof g_ts_failed[0], "%s", g->slug);
        }
        return;
    }
    if (getenv("LAUNCHER_NO_TITLES")) { g_ts_idle = true; return; }
    if (g_proc_kind || g_ts_idle) return;
    for (int i = 0; i < g_ngames; i++) {
        const Game *g = &g_games[i];
        if ((g->state != ST_READY && g->state != ST_NOT_BUILT) || !g->rom[0] || g->cover[0] || !g->title[0] || is_file(g->title) || ts_failed(g->slug)) continue;
        char shooter[MAX_PATH];
        if (g->exe[0]) snprintf(shooter, sizeof shooter, "%s", g->exe);
        else {
            snprintf(shooter, sizeof shooter, "%s\\Engine\\%s\\SmsInterp.exe", g_root, g->platform[0] ? g->platform : "sms");
            if (!is_file(shooter)) continue;
        }
        char dir[MAX_PATH]; snprintf(dir, sizeof dir, "%s\\Images", g_root); CreateDirectoryA(dir, NULL);
        { size_t dl = strlen(dir); snprintf(dir + dl, sizeof dir - dl, "\\%s", g->platform[0] ? g->platform : "sms"); } CreateDirectoryA(dir, NULL);
        char cmd[4 * MAX_PATH];
        int n = snprintf(cmd, sizeof cmd, "\"%s\" \"%s\" --title-shot \"%s\"", shooter, g->rom, g->title);
        if (g->title_frame > 0) snprintf(cmd + n, sizeof cmd - (size_t)n, " --title-frame %d", g->title_frame);
        STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
        if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, NULL, g->dir, &si, &g_ts_proc)) {
            if (g_ts_nfailed < MAX_TS_FAIL) snprintf(g_ts_failed[g_ts_nfailed++], sizeof g_ts_failed[0], "%s", g->slug);
            return;
        }
        g_ts_running = true; snprintf(g_ts_slug, sizeof g_ts_slug, "%s", g->slug);
        return;
    }
    g_ts_idle = true;                                  /* nothing left; scan_games() looks again */
}

static void move_sel(int d)
{
    if (!g_ngames) return;
    g_sel += d;
    if (g_sel < 0) g_sel = 0;
    if (g_sel >= g_ngames) g_sel = g_ngames - 1;
}
/* held direction: +-1 = one row, +-2 = one page (left / right, LB / RB) */
static int nav_step(int dir) { return dir == 2 ? g_rows : dir == -2 ? -g_rows : dir; }

/* test: the window's picture as a PPM file */
static void write_shot(const char *path)
{
    int w, h; SDL_GetRendererOutputSize(g_ren, &w, &h);
    uint32_t *px = (uint32_t *)malloc((size_t)w * h * 4);
    if (px && SDL_RenderReadPixels(g_ren, NULL, SDL_PIXELFORMAT_ARGB8888, px, w * 4) == 0) {
        FILE *f = fopen(path, "wb");
        if (f) { fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (int i = 0; i < w * h; i++) { uint8_t c[3] = { (uint8_t)(px[i] >> 16), (uint8_t)(px[i] >> 8), (uint8_t)px[i] }; fwrite(c, 1, 3, f); }
            fclose(f); }
    }
    free(px);
}

int main(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "--build")) {          /* release: build one game (start_build runs this) */
        find_root();
        return release_build(g_root, argv[2], argv[3]);
    }
    find_root();
    load_ini();
    find_background();
    if (getenv("LAUNCHER_NO_IMAGE")) { g_bg_path[0] = 0; g_bg_is_app = false; }   /* test: the generic background */
    scan_games();
    if (getenv("LAUNCHER_CAPTURE_ONLY")) {                  /* test / batch: every missing title, no window */
        do { title_capture_tick(); Sleep(20); } while (g_ts_running || !g_ts_idle);
        for (int i = 0; i < g_ngames; i++)
            if (g_games[i].state != ST_NO_PROFILE)
                fprintf(stderr, "[launcher] %-12s %s\n", g_games[i].cover[0] ? (!strcmp(g_games[i].cover, g_games[i].title) ? "title" : "cover")
                        : ts_failed(g_games[i].slug) ? "FAILED" : "-", g_games[i].name);
        return 0;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    int win_w = 1280, win_h = 720;                              /* test: LAUNCHER_SIZE=<w>x<h> */
    if (getenv("LAUNCHER_SIZE") && sscanf(getenv("LAUNCHER_SIZE"), "%dx%d", &win_w, &win_h) != 2) { win_w = 1280; win_h = 720; }
    g_win = SDL_CreateWindow(APP_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (g_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!g_win) return 1;
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, 0);
    if (!g_ren) return 1;
    {   /* test: LAUNCHER_SHOT=<file.ppm> renders one frame to a file and quits */
        const char *shot = getenv("LAUNCHER_SHOT");
        if (shot) {
            const char *sel = getenv("LAUNCHER_SEL"); if (sel) g_sel = atoi(sel);
            g_no_fade = true;
            g_splash = getenv("LAUNCHER_SPLASH") != NULL;          /* test: shoot the PRESS START screen */
            g_art_sel = g_sel;
            render(); render();
            write_shot(shot);
            SDL_Quit(); return 0;
        }
    }
    bool quit = false; Uint32 last_click = 0; int last_click_row = -1;
    /* test: LAUNCHER_AUTO=play:<slug> | build:<slug> starts that action (LAUNCHER_AUTO_MS=<ms> after the
       start, e.g. once a test script has brought the launcher to the front); LAUNCHER_EXIT_AFTER=1 quits when it ends */
    const char *auto_act = getenv("LAUNCHER_AUTO");
    Uint32 auto_at = getenv("LAUNCHER_AUTO_MS") ? (Uint32)atol(getenv("LAUNCHER_AUTO_MS")) : 0;
    if (auto_act && strncmp(auto_act, "play:", 5) && strncmp(auto_act, "build:", 6)) auto_act = NULL;
    if (auto_act) g_splash = false;
    bool exit_after = getenv("LAUNCHER_EXIT_AFTER") != NULL;
    /* test: LAUNCHER_SHOT_MS=<ms>[,<ms>...] + LAUNCHER_SHOT_OUT=<file with %d> = window pictures while it runs */
    const char *shot_ms = getenv("LAUNCHER_SHOT_MS"), *shot_out = getenv("LAUNCHER_SHOT_OUT");
    Uint32 nav_t = 0; int nav_dir = 0; Uint32 nav_next = 0;
    g_splash_t = SDL_GetTicks();
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            else if (e.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(e.cdevice.which);
            else if (g_splash) {                                   /* PRESS START: only continue / quit / fullscreen */
                if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                    SDL_Keycode k = e.key.keysym.sym;
                    if (k == SDLK_ESCAPE) quit = true;
                    else if (k == SDLK_F11 || (k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                        bool fs = (SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                        SDL_SetWindowFullscreen(g_win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                    }
                    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) leave_splash();
                }
                else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                    Uint8 b = e.cbutton.button;
                    if (b == SDL_CONTROLLER_BUTTON_START) leave_splash();
                }
                else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) leave_splash();
            }
            else if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE && !e.key.repeat) { if (g_proc_kind == 2) quit = true; else enter_splash(); }   /* building: ESC QUIT */
                else if (k == SDLK_F11 || (k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                    bool fs = (SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                    SDL_SetWindowFullscreen(g_win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
                else if (k == SDLK_UP) move_sel(-1);
                else if (k == SDLK_DOWN) move_sel(1);
                else if (k == SDLK_PAGEUP || k == SDLK_LEFT) move_sel(-g_rows);
                else if (k == SDLK_PAGEDOWN || k == SDLK_RIGHT) move_sel(g_rows);
                else if (k == SDLK_HOME) g_sel = 0;
                else if (k == SDLK_END) g_sel = g_ngames - 1;
                else if (!e.key.repeat && (k == SDLK_RETURN || k == SDLK_KP_ENTER)) play_selected(false);
                else if (!e.key.repeat && k == SDLK_r) play_selected(true);
            }
            else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                Uint8 b = e.cbutton.button;
                if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) { move_sel(-1); nav_dir = -1; nav_t = SDL_GetTicks(); nav_next = nav_t + 400; }
                else if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) { move_sel(1); nav_dir = 1; nav_t = SDL_GetTicks(); nav_next = nav_t + 400; }
                else if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT || b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) { move_sel(-g_rows); nav_dir = -2; nav_t = SDL_GetTicks(); nav_next = nav_t + 400; }
                else if (b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT || b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) { move_sel(g_rows); nav_dir = 2; nav_t = SDL_GetTicks(); nav_next = nav_t + 400; }
                else if (b == SDL_CONTROLLER_BUTTON_A) play_selected(false);
                else if ((b == SDL_CONTROLLER_BUTTON_B || b == SDL_CONTROLLER_BUTTON_BACK) && !g_proc_kind) enter_splash();
                else if (b == SDL_CONTROLLER_BUTTON_Y) play_selected(true);
            }
            else if (e.type == SDL_CONTROLLERBUTTONUP) {
                Uint8 b = e.cbutton.button;
                if (b == SDL_CONTROLLER_BUTTON_DPAD_UP || b == SDL_CONTROLLER_BUTTON_DPAD_DOWN || b == SDL_CONTROLLER_BUTTON_DPAD_LEFT
                    || b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT || b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER || b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) nav_dir = 0;
            }
            else if (e.type == SDL_CONTROLLERAXISMOTION && (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY || e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)) {
                int k2 = e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ? 2 : 1;      /* 1 = rows, 2 = pages */
                int d = e.caxis.value < -16000 ? -k2 : e.caxis.value > 16000 ? k2 : 0;
                if (!d && nav_dir && (nav_dir == 2 || nav_dir == -2) != (k2 == 2)) continue;   /* the other axis went back to 0 */
                if (d != nav_dir) { nav_dir = d; if (d) { move_sel(nav_step(d)); nav_t = SDL_GetTicks(); nav_next = nav_t + 400; } }
            }
            else if (e.type == SDL_MOUSEWHEEL) move_sel(-e.wheel.y);
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int w, h; SDL_GetRendererOutputSize(g_ren, &w, &h);
                int ww, wh; SDL_GetWindowSize(g_win, &ww, &wh);
                int mx = e.button.x * w / (ww ? ww : 1), my = e.button.y * h / (wh ? wh : 1);
                Layout L = layout(w, h);
                if (mx >= L.x0 && mx < L.x1 && my >= L.row_y - 2 * L.sc && my < L.row_y + L.rows * L.row_h) {
                    int row = g_top + (my - L.row_y + 2 * L.sc) / L.row_h;
                    if (row >= 0 && row < g_ngames) {
                        Uint32 now = SDL_GetTicks();
                        if (row == last_click_row && now - last_click < 400) play_selected(false);
                        g_sel = row; last_click = now; last_click_row = row;
                    }
                }
            }
        }
        if (nav_dir && SDL_GetTicks() >= nav_next) {               /* held D-pad / stick: repeat, faster */
            move_sel(nav_step(nav_dir));
            Uint32 held = SDL_GetTicks() - nav_t;
            nav_next = SDL_GetTicks() + (held > 1500 ? 40 : held > 800 ? 80 : 120);
        }
        {   /* selection changed: old message away; the cover follows once the selection rests */
            static int last_sel = -1; static Uint32 sel_t;
            if (g_sel != last_sel) {
                if (last_sel >= 0 && !g_proc_kind) g_status[0] = 0;
                last_sel = g_sel; sel_t = SDL_GetTicks();
            }
            const Uint8 *ks = SDL_GetKeyboardState(NULL);
            bool held = nav_dir || ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_DOWN] || ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_RIGHT]
                        || ks[SDL_SCANCODE_PAGEUP] || ks[SDL_SCANCODE_PAGEDOWN];
            if (g_art_sel < 0 || g_art_sel >= g_ngames) g_art_sel = g_sel;                  /* first list / after a rescan */
            else if (g_art_sel != g_sel && !held && SDL_GetTicks() - sel_t >= 120) g_art_sel = g_sel;
        }
        if (auto_act && SDL_GetTicks() >= auto_at) {              /* test: LAUNCHER_AUTO */
            const char *slug = strchr(auto_act, ':') + 1;
            for (int i = 0; i < g_ngames; i++) if (!strcmp(g_games[i].slug, slug)) g_sel = i;
            play_selected(auto_act[0] == 'b');
            auto_act = NULL;
        }
        int was = g_proc_kind;
        poll_process();
        title_capture_tick();
        if (exit_after && was && !g_proc_kind) { fprintf(stderr, "[launcher] %s\n", g_status[0] ? g_status : "game ended"); quit = true; }
        if (g_proc_kind == 1 && g_wait_win) {                     /* game started: focus its window, then hide */
            HWND gw = process_window(g_proc.dwProcessId);
            if (gw || SDL_GetTicks() >= g_wait_win_until) {
                take_focus(gw);
                fprintf(stderr, "[launcher] game window %s; foreground = game: %s\n", gw ? "found" : "NOT found (timeout)",
                        gw && GetForegroundWindow() == gw ? "yes" : "no");
                SDL_HideWindow(g_win); g_wait_win = false; g_status[0] = 0;
            } else { render(); SDL_Delay(15); continue; }
        }
        if (g_proc_kind == 1) {                                     /* game running: launcher hidden */
            WaitForSingleObject(g_proc.hProcess, 100);             /* wakes at once when the game ends (focus back sooner) */
            continue;
        }
        render();
        if (shot_ms && shot_out && *shot_ms && SDL_GetTicks() >= (Uint32)atol(shot_ms)) {
            char fn[MAX_PATH]; snprintf(fn, sizeof fn, shot_out, atoi(shot_ms));
            write_shot(fn);
            shot_ms = strchr(shot_ms, ','); if (shot_ms) shot_ms++;
        }
        art_warm();
        SDL_Delay(g_proc_kind ? 30 : 15);
    }
    if (g_proc_kind == 2) TerminateProcess(g_proc.hProcess, 1);
    if (g_ts_running) TerminateProcess(g_ts_proc.hProcess, 1);   /* it writes <file>.tmp and renames it at the end: never a half title file */
    SDL_Quit();
    return 0;
}
