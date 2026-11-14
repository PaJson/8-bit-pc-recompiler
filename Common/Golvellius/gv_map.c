/*
 * gv_map.c - Golvellius: world map of the screens visited (HUD tab MAP).
 *
 * The overworld is 13 x 14 screens; RAM $C80D = screen index = row * 13 + column (0..181; the
 * password start L14 = 180, a scroll up subtracts 13; New Game starts at 182, outside the grid).
 * Whenever the overworld ($C802 = 7) has been still for 30 frames (no scroll: $C814 = 0) on a grid
 * screen, the playfield of the presented frame (x 8..255, y 16..191: without the status bar and
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
#define ROWS 14
#define CW   62                    /* cell: 248 x 176 playfield / 4 */
#define CH   44
#define MW   (COLS * CW)
#define MH   (ROWS * CH)

static uint32_t s_map[MW * MH];
static uint8_t  s_seen[COLS * ROWS];
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
    fwrite("GVM1", 1, 4, f);
    fwrite(s_seen, 1, sizeof s_seen, f);
    for (int i = 0; i < COLS * ROWS; i++) {
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
    for (int i = 0; i < MW * MH; i++) s_map[i] = 0xFF000000u;
    char p[1100]; map_path(p, sizeof p);
    FILE *f = p[0] ? fopen(p, "rb") : NULL;
    if (!f) return;
    char mg[4];
    if (fread(mg, 1, 4, f) == 4 && !memcmp(mg, "GVM1", 4) && fread(s_seen, 1, sizeof s_seen, f) == sizeof s_seen) {
        for (int i = 0; i < COLS * ROWS; i++) {
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
    bool still = sms_read8(0xC802) == 7 && sms_read8(0xC814) == 0 && idx < COLS * ROWS;
    if (!still || idx != s_last_idx) { s_still = 0; s_last_idx = still ? idx : -1; return; }
    if (++s_still != 30) return;                       /* once per stay, after 30 still frames */
    int cx = (idx % COLS) * CW, cy = (idx / COLS) * CH;
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++) {
            unsigned r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 4; dy++)
                for (int dx = 0; dx < 4; dx++) {
                    uint32_t p = fb[(size_t)(16 + y * 4 + dy) * w + 8 + x * 4 + dx];
                    r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255;
                }
            s_map[(size_t)(cy + y) * MW + cx + x] = 0xFF000000u | ((r / 16) << 16) | ((g / 16) << 8) | (b / 16);
        }
    s_seen[idx] = 1; s_dirty = true; s_gen++;
    if (map_log()) fprintf(stderr, "[gv_map] captured screen %c%d (index %d)\n", 'A' + idx % COLS, idx / COLS + 1, idx);
}

static void map_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1)
{
    const uint32_t CYAN = 0xFF60D0FF, GREY = 0xFFA0A0B0, YEL = 0xFFFFE040;
    int idx = sms_read8(0xC80D), seen = 0;
    for (int i = 0; i < COLS * ROWS; i++) seen += s_seen[i];
    char t[48];
    if (idx < COLS * ROWS) snprintf(t, sizeof t, "WORLD MAP   SCREEN %c%d", 'A' + idx % COLS, idx / COLS + 1);
    else snprintf(t, sizeof t, "WORLD MAP");
    hud_text(fb, w, h, x0 + 4, y0, t, CYAN);
    snprintf(t, sizeof t, "%d/%d", seen, COLS * ROWS);
    hud_text(fb, w, h, x1 - 4 - 6 * (int)strlen(t), y0, t, GREY);
    int by0 = y0 + 11, bw = x1 - x0 - 8, bh = y1 - by0 - 2;
    float s = (float)bw / MW < (float)bh / MH ? (float)bw / MW : (float)bh / MH;
    float dw = MW * s, dh = MH * s, dx = x0 + 4 + (bw - dw) / 2, dy = by0 + (bh - dh) / 2;
    hud_fill(fb, w, h, (int)dx - 1, (int)dy - 1, (int)(dx + dw) + 1, (int)(dy + dh) + 1, 0xFF000000u, false);
    if (hud_hires()) {
        hud_image(HUD_SPACE_GAME, dx, dy, dw, dh, s_map, MW, MH, 0, 0, MW, MH, false, s_gen);
        for (int c = 1; c < COLS; c++) hud_rect(HUD_SPACE_GAME, dx + c * CW * s, dy, 0.25f, dh, 0x40FFFFFFu);
        for (int r = 1; r < ROWS; r++) hud_rect(HUD_SPACE_GAME, dx, dy + r * CH * s, dw, 0.25f, 0x40FFFFFFu);
    } else {                                            /* software: nearest into the frame */
        for (int y = 0; y < (int)dh; y++)
            for (int x = 0; x < (int)dw; x++) {
                int X = (int)dx + x, Y = (int)dy + y;
                if (X >= 0 && Y >= 0 && X < w && Y < h) fb[(size_t)Y * w + X] = s_map[(size_t)(int)(y / s) * MW + (int)(x / s)];
            }
    }
    static unsigned blink; blink++;
    if (idx < COLS * ROWS && (blink / 18) % 2 == 0) {                 /* current screen, blinking frame */
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
