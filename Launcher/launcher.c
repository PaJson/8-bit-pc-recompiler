/*
 * launcher.c - game launcher for the recompiled Master System games of this project.
 *
 * Lists every game folder Games/<slug>/ (game.toml) whose ROM is present in ROMs/ (the ROM path in
 * game.toml), plus .sms files in ROMs/ (one subfolder level) that no game folder uses ("no build
 * profile"). A game is started like its play.ps1 does: Games/<slug>/build/<name>.exe "<rom>"
 * --window <scale>, working directory = the game folder, SMS_SAVE_DIR = Games/<slug>/saves. A game
 * that is not built yet is built first (its build.ps1, output in Games/<slug>/build_log.txt; this
 * recompiles the ROM and can take minutes). The launcher hides while a game runs.
 *
 * Background: the first .jpg/.png/.bmp in Images/ (the user's own picture, not part of the repo) or
 * `background = <path>` in launcher.ini; without one a generic background is drawn (no logos).
 * Text uses the engine HUD font (runner/hud.c). Images are decoded with GDI+ (part of Windows).
 *
 * Keys: Up/Down (PgUp/PgDn, Home/End) select, Enter/Space play (build first if needed), R rebuild,
 * F11 / Alt+Enter fullscreen, Esc quit. Gamepad: D-pad / stick, A or Start play, Y rebuild.
 * Mouse: wheel, click selects, double click plays.
 *
 * launcher.ini (project root, optional): background = <image path>, panel = right | left,
 * window_scale = 3 (the games' window scale), fullscreen = 0 | 1.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "hud.h"

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
    }
    fclose(f);
}

/* first image in Images/ when launcher.ini names none */
static void find_background(void)
{
    if (g_bg_path[0] && is_file(g_bg_path)) return;
    g_bg_path[0] = 0;
    static const char *const ext[] = { "*.jpg", "*.jpeg", "*.png", "*.bmp" };
    for (int i = 0; i < 4 && !g_bg_path[0]; i++) {
        char pat[MAX_PATH]; snprintf(pat, sizeof pat, "%s\\Images\\%s", g_root, ext[i]);
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) { snprintf(g_bg_path, sizeof g_bg_path, "%s\\Images\\%s", g_root, fd.cFileName); FindClose(h); }
    }
}

/* ---------------- games ---------------- */
enum { ST_READY, ST_NOT_BUILT, ST_NO_PROFILE };
typedef struct {
    char slug[64], name[160], dir[MAX_PATH], rom[MAX_PATH], exe[MAX_PATH], romfile[200];
    int  state;
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

static void find_exe(Game *g)
{
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

static void add_unprofiled_roms(const char *dir, int depth)
{
    char pat[MAX_PATH]; snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (depth > 0) add_unprofiled_roms(p, depth - 1); continue; }
        size_t l = strlen(fd.cFileName);
        if (l < 5 || _stricmp(fd.cFileName + l - 4, ".sms")) continue;
        char full[MAX_PATH]; GetFullPathNameA(p, MAX_PATH, full, NULL);
        bool used = false;
        for (int i = 0; i < g_ngames && !used; i++) used = !_stricmp(g_games[i].rom, full);
        if (used) continue;
        Game *g = add_game();
        snprintf(g->rom, sizeof g->rom, "%s", full);
        snprintf(g->romfile, sizeof g->romfile, "%s", fd.cFileName);
        snprintf(g->name, sizeof g->name, "%.*s", (int)(l - 4), fd.cFileName);
        g->state = ST_NO_PROFILE;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static void scan_games(void)
{
    g_ngames = 0;
    char pat[MAX_PATH]; snprintf(pat, sizeof pat, "%s\\Games\\*", g_root);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
            char dir[MAX_PATH], toml[MAX_PATH], rom[MAX_PATH], romfull[MAX_PATH];
            snprintf(dir, sizeof dir, "%s\\Games\\%s", g_root, fd.cFileName);
            snprintf(toml, sizeof toml, "%s\\game.toml", dir);
            if (!toml_str(toml, "rom", rom, sizeof rom)) continue;
            char rp[MAX_PATH]; snprintf(rp, sizeof rp, "%s\\%s", dir, rom);
            for (char *c = rp; *c; c++) if (*c == '/') *c = '\\';
            GetFullPathNameA(rp, MAX_PATH, romfull, NULL);
            if (!is_file(romfull)) continue;                 /* only games whose ROM is present */
            Game *g = add_game();
            snprintf(g->slug, sizeof g->slug, "%s", fd.cFileName);
            snprintf(g->dir, sizeof g->dir, "%s", dir);
            snprintf(g->rom, sizeof g->rom, "%s", romfull);
            const char *base = strrchr(romfull, '\\'); snprintf(g->romfile, sizeof g->romfile, "%s", base ? base + 1 : romfull);
            if (!toml_str(toml, "display_name", g->name, sizeof g->name)) {
                snprintf(g->name, sizeof g->name, "%s", g->romfile);
                char *dot = strrchr(g->name, '.'); if (dot) *dot = 0;
            }
            find_exe(g);
            g->state = g->exe[0] ? ST_READY : ST_NOT_BUILT;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    char roms[MAX_PATH]; snprintf(roms, sizeof roms, "%s\\ROMs", g_root);
    add_unprofiled_roms(roms, 1);
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

static bool start_game(const Game *g)
{
    char saves[MAX_PATH]; snprintf(saves, sizeof saves, "%s\\saves", g->dir);
    CreateDirectoryA(saves, NULL);
    char cmd[3 * MAX_PATH];
    const char *extra = getenv("LAUNCHER_GAME_EXTRA");                  /* test: extra runner arguments */
    snprintf(cmd, sizeof cmd, "\"%s\" \"%s\" --window %d %s", g->exe, g->rom, g_game_scale, extra ? extra : "");
    STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    SetEnvironmentVariableA("SMS_SAVE_DIR", saves);
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, g->dir, &si, &g_proc);
    SetEnvironmentVariableA("SMS_SAVE_DIR", NULL);
    if (!ok) { snprintf(g_status, sizeof g_status, "Could not start %s", g->exe); g_status_col = 0xFFFF6060; return false; }
    g_proc_kind = 1; snprintf(g_proc_slug, sizeof g_proc_slug, "%s", g->slug);
    return true;
}

static bool start_build(const Game *g, bool play_after)
{
    snprintf(g_build_log, sizeof g_build_log, "%s\\build_log.txt", g->dir);
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE log = CreateFileA(g_build_log, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log == INVALID_HANDLE_VALUE) { snprintf(g_status, sizeof g_status, "Cannot write %s", g_build_log); g_status_col = 0xFFFF6060; return false; }
    char cmd[2 * MAX_PATH];
    snprintf(cmd, sizeof cmd, "powershell -NoProfile -ExecutionPolicy Bypass -File \"%s\\build.ps1\"", g->dir);
    STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = log; si.hStdError = log; si.hStdInput = NULL;
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, g->dir, &si, &g_proc);
    CloseHandle(log);
    if (!ok) { snprintf(g_status, sizeof g_status, "Could not start the build"); g_status_col = 0xFFFF6060; return false; }
    g_proc_kind = 2; snprintf(g_proc_slug, sizeof g_proc_slug, "%s", g->slug);
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

/* ---------------- drawing ---------------- */
static SDL_Window *g_win; static SDL_Renderer *g_ren;
static SDL_Texture *g_bg_tex; static int g_bg_w, g_bg_h; static bool g_bg_generic;
static SDL_Texture *g_ui_tex; static uint32_t *g_ui; static int g_ui_w, g_ui_h;
static int g_sel, g_top;

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
    /* title */
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

typedef struct { int x0, y0, x1, y1, row_y, row_h, rows, sc; } Layout;
static Layout layout(int w, int h)
{
    Layout L; int sc = w >= 2400 ? 4 : w >= 1600 ? 3 : 2; if (h < 600) sc = 2; if (w < 900) sc = 1;
    int pw = w * 40 / 100;
    L.sc = sc;
    L.x0 = g_panel_left ? w * 2 / 100 : w - pw - w * 2 / 100; L.x1 = L.x0 + pw;
    L.y0 = h * 4 / 100; L.y1 = h - h * 4 / 100;
    L.row_h = 11 * sc;
    L.row_y = L.y0 + 30 * sc;
    int bottom = L.y1 - 64 * sc;
    L.rows = (bottom - L.row_y) / L.row_h; if (L.rows < 1) L.rows = 1;
    return L;
}

static uint32_t state_col(int s) { return s == ST_READY ? 0xFF7CE07C : s == ST_NOT_BUILT ? 0xFFFFE040 : 0xFF808898; }

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
        text(tx, L.row_y + 12 * sc, "Put your ROMs (.sms) in", 0xFFA0A0B0, sc);
        text(tx, L.row_y + 22 * sc, "the ROMs folder.", 0xFFA0A0B0, sc);
    }
    if (g_sel < g_top) g_top = g_sel;
    if (g_sel >= g_top + L.rows) g_top = g_sel - L.rows + 1;
    if (g_top < 0) g_top = 0;
    int name_chars = (L.x1 - L.x0 - 24 * sc) / (6 * sc);
    for (int i = 0; i < L.rows && g_top + i < g_ngames; i++) {
        const Game *g = &g_games[g_top + i];
        int y = L.row_y + i * L.row_h;
        bool sel = g_top + i == g_sel;
        if (sel) fill(L.x0 + 3 * sc, y - 2 * sc, L.x1 - 3 * sc, y + 9 * sc, 0xE0204A80);
        char nm[200]; snprintf(nm, sizeof nm, "%.*s", name_chars > 4 ? name_chars : 4, g->name);
        if ((int)strlen(g->name) > name_chars && name_chars > 6) { nm[name_chars - 2] = '.'; nm[name_chars - 1] = '.'; }
        uint32_t col = g->state == ST_NO_PROFILE ? 0xFF8890A0 : sel ? 0xFFFFE040 : 0xFFFFFFFF;
        if (g->state != ST_NO_PROFILE) fill(tx, y + sc, tx + 4 * sc, y + 5 * sc, state_col(g->state));   /* status marker */
        text(tx + 7 * sc, y, nm, col, sc);
    }
    if (g_top > 0) text(L.x1 - 14 * sc, L.row_y - 11 * sc, "^", 0xFF60D0FF, sc);
    if (g_top + L.rows < g_ngames) text(L.x1 - 14 * sc, L.row_y + L.rows * L.row_h - 2 * sc, "v", 0xFF60D0FF, sc);
    /* details */
    int dy = L.y1 - 60 * sc;
    fill(L.x0 + 3 * sc, dy - 4 * sc, L.x1 - 3 * sc, dy - 3 * sc, 0x8060D0FF);
    if (g_sel >= 0 && g_sel < g_ngames) {
        const Game *g = &g_games[g_sel];
        char line[260];
        int chars = (L.x1 - L.x0 - 16 * sc) / (6 * sc);
        int nl = (int)strlen(g->name);                            /* full name, up to 2 lines */
        int cut = nl;
        if (nl > chars) { cut = chars; while (cut > chars / 2 && g->name[cut] != ' ') cut--; if (cut <= chars / 2) cut = chars; }
        snprintf(line, sizeof line, "%.*s", cut, g->name); text(tx, dy, line, 0xFFFFFFFF, sc);
        if (nl > cut) { const char *r = g->name + cut; while (*r == ' ') r++; snprintf(line, sizeof line, "%.*s", chars, r); text(tx, dy + 10 * sc, line, 0xFFFFFFFF, sc); }
        if (g->state == ST_NO_PROFILE) snprintf(line, sizeof line, "%.*s", chars, "No build profile for this ROM yet.");
        else snprintf(line, sizeof line, "%.*s", chars, g->state == ST_READY ? "Ready to play." : "Not built yet: Play builds it first (minutes).");
        text(tx, dy + 22 * sc, line, state_col(g->state), sc);
    }
    if (g_status[0]) { char line[260]; snprintf(line, sizeof line, "%.*s", (L.x1 - L.x0 - 16 * sc) / (6 * sc), g_status); text(tx, dy + 33 * sc, line, g_status_col, sc); }
    text(tx, L.y1 - 13 * sc, g_proc_kind == 2 ? "Please wait ...   ESC QUIT" : "ENTER/A PLAY   R/Y REBUILD   ESC QUIT", 0xFF8890A0, sc);
    SDL_UpdateTexture(g_ui_tex, NULL, g_ui, w * 4);
}

static void render(void)
{
    make_background();
    draw_ui();
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
    if (g_ui_tex) SDL_RenderCopy(g_ren, g_ui_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
}

/* ---------------- actions ---------------- */
static void play_selected(bool force_build)
{
    if (g_proc_kind || g_sel < 0 || g_sel >= g_ngames) return;
    Game *g = &g_games[g_sel];
    if (g->state == ST_NO_PROFILE) { snprintf(g_status, sizeof g_status, "This ROM has no build profile (Games folder)."); g_status_col = 0xFFFF6060; return; }
    if (force_build || g->state == ST_NOT_BUILT) { start_build(g, !force_build); return; }
    g_status[0] = 0;
    if (start_game(g)) SDL_HideWindow(g_win);
}

static void poll_process(void)
{
    if (!g_proc_kind) return;
    if (g_proc_kind == 2) {
        char tail[200]; build_log_tail(tail, sizeof tail);
        if (tail[0]) { snprintf(g_status, sizeof g_status, "Building: %s", tail); g_status_col = 0xFFFFE040; }
    }
    if (WaitForSingleObject(g_proc.hProcess, 0) != WAIT_OBJECT_0) return;
    DWORD code = 1; GetExitCodeProcess(g_proc.hProcess, &code);
    CloseHandle(g_proc.hProcess); CloseHandle(g_proc.hThread);
    int kind = g_proc_kind; g_proc_kind = 0;
    char slug[64]; snprintf(slug, sizeof slug, "%s", g_proc_slug);
    scan_games();
    for (int i = 0; i < g_ngames; i++) if (!strcmp(g_games[i].slug, slug)) g_sel = i;
    if (kind == 1) { SDL_ShowWindow(g_win); SDL_RaiseWindow(g_win); return; }
    Game *g = (g_sel >= 0 && g_sel < g_ngames) ? &g_games[g_sel] : NULL;
    if (code != 0 || !g || g->state != ST_READY) { snprintf(g_status, sizeof g_status, "Build failed - see Games\\%s\\build_log.txt", slug); g_status_col = 0xFFFF6060; return; }
    snprintf(g_status, sizeof g_status, "Build finished."); g_status_col = 0xFF7CE07C;
    if (g_play_after_build) { g_status[0] = 0; if (start_game(g)) SDL_HideWindow(g_win); }
}

static void move_sel(int d)
{
    if (!g_ngames) return;
    g_sel += d;
    if (g_sel < 0) g_sel = 0;
    if (g_sel >= g_ngames) g_sel = g_ngames - 1;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    find_root();
    load_ini();
    if (getenv("LAUNCHER_NO_IMAGE")) g_bg_path[0] = 0;     /* test: the generic background */
    else find_background();
    scan_games();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    g_win = SDL_CreateWindow("Master System Recomp - Launcher", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (g_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!g_win) return 1;
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, 0);
    if (!g_ren) return 1;
    {   /* test: LAUNCHER_SHOT=<file.ppm> renders one frame to a file and quits */
        const char *shot = getenv("LAUNCHER_SHOT");
        if (shot) {
            const char *sel = getenv("LAUNCHER_SEL"); if (sel) g_sel = atoi(sel);
            render(); render();
            int w, h; SDL_GetRendererOutputSize(g_ren, &w, &h);
            uint32_t *px = (uint32_t *)malloc((size_t)w * h * 4);
            if (px && SDL_RenderReadPixels(g_ren, NULL, SDL_PIXELFORMAT_ARGB8888, px, w * 4) == 0) {
                FILE *f = fopen(shot, "wb");
                if (f) { fprintf(f, "P6\n%d %d\n255\n", w, h);
                    for (int i = 0; i < w * h; i++) { uint8_t c[3] = { (uint8_t)(px[i] >> 16), (uint8_t)(px[i] >> 8), (uint8_t)px[i] }; fwrite(c, 1, 3, f); }
                    fclose(f); }
            }
            free(px); SDL_Quit(); return 0;
        }
    }
    bool quit = false; Uint32 last_click = 0; int last_click_row = -1;
    {   /* test: LAUNCHER_AUTO=play:<slug> | build:<slug> starts that action; LAUNCHER_EXIT_AFTER=1 quits when it ends */
        const char *a = getenv("LAUNCHER_AUTO");
        if (a && (!strncmp(a, "play:", 5) || !strncmp(a, "build:", 6))) {
            const char *slug = strchr(a, ':') + 1;
            for (int i = 0; i < g_ngames; i++) if (!strcmp(g_games[i].slug, slug)) g_sel = i;
            play_selected(a[0] == 'b');
        }
    }
    bool exit_after = getenv("LAUNCHER_EXIT_AFTER") != NULL;
    Uint32 nav_t = 0; int nav_dir = 0; Uint32 nav_next = 0;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            else if (e.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(e.cdevice.which);
            else if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) quit = true;
                else if (k == SDLK_F11 || (k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                    bool fs = (SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                    SDL_SetWindowFullscreen(g_win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
                else if (k == SDLK_UP) move_sel(-1);
                else if (k == SDLK_DOWN) move_sel(1);
                else if (k == SDLK_PAGEUP) move_sel(-10);
                else if (k == SDLK_PAGEDOWN) move_sel(10);
                else if (k == SDLK_HOME) g_sel = 0;
                else if (k == SDLK_END) g_sel = g_ngames - 1;
                else if (!e.key.repeat && (k == SDLK_RETURN || k == SDLK_SPACE)) play_selected(false);
                else if (!e.key.repeat && k == SDLK_r) play_selected(true);
            }
            else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                Uint8 b = e.cbutton.button;
                if (b == SDL_CONTROLLER_BUTTON_DPAD_UP) { move_sel(-1); nav_dir = -1; nav_t = SDL_GetTicks(); nav_next = nav_t + 400; }
                else if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) { move_sel(1); nav_dir = 1; nav_t = SDL_GetTicks(); nav_next = nav_t + 400; }
                else if (b == SDL_CONTROLLER_BUTTON_A || b == SDL_CONTROLLER_BUTTON_START || b == SDL_CONTROLLER_BUTTON_B) play_selected(false);
                else if (b == SDL_CONTROLLER_BUTTON_Y) play_selected(true);
            }
            else if (e.type == SDL_CONTROLLERBUTTONUP) {
                Uint8 b = e.cbutton.button;
                if (b == SDL_CONTROLLER_BUTTON_DPAD_UP || b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) nav_dir = 0;
            }
            else if (e.type == SDL_CONTROLLERAXISMOTION && e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                int d = e.caxis.value < -16000 ? -1 : e.caxis.value > 16000 ? 1 : 0;
                if (d != nav_dir) { nav_dir = d; if (d) { move_sel(d); nav_t = SDL_GetTicks(); nav_next = nav_t + 400; } }
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
            move_sel(nav_dir);
            Uint32 held = SDL_GetTicks() - nav_t;
            nav_next = SDL_GetTicks() + (held > 1500 ? 40 : held > 800 ? 80 : 120);
        }
        int was = g_proc_kind;
        poll_process();
        if (exit_after && was && !g_proc_kind) { fprintf(stderr, "[launcher] %s\n", g_status[0] ? g_status : "game ended"); quit = true; }
        if (g_proc_kind == 1) { SDL_Delay(100); continue; }        /* game running: launcher hidden */
        render();
        SDL_Delay(g_proc_kind ? 30 : 15);
    }
    if (g_proc_kind == 2) TerminateProcess(g_proc.hProcess, 1);
    SDL_Quit();
    return 0;
}
