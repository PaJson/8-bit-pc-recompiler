/*
 * gv_panel.c - Golvellius side panel (widescreen window, option SIDE PANEL): the screens around the
 * hero, drawn from the ROM's screen data, plus gold and power.
 *
 * Surroundings = a 3 x 3 window of overworld screens (13 x 14 grid, screen = row * 13 + col, $C80D).
 * The window is centred on the hero's screen, but never leaves the grid: at an edge or a corner the
 * hero's screen stays at the edge / corner of the window (user's wish). A screen of the window is
 * only shown when the hero can walk there with his current equipment: a flood fill over the 45 x 36
 * cells of the window from the hero's cell, a cell is passable when its terrain attribute
 * ($D000[metatile], 0 openings, 1 ground, $40 / $80 = needs the matching item, $FF never) is 1 or
 * more and not above the hero's terrain level $C817 (the test of $2D06 / $3DB9); openings lead into a
 * cave or house, not across the map. So rivers and mountain lines
 * hide what is behind them until the item that crosses them is found. Not modelled: things the
 * sword clears (bushes) and routes that leave the 3 x 3 window.
 *
 * Drawing (all from the ROM + the game's RAM / VRAM tables, nothing stored in the project):
 *   layout  : screen -> room byte `$15890 + s`, room records `$15947 + 7 * room` (byte 0 = layout id:
 *             < $5B bank 3 `$C000 + id * 180`, else bank 4 `$10000 + (id - $5B) * 180`; 15 x 12
 *             metatiles); the hero's own screen from RAM $CA00 (opened caves, cut bushes).
 *   metatile: 4 tile numbers at RAM `$CC00 + 4 * m` (TL TR BL BR), tile = 256 + n, palette 0 ($0E34
 *             with $C047 = 1 on the overworld); tiles from VRAM.
 *   palette : per screen, bank 5 table `$15E48 + s` low nibble (0 = the previous screen's), id ->
 *             code `$1A77[id]` -> 16 colours via the asset pointer table of bank code >> 4 ($0E0B).
 *   tiles   : part of the tile set is loaded per palette id (VRAM $2E00, $1A6B), so the panel keeps
 *             a copy of VRAM $2000-$3FFF per id, taken while the hero is on a screen of that id
 *             (and CRAM shows its palette); other ids use the current tiles until visited.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hud.h"
#include "sms_runtime.h"
#include "video/sms_vdp.h"

extern uint8_t *glue_rom_image(size_t *size);
extern bool gv_pw_cave(int idx);
extern bool gv_map_seen(int idx);
extern const uint32_t *gv_map_overview(int *w, int *h, uint32_t *gen);

#define COLS 13
#define ROWS 14
#define NSCR (COLS * ROWS)
#define SW 240                                      /* one screen: 15 x 12 metatiles of 16 px */
#define SH 192
#define WIN 3

#define BG      0xFF0A1024u
#define BOX     0xFF141E3Cu
#define FRAME   0xFF3C6EA0u
#define WHITE   0xFFFFFFFFu
#define GREY    0xFF8C96AAu
#define CYAN    0xFF60D0FFu
#define GOLDC   0xFFF0C030u
#define POWC    0xFFE04040u

static const uint8_t *s_rom; static size_t s_rom_n;
static uint8_t  s_snap[16][0x2000];                 /* VRAM $2000-$3FFF per palette id */
static bool     s_has_snap[16];
static uint32_t s_img[WIN * SW * WIN * SH];
static uint32_t s_gen = 1;
static uint8_t  s_reach[WIN * WIN];
static int      s_c0 = -1, s_r0 = -1;
static uint32_t s_key = 0xFFFFFFFFu;

static uint32_t argb(uint8_t c)
{
    return 0xFF000000u | (uint32_t)((c & 3) * 85) << 16 | (uint32_t)(((c >> 2) & 3) * 85) << 8 | (uint32_t)(((c >> 4) & 3) * 85);
}
static int asset(int code)                          /* $0E0B: bank code >> 4, entry code & 15 -> ROM offset */
{
    int bank = (code >> 4) & 15, e = code & 15;
    size_t o = (size_t)bank * 0x4000 + (size_t)e * 2;
    if (o + 1 >= s_rom_n) return -1;
    int p = s_rom[o] | s_rom[o + 1] << 8;
    return bank * 0x4000 + (p - 0x8000);
}
static int pal_id(int s)
{
    int t = asset(0x5D); if (t < 0) return 1;
    int id = s_rom[t + s] & 15;
    if (!id && s > 0) id = s_rom[t + s - 1] & 15;
    return id;
}
static const uint8_t *palette(int id)
{
    int o = asset(s_rom[0x1A77 + (id & 15)]);
    return o >= 0 ? s_rom + o : NULL;
}
static const uint8_t *layout(int s)                /* ROM layout of screen s (180 metatiles) */
{
    int room = s_rom[0x15890 + s], id = s_rom[0x15947 + room * 7];
    size_t o = id < 0x5B ? 0xC000 + (size_t)id * 180 : 0x10000 + (size_t)(id - 0x5B) * 180;
    return o + 180 <= s_rom_n ? s_rom + o : NULL;
}
static int hero_screen(void) { int s = sms_read8(0xC80D); return sms_read8(0xC802) >= 5 && s < NSCR ? s : -1; }
/* A still overworld screen: overworld mode, overworld display, no scroll running, and the screen
   RAM $CA00 holds the hero's screen (= its ROM layout except a few opened caves / cut bushes).
   In caves and dungeons $C80D keeps the overworld screen while $CA00 / $CC00 / $D000 hold the
   cave's data; mid-scroll $CA00 is half old, half new. The panel only re-renders when this holds. */
static bool on_overworld(void)
{
    uint8_t m = sms_read8(0xC802); int s = sms_read8(0xC80D);
    if (!((m == 6 || m == 7) && (g_vdp.reg[0] & 0x20) && (g_vdp.reg[1] & 0x40) && sms_read8(0xC814) == 0 && s < NSCR)) return false;
    const uint8_t *l = s_rom ? layout(s) : NULL;
    if (!l) return false;
    int same = 0;
    for (int i = 0; i < 180; i++) same += sms_read8((uint16_t)(0xCA00 + i)) == l[i];
    if (same < 160) return false;
    const uint8_t *p = palette(sms_read8(0xC819) & 15);  /* CRAM = this area's palette (not a cave's, not mid-fade) */
    return p && memcmp(p, g_vdp.cram, 16) == 0;
}

/* keep the tiles of the current palette id (only while CRAM shows that palette: not mid-fade) */
static void snapshot(void)
{
    int s = hero_screen(); if (s < 0 || !on_overworld()) return;
    int id = sms_read8(0xC819) & 15;
    const uint8_t *p = palette(id);
    if (!p || memcmp(p, g_vdp.cram, 16) != 0) return;
    memcpy(s_snap[id], g_vdp.vram + 0x2000, 0x2000);
    if (!s_has_snap[id]) { s_has_snap[id] = true; s_key = 0xFFFFFFFFu; }
}

static uint8_t mt_at(int s, int cell, int hero)     /* metatile of a cell (the hero's screen: live) */
{
    if (s == hero) return sms_read8((uint16_t)(0xCA00 + cell));
    const uint8_t *l = layout(s);
    return l ? l[cell] : 0xFF;
}

/* flood fill from the hero's cell over the 45 x 36 cells of the window */
static void reach(int hero)
{
    enum { GW = 15 * WIN, GH = 12 * WIN };
    static uint8_t pass[GW * GH], seen[GW * GH]; static int q[GW * GH];
    uint8_t lvl = sms_read8(0xC817);
    for (int wy = 0; wy < WIN; wy++)
        for (int wx = 0; wx < WIN; wx++) {
            int s = (s_r0 + wy) * COLS + s_c0 + wx;
            for (int cell = 0; cell < 180; cell++) {
                uint8_t a = sms_read8((uint16_t)(0xD000 + mt_at(s, cell, hero)));
                pass[(wy * 12 + cell / 15) * GW + wx * 15 + cell % 15] = a >= 1 && a <= lvl;   /* 0 = openings (into a cave / house) */
            }
        }
    memset(seen, 0, sizeof seen); memset(s_reach, 0, sizeof s_reach);
    int hr = (sms_read8(0xC101) - 8) / 16, hc = (sms_read8(0xC102) - 16) / 16;
    if (hr < 0) hr = 0; if (hr > 11) hr = 11; if (hc < 0) hc = 0; if (hc > 14) hc = 14;
    int start = ((hero / COLS - s_r0) * 12 + hr) * GW + (hero % COLS - s_c0) * 15 + hc;
    int qn = 0; q[qn++] = start; seen[start] = 1;
    for (int i = 0; i < qn; i++) {
        int p = q[i], x = p % GW, y = p / GW;
        s_reach[(y / 12) * WIN + x / 15] = 1;
        static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
        for (int d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d];
            if (nx < 0 || ny < 0 || nx >= GW || ny >= GH) continue;
            int n = ny * GW + nx;
            if (!seen[n] && pass[n]) { seen[n] = 1; q[qn++] = n; }
        }
    }
    s_reach[(hero / COLS - s_r0) * WIN + hero % COLS - s_c0] = 1;
}

static void draw_screen(int s, int ox, int oy, int hero)
{
    int id = pal_id(s);
    const uint8_t *pal = palette(id);
    const uint8_t *tiles = s_has_snap[id] ? s_snap[id] : (on_overworld() ? g_vdp.vram + 0x2000 : NULL);
    if (!tiles) for (int k = 0; k < 16 && !tiles; k++) if (s_has_snap[k]) tiles = s_snap[k];
    uint32_t col[16];
    for (int i = 0; i < 16; i++) col[i] = argb(pal ? pal[i] : g_vdp.cram[i]);
    for (int cell = 0; cell < 180; cell++) {
        uint8_t m = mt_at(s, cell, hero);
        for (int q = 0; q < 4; q++) {
            int t = sms_read8((uint16_t)(0xCC00 + m * 4 + q));      /* tile 256 + t */
            int px = ox + (cell % 15) * 16 + (q & 1) * 8, py = oy + (cell / 15) * 16 + (q >> 1) * 8;
            for (int r = 0; r < 8; r++) {
                uint32_t *dst = s_img + (size_t)(py + r) * (WIN * SW) + px;
                if (!tiles) { for (int x = 0; x < 8; x++) dst[x] = 0xFF000000u; continue; }
                const uint8_t *row = tiles + t * 32 + r * 4;
                for (int x = 0; x < 8; x++) {
                    int b = 7 - x;
                    int c = ((row[0] >> b) & 1) | ((row[1] >> b) & 1) << 1 | ((row[2] >> b) & 1) << 2 | ((row[3] >> b) & 1) << 3;
                    dst[x] = col[c];
                }
            }
        }
    }
}
static void fill_screen(int ox, int oy, uint32_t c)
{
    for (int y = 0; y < SH; y++) for (int x = 0; x < SW; x++) s_img[(size_t)(oy + y) * (WIN * SW) + ox + x] = c;
}

/* re-render when the window, the hero's cell / screen data, the terrain level or the tiles change */
static void render(int hero)
{
    int hc = hero % COLS, hr = hero / COLS;
    int c0 = hc - 1, r0 = hr - 1;
    if (c0 < 0) c0 = 0; if (c0 > COLS - WIN) c0 = COLS - WIN;
    if (r0 < 0) r0 = 0; if (r0 > ROWS - WIN) r0 = ROWS - WIN;
    uint32_t key = (uint32_t)hero | (uint32_t)sms_read8(0xC817) << 8 | (uint32_t)((sms_read8(0xC101) - 8) / 16) << 16
                 | (uint32_t)((sms_read8(0xC102) - 16) / 16) << 20;
    uint32_t h = 2166136261u;                       /* the hero's live screen (revealed caves etc.) */
    for (int i = 0; i < 180; i++) h = (h ^ sms_read8((uint16_t)(0xCA00 + i))) * 16777619u;
    key ^= h;
    if (key == s_key && c0 == s_c0 && r0 == s_r0) return;
    s_key = key; s_c0 = c0; s_r0 = r0;
    reach(hero);
    for (int wy = 0; wy < WIN; wy++)
        for (int wx = 0; wx < WIN; wx++) {
            int s = (r0 + wy) * COLS + c0 + wx;
            if (s_reach[wy * WIN + wx]) draw_screen(s, wx * SW, wy * SH, hero);
            else fill_screen(wx * SW, wy * SH, 0xFF000000u);
        }
    s_gen++;
}

static void box(uint32_t *b, int w, int h, int x0, int y0, int x1, int y1)
{
    hud_fill(b, w, h, x0, y0, x1, y1, FRAME, false);
    hud_fill(b, w, h, x0 + 2, y0 + 2, x1 - 2, y1 - 2, BOX, false);
}
static int bcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }

extern bool gv_cave_panel(uint32_t *b, int w, int h);
void gv_panel_draw(uint32_t *b, int w, int h)
{
    if (gv_cave_panel(b, w, h)) return;             /* inside a dungeon: its map fills the panel (gv_cave.c) */
    for (int i = 0; i < w * h; i++) b[i] = BG;
    if (!s_rom) s_rom = glue_rom_image(&s_rom_n);
    int hero = hero_screen();
    if (hero < 0 || !s_rom || s_rom_n < 0x20000) {        /* title / intro */
        box(b, w, h, 8, 8, w - 8, h - 8);
        if (hud_logo(NULL, NULL)) hud_draw_logo(b, w, h, HUD_SPACE_PANEL, 24.f, (float)(h / 2 - 70), (float)(w - 48), 120.f, true);
        else hud_text_scaled(b, w, h, (w - 10 * 12) / 2, h / 2 - 7, "GOLVELLIUS", CYAN, 2);
        return;
    }
    snapshot();
    char t[48];
    int y = 8;
    /* logo / title */
    box(b, w, h, 8, y, w - 8, y + 70);
    if (hud_logo(NULL, NULL)) hud_draw_logo(b, w, h, HUD_SPACE_PANEL, 16.f, (float)y + 6, (float)(w - 32), 58.f, true);
    else hud_text_scaled(b, w, h, (w - 10 * 12) / 2, y + 28, "GOLVELLIUS", CYAN, 2);
    y += 78;

    /* surroundings */
    snprintf(t, sizeof t, "SCREEN %c%d", 'A' + hero % COLS, hero / COLS + 1);
    int bx0 = 8, bx1 = w - 8, mw = bx1 - bx0 - 8, mh = mw * (WIN * SH) / (WIN * SW);
    box(b, w, h, bx0, y, bx1, y + 24 + mh + 8);
    hud_text_scaled(b, w, h, 16, y + 6, t, CYAN, 2);
    if (gv_pw_cave(hero)) hud_text(b, w, h, w - 16 - 6 * 8, y + 10, "PASSWORD", 0xFFFF40FFu);
    float mx = (float)bx0 + 4, my = (float)y + 24;
    bool live = on_overworld();
    if (live) render(hero);                         /* else: keep the last overworld picture */
    if (s_c0 < 0) { s_c0 = hero % COLS - 1; s_r0 = hero / COLS - 1;
        if (s_c0 < 0) s_c0 = 0; if (s_c0 > COLS - WIN) s_c0 = COLS - WIN;
        if (s_r0 < 0) s_r0 = 0; if (s_r0 > ROWS - WIN) s_r0 = ROWS - WIN; }
    if (hud_hires()) hud_image(HUD_SPACE_PANEL, mx, my, (float)mw, (float)mh, s_img, WIN * SW, WIN * SH, 0, 0, WIN * SW, WIN * SH, false, s_gen);
    else
        for (int yy = 0; yy < mh; yy++)
            for (int xx = 0; xx < mw; xx++)
                b[(size_t)((int)my + yy) * w + (int)mx + xx] = s_img[(size_t)(yy * WIN * SH / mh) * (WIN * SW) + xx * WIN * SW / mw];
    {   float sc = (float)mw / (WIN * SW);
        for (int i = 1; i < WIN; i++) {               /* faint screen borders */
            hud_rect(HUD_SPACE_PANEL, mx + i * SW * sc, my, 0.5f, (float)mh, 0x50FFFFFFu);
            hud_rect(HUD_SPACE_PANEL, mx, my + i * SH * sc, (float)mw, 0.5f, 0x50FFFFFFu);
        }
        static unsigned blink; blink++;
        if (live && (blink / 20) % 3) {               /* the hero (cell centre) */
            float hx = (float)((hero % COLS - s_c0) * SW + sms_read8(0xC102) - 8);
            float hy = (float)((hero / COLS - s_r0) * SH + sms_read8(0xC101));
            hud_marker(HUD_SPACE_PANEL, mx + hx * sc, my + hy * sc, 3.f, 1.f, 0xFFFFE040u, 0xFF000000u);
        }
    }
    y += 24 + mh + 8 + 8;

    /* gold + power */
    box(b, w, h, 8, y, w - 8, y + 92);
    long gold = (bcd(sms_read8(0xC83F)) * 10000L + bcd(sms_read8(0xC840)) * 100L + bcd(sms_read8(0xC841))) * 10;
    long gmax = (bcd(sms_read8(0xC842)) * 10000L + bcd(sms_read8(0xC843)) * 100L + bcd(sms_read8(0xC844))) * 10;
    hud_text_scaled(b, w, h, 16, y + 8, "GOLD", GOLDC, 2);
    snprintf(t, sizeof t, "%ld", gold); hud_text_scaled(b, w, h, w - 16 - 12 * (int)strlen(t), y + 8, t, WHITE, 2);
    hud_text_scaled(b, w, h, 16, y + 28, "MAX", GREY, 2);
    snprintf(t, sizeof t, "%ld", gmax); hud_text_scaled(b, w, h, w - 16 - 12 * (int)strlen(t), y + 28, t, GREY, 2);
    int pw = bcd(sms_read8(0xC836)), pmax = bcd(sms_read8(0xC839));
    hud_text_scaled(b, w, h, 16, y + 50, "POWER", POWC, 2);
    snprintf(t, sizeof t, "%d/%d", pw, pmax); hud_text_scaled(b, w, h, w - 16 - 12 * (int)strlen(t), y + 50, t, WHITE, 2);
    hud_fill(b, w, h, 16, y + 72, w - 16, y + 80, 0xFF000000u, false);
    if (pmax > 0) hud_fill(b, w, h, 16, y + 72, 16 + (w - 32) * (pw > pmax ? pmax : pw) / pmax, y + 80, POWC, false);
    y += 100;

    /* world map: the screens visited so far (gv_map.c, the MAP tab's picture), the 3 x 3 window framed */
    int ow, oh; uint32_t ogen; const uint32_t *ov = gv_map_overview(&ow, &oh, &ogen);
    int avail = h - 8 - y - 24 - 8;
    float ms = (float)(w - 24) / ow; if (oh * ms > avail) ms = (float)avail / oh;
    if (ov && ms > 0 && avail > 40) {
        float dw = ow * ms, dh = oh * ms;
        box(b, w, h, 8, y, w - 8, y + 24 + (int)dh + 8);
        int seen = 0; for (int i = 0; i < NSCR; i++) seen += gv_map_seen(i);
        hud_text_scaled(b, w, h, 16, y + 6, "WORLD", CYAN, 2);
        snprintf(t, sizeof t, "%d/%d", seen, NSCR); hud_text(b, w, h, w - 16 - 6 * (int)strlen(t), y + 10, t, GREY);
        float ox = 8 + ((w - 16) - dw) / 2, oy = (float)y + 24;
        hud_fill(b, w, h, (int)ox, (int)oy, (int)(ox + dw), (int)(oy + dh), 0xFF000000u, false);
        float cw = dw / COLS, ch = dh / ROWS;
        if (hud_hires()) {
            hud_image_alpha(HUD_SPACE_PANEL, ox, oy, dw, dh, ov, ow, oh, ogen);
            for (int i = 0; i < NSCR; i++)                    /* visited password caves: magenta corner */
                if (gv_map_seen(i) && gv_pw_cave(i))
                    hud_rect(HUD_SPACE_PANEL, ox + (i % COLS + 1) * cw - cw * 0.35f, oy + (i / COLS) * ch, cw * 0.35f, cw * 0.35f, 0xFFFF40FFu);
            float fx = ox + s_c0 * cw, fy = oy + s_r0 * ch, lw = 1.f;     /* the surroundings window */
            hud_rect(HUD_SPACE_PANEL, fx, fy, WIN * cw, lw, CYAN); hud_rect(HUD_SPACE_PANEL, fx, fy + WIN * ch - lw, WIN * cw, lw, CYAN);
            hud_rect(HUD_SPACE_PANEL, fx, fy, lw, WIN * ch, CYAN); hud_rect(HUD_SPACE_PANEL, fx + WIN * cw - lw, fy, lw, WIN * ch, CYAN);
            hud_marker(HUD_SPACE_PANEL, ox + (hero % COLS + 0.5f) * cw, oy + (hero / COLS + 0.5f) * ch, 2.f, 1.f, 0xFFFFE040u, 0xFF000000u);
        }
    }
}

/* test: GV_PANEL_PPM=<file> writes the surroundings picture (720 x 576) as the panel has it at each
   frame of GV_PANEL_FRAME (comma list; the panel's update runs every frame meanwhile, as in the
   window). With a list, "%d" in the file name = the frame. GV_PANEL_LOG=1: one line per re-render. */
void gv_panel_test(uint64_t frame)
{
    const char *p = getenv("GV_PANEL_PPM"), *f = getenv("GV_PANEL_FRAME");
    if (!p || !f) return;
    if (!s_rom) s_rom = glue_rom_image(&s_rom_n);
    int hero = hero_screen(); if (hero < 0) return;
    snapshot();
    uint32_t g0 = s_gen;
    if (on_overworld()) render(hero);
    if (s_gen != g0 && getenv("GV_PANEL_LOG")) {
        fprintf(stderr, "[gv_panel] frame %llu render screen %d hero y %d x %d mode %d reach", (unsigned long long)frame, hero, sms_read8(0xC101), sms_read8(0xC102), sms_read8(0xC802));
        for (int i = 0; i < WIN * WIN; i++) fprintf(stderr, " %d", s_reach[i]);
        fprintf(stderr, "\n");
    }
    bool hit = false;
    for (const char *q = f; *q; ) { if ((uint64_t)atoll(q) == frame) hit = true; q = strchr(q, ','); if (!q) break; q++; }
    if (!hit) return;
    char fn[512]; snprintf(fn, sizeof fn, p, (int)frame);
    FILE *o = fopen(fn, "wb"); if (!o) return;
    fprintf(o, "P6\n%d %d\n255\n", WIN * SW, WIN * SH);
    for (int i = 0; i < WIN * SW * WIN * SH; i++) { uint8_t c[3] = { (uint8_t)(s_img[i] >> 16), (uint8_t)(s_img[i] >> 8), (uint8_t)s_img[i] }; fwrite(c, 1, 3, o); }
    fclose(o);
    fprintf(stderr, "[gv_panel] screen %d window %d,%d reach", hero, s_c0, s_r0);
    for (int i = 0; i < WIN * WIN; i++) fprintf(stderr, " %d", s_reach[i]);
    fprintf(stderr, " -> %s\n", fn);
}
