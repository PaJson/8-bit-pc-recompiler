/*
 * gv_map.c - Golvellius: world map of the screens visited (HUD tab MAP).
 *
 * The overworld is 13 x 14 screens plus the start screen; RAM $C80D = screen index = row * 13 +
 * column (scroll up -13, down +13, left -1, right +1: $2198/$2234/$22CE/$235B). New Game starts at
 * 182 ($B6, $1309) = A15, the forest screen below A14 (the only screen of row 15); a password starts
 * at 180 ($B4, $202A) = L14. The screen loader ($1918) reads a 183-byte screen -> room table.
 * Overworld = $C802 6 (after a password continue, $2025) or 7 (New Game / after a cave, $1DD3).
 * Whenever the overworld has been still for 30 frames (no scroll: $C814 = 0) on a screen, the playfield of the presented frame (x 8..255, y 16..191: without the status bar and
 * the blank left column) is stored at a quarter size (62 x 44) in its cell; every later visit
 * refreshes it. The frame comes from the engine overlay callback (hud_set_overlay), so this only
 * works in the SDL window. The map is kept in <save dir>/<rom>.gvmap (written when the HUD closes
 * and at exit; not with SMS_NO_SAVE). The HUD MAP tab shows it (engine hi-res layer when available)
 * with the current screen framed, plus its name (column letter A-M + row 1-14).
 * Test env: GV_MAP_LOG=1 (captures).
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hud.h"
#include "sms_runtime.h"

extern const char *glue_save_path(void);

#define COLS 13
#define ROWS 15                    /* 14 rows + the start screen A15 (index 182) */
#define NSCR 183
#define CW   62                    /* cell: 248 x 176 playfield / 4 */
#define CH   44
#define MW   (COLS * CW)
#define MH   (ROWS * CH)

static uint32_t s_map[MW * MH];
static uint8_t  s_seen[NSCR];
static bool overworld(void) { uint8_t m = sms_read8(0xC802); return m == 6 || m == 7; }
static bool in_game(void) { return sms_read8(0xC802) >= 5; }
static bool cell_valid(int col, int row) { return row < ROWS - 1 || col == 0; }
static uint32_t s_gen = 1;
static int      s_still, s_last_idx = -1;
static bool     s_dirty, s_loaded;

static bool map_log(void) { static int on = -1; if (on < 0) on = getenv("GV_MAP_LOG") != NULL; return on; }

static void map_path(char *out, size_t n)
{
    const char *sp = glue_save_path();
    out[0] = 0;
    if (!sp || !sp[0]) return;
    snprintf(out, n, "%s", sp);
    size_t l = strlen(out);
    if (l > 4 && !strcmp(out + l - 4, ".sav")) out[l - 4] = 0;
    strncat(out, ".gvmap", n - strlen(out) - 1);
}
void gv_map_save(void)
{
    if (!s_dirty || getenv("SMS_NO_SAVE")) return;
    char p[1100]; map_path(p, sizeof p);
    if (!p[0]) return;
    FILE *f = fopen(p, "wb"); if (!f) return;
    fwrite("GVM2", 1, 4, f);
    fwrite(s_seen, 1, sizeof s_seen, f);
    for (int i = 0; i < NSCR; i++) {
        if (!s_seen[i]) continue;
        int cx = (i % COLS) * CW, cy = (i / COLS) * CH;
        for (int y = 0; y < CH; y++) fwrite(&s_map[(size_t)(cy + y) * MW + cx], 4, CW, f);
    }
    fclose(f);
    s_dirty = false;
}
static void map_load(void)
{
    if (s_loaded) return;
    s_loaded = true;
    for (int i = 0; i < MW * MH; i++)              /* black; transparent where row 15 has no screen */
        s_map[i] = cell_valid((i % MW) / CW, (i / MW) / CH) ? 0xFF000000u : 0;
    char p[1100]; map_path(p, sizeof p);
    FILE *f = p[0] ? fopen(p, "rb") : NULL;
    if (!f) return;
    char mg[4]; size_t ns = 0;
    if (fread(mg, 1, 4, f) == 4) ns = !memcmp(mg, "GVM2", 4) ? NSCR : !memcmp(mg, "GVM1", 4) ? 182 : 0;   /* GVM1: no A15 */
    if (ns && fread(s_seen, 1, ns, f) == ns) {
        for (int i = 0; i < (int)ns; i++) {
            if (!s_seen[i]) continue;
            int cx = (i % COLS) * CW, cy = (i / COLS) * CH;
            for (int y = 0; y < CH; y++)
                if (fread(&s_map[(size_t)(cy + y) * MW + cx], 4, CW, f) != CW) { s_seen[i] = 0; break; }
        }
    } else memset(s_seen, 0, sizeof s_seen);
    fclose(f);
    s_gen++;
}

/* engine overlay callback: sees every presented frame (draws nothing) */
static void capture(uint32_t *fb, int w, int h)
{
    if (w != 256 || h < 192) return;
    int idx = sms_read8(0xC80D);
    if (map_log()) {                                   /* every change of screen / mode / scroll */
        static int last = -1; int now = idx | sms_read8(0xC802) << 8 | sms_read8(0xC814) << 16;
        if (now != last) { fprintf(stderr, "[gv_map] screen %d mode %02X scroll %02X\n", idx, sms_read8(0xC802), sms_read8(0xC814)); last = now; }
    }
    bool still = overworld() && sms_read8(0xC814) == 0 && idx < NSCR;
    if (!still || idx != s_last_idx) { s_still = 0; s_last_idx = still ? idx : -1; return; }
    /* after 30 still frames, then every 60 while staying (a capture taken during a fade-in, e.g.
       right after a password continue, is replaced); a (nearly) black frame = still fading: skipped */
    ++s_still;
    if (s_still < 30 || (s_still - 30) % 60) return;
    static uint32_t cell[CW * CH];
    unsigned long long sum = 0;
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++) {
            unsigned r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 4; dy++)
                for (int dx = 0; dx < 4; dx++) {
                    uint32_t p = fb[(size_t)(16 + y * 4 + dy) * w + 8 + x * 4 + dx];
                    r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255;
                }
            cell[y * CW + x] = 0xFF000000u | ((r / 16) << 16) | ((g / 16) << 8) | (b / 16);
            sum += r + g + b;
        }
    if (sum / (16ull * 3 * CW * CH) < 12) return;      /* mean brightness < 12 of 255 */
    int cx = (idx % COLS) * CW, cy = (idx / COLS) * CH;
    bool changed = !s_seen[idx];
    for (int y = 0; y < CH; y++) {
        uint32_t *dst = &s_map[(size_t)(cy + y) * MW + cx];
        if (memcmp(dst, &cell[y * CW], CW * 4)) { memcpy(dst, &cell[y * CW], CW * 4); changed = true; }
    }
    if (!changed) return;
    s_seen[idx] = 1; s_dirty = true; s_gen++;
    if (map_log()) fprintf(stderr, "[gv_map] captured screen %c%d (index %d)\n", 'A' + idx % COLS, idx / COLS + 1, idx);
}

static void map_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1)
{
    const uint32_t CYAN = 0xFF60D0FF, GREY = 0xFFA0A0B0, YEL = 0xFFFFE040;
    {   static int dumped; const char *dp = getenv("GV_MAP_DUMP");     /* test: the map image as PPM */
        if (dp && !dumped) { dumped = 1; FILE *f = fopen(dp, "wb");
            if (f) { fprintf(f, "P6\n%d %d\n255\n", MW, MH);
                for (int i = 0; i < MW * MH; i++) { uint8_t c[3] = { (uint8_t)(s_map[i] >> 16), (uint8_t)(s_map[i] >> 8), (uint8_t)s_map[i] }; fwrite(c, 1, 3, f); }
                fclose(f); } } }
    int idx = in_game() ? sms_read8(0xC80D) : NSCR, seen = 0;
    for (int i = 0; i < NSCR; i++) seen += s_seen[i];
    char t[48];
    if (idx < NSCR) snprintf(t, sizeof t, "WORLD MAP   SCREEN %c%d", 'A' + idx % COLS, idx / COLS + 1);
    else snprintf(t, sizeof t, "WORLD MAP");
    hud_text(fb, w, h, x0 + 4, y0, t, CYAN);
    snprintf(t, sizeof t, "%d/%d", seen, NSCR);
    hud_text(fb, w, h, x1 - 4 - 6 * (int)strlen(t), y0, t, GREY);
    int by0 = y0 + 11, bw = x1 - x0 - 8, bh = y1 - by0 - 2;
    float s = (float)bw / MW < (float)bh / MH ? (float)bw / MW : (float)bh / MH;
    float dw = MW * s, dh = MH * s, dx = x0 + 4 + (bw - dw) / 2, dy = by0 + (bh - dh) / 2;
    float gh = (ROWS - 1) * CH * s, cw1 = CW * s;       /* height of the 13 x 14 grid, one cell width */
    hud_fill(fb, w, h, (int)dx - 1, (int)dy - 1, (int)(dx + dw) + 1, (int)(dy + gh) + 1, 0xFF000000u, false);
    hud_fill(fb, w, h, (int)dx - 1, (int)(dy + gh), (int)(dx + cw1) + 1, (int)(dy + dh) + 1, 0xFF000000u, false);
    if (hud_hires()) {
        hud_image_alpha(HUD_SPACE_GAME, dx, dy, dw, dh, s_map, MW, MH, s_gen);
        for (int c = 1; c < COLS; c++) hud_rect(HUD_SPACE_GAME, dx + c * CW * s, dy, 0.25f, c == 1 ? dh : gh, 0x40FFFFFFu);
        for (int r = 1; r < ROWS; r++) hud_rect(HUD_SPACE_GAME, dx, dy + r * CH * s, r < ROWS - 1 ? dw : cw1, 0.25f, 0x40FFFFFFu);
    } else {                                            /* software: nearest into the frame */
        for (int y = 0; y < (int)dh; y++)
            for (int x = 0; x < (int)dw; x++) {
                int X = (int)dx + x, Y = (int)dy + y;
                uint32_t p = s_map[(size_t)(int)(y / s) * MW + (int)(x / s)];
                if ((p >> 24) && X >= 0 && Y >= 0 && X < w && Y < h) fb[(size_t)Y * w + X] = p;
            }
    }
    static unsigned blink; blink++;
    if (idx < NSCR && (blink / 18) % 2 == 0) {                 /* current screen, blinking frame */
        float cx = dx + (idx % COLS) * CW * s, cy = dy + (idx / COLS) * CH * s, cw = CW * s, ch = CH * s, lw = 0.75f;
        if (hud_hires()) {
            hud_rect(HUD_SPACE_GAME, cx, cy, cw, lw, YEL); hud_rect(HUD_SPACE_GAME, cx, cy + ch - lw, cw, lw, YEL);
            hud_rect(HUD_SPACE_GAME, cx, cy, lw, ch, YEL); hud_rect(HUD_SPACE_GAME, cx + cw - lw, cy, lw, ch, YEL);
        } else {
            hud_fill(fb, w, h, (int)cx, (int)cy, (int)(cx + cw), (int)cy + 1, YEL, false);
            hud_fill(fb, w, h, (int)cx, (int)(cy + ch) - 1, (int)(cx + cw), (int)(cy + ch), YEL, false);
            hud_fill(fb, w, h, (int)cx, (int)cy, (int)cx + 1, (int)(cy + ch), YEL, false);
            hud_fill(fb, w, h, (int)(cx + cw) - 1, (int)cy, (int)(cx + cw), (int)(cy + ch), YEL, false);
        }
    }
}

void gv_map_init(void)
{
    static bool pages;
    map_load();
    hud_set_overlay(capture);
    if (!pages) { hud_add_page("MAP", map_draw, NULL); pages = true; atexit(gv_map_save); }
}
