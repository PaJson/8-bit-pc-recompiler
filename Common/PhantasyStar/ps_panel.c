/*
 * ps_panel.c - Phantasy Star (SMS) side panel for the widescreen window (SEGA AGES style): party
 * status, Meseta and a map box (surroundings map on a planet / in a town, the dungeon map in a
 * dungeon). Drawn by the runner's side-panel hook (hud.h, HUD_PANEL_W x HUD_PANEL_H) every presented
 * frame; read-only like the other PS hooks.
 *
 * Party data (ps1.constants.asm): stats at $C400 + $10 * n (Alis, Myau, Odin, Noah): +0 status
 * (1 = alive), +1 HP, +2 MP, +5 level, +6 max HP, +7 max MP; Party_curr_num $C4F0 (0-based);
 * Current_money $C4E0 (word).
 * Portraits: placeholders (initial letter) until the user supplies images.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hud.h"

extern uint8_t     sms_read8(uint16_t addr);
extern bool        ps_worldmap_draw(uint32_t *fb, int w, int h, int bx, int by, int bw, int bh);
extern const char *ps_worldmap_area_name(void);
extern bool        ps_automap_draw(uint32_t *fb, int w, int h, int x, int y, int cs);
extern int         ps_automap_floor(void);
extern bool        ps_automap_here(void);
extern int         ps_map_context(void);       /* ps_qol.c: 0 none, 1 dungeon, 2 planet / town */

#define BG      0xFF0A1024u
#define BOX     0xFF141E3Cu
#define FRAME   0xFF3C6EA0u
#define WHITE   0xFFFFFFFFu
#define GREY    0xFF8C96AAu
#define CYAN    0xFF60D0FFu
#define GOLD    0xFFF0C030u
#define HPCOL   0xFF40D060u
#define MPCOL   0xFF4080F0u
#define DEADCOL 0xFFC03C3Cu

static void box(uint32_t *b, int w, int h, int x0, int y0, int x1, int y1)
{
    hud_fill(b, w, h, x0, y0, x1, y1, FRAME, false);
    hud_fill(b, w, h, x0 + 2, y0 + 2, x1 - 2, y1 - 2, BOX, false);
}

static void bar(uint32_t *b, int w, int h, int x, int y, int bw, unsigned cur, unsigned max, uint32_t col)
{
    hud_fill(b, w, h, x, y, x + bw, y + 4, 0xFF000000u, false);
    if (max) hud_fill(b, w, h, x, y, x + (int)(bw * (cur > max ? max : cur) / max), y + 4, col, false);
}

static int text_right(uint32_t *b, int w, int h, int xr, int y, const char *s, uint32_t c, int k)
{
    return hud_text_scaled(b, w, h, xr - 6 * k * (int)strlen(s) + k, y, s, c, k);
}

void ps_panel_draw(uint32_t *b, int w, int h)
{
    for (int i = 0; i < w * h; i++) b[i] = BG;
    if (sms_read8(0xC202) <= 3) {                      /* title / intro: no game data yet */
        box(b, w, h, 8, 8, w - 8, h - 8);
        hud_text_scaled(b, w, h, (w - 8 * 12) / 2, h / 2 - 30, "PHANTASY", CYAN, 2);
        hud_text_scaled(b, w, h, (w - 4 * 12) / 2, h / 2 - 6, "STAR", CYAN, 2);
        return;
    }
    static const char *const names[4] = { "ALIS", "MYAU", "ODIN", "NOAH" };
    static const uint32_t pcol[4] = { 0xFFC85A78u, 0xFFD2B43Cu, 0xFF5A82C8u, 0xFF64B48Cu };
    int y = 8, n = sms_read8(0xC4F0) + 1;
    if (n < 1 || n > 4) n = 1;
    char buf[32];
    for (int c = 0; c < 4; c++, y += 68) {
        if (c >= n) continue;
        uint16_t s = (uint16_t)(0xC400 + 0x10 * c);
        bool alive = sms_read8(s) & 1;
        box(b, w, h, 8, y, w - 8, y + 62);
        /* portrait placeholder */
        hud_fill(b, w, h, 14, y + 7, 62, y + 55, alive ? pcol[c] : 0xFF404040u, false);
        { char ini[2] = { names[c][0], 0 }; hud_text_scaled(b, w, h, 14 + 13, y + 17, ini, 0xFFFFFFFFu, 4); }
        int tx = 70, tr = w - 14;
        hud_text_scaled(b, w, h, tx, y + 6, names[c], alive ? WHITE : DEADCOL, 2);
        snprintf(buf, sizeof buf, "LV%u", sms_read8(s + 5));
        text_right(b, w, h, tr, y + 6, buf, CYAN, 2);
        unsigned hp = sms_read8(s + 1), mhp = sms_read8(s + 6), mp = sms_read8(s + 2), mmp = sms_read8(s + 7);
        snprintf(buf, sizeof buf, "HP %u/%u", hp, mhp);
        hud_text(b, w, h, tx, y + 26, buf, alive ? WHITE : GREY);
        bar(b, w, h, tx, y + 35, tr - tx, hp, mhp, HPCOL);
        if (mmp) {
            snprintf(buf, sizeof buf, "MP %u/%u", mp, mmp);
            hud_text(b, w, h, tx, y + 42, buf, alive ? WHITE : GREY);
            bar(b, w, h, tx, y + 51, tr - tx, mp, mmp, MPCOL);
        }
    }
    /* Meseta */
    y = 8 + 4 * 68;
    box(b, w, h, 8, y, w - 8, y + 30);
    hud_text_scaled(b, w, h, 16, y + 8, "M", GOLD, 2);
    snprintf(buf, sizeof buf, "%u", sms_read8(0xC4E0) | (sms_read8(0xC4E1) << 8));
    text_right(b, w, h, w - 16, y + 8, buf, WHITE, 2);
    /* map box */
    y += 36;
    box(b, w, h, 8, y, w - 8, h - 8);
    int ctx = ps_map_context(), mx0 = 14, mx1 = w - 14, my0 = y + 24, my1 = h - 14;
    if (ctx == 1 && ps_automap_floor() >= 0) {
        snprintf(buf, sizeof buf, "%s %02X", ps_automap_here() ? "FLOOR" : "LAST FLOOR", ps_automap_floor());
        hud_text_scaled(b, w, h, 16, y + 6, buf, CYAN, 2);
        int cs = (mx1 - mx0) / 16; if ((my1 - my0) / 16 < cs) cs = (my1 - my0) / 16;
        ps_automap_draw(b, w, h, mx0 + (mx1 - mx0 - 16 * cs) / 2, my0 + (my1 - my0 - 16 * cs) / 2, cs);
    } else if (ctx == 2) {
        hud_text_scaled(b, w, h, 16, y + 6, ps_worldmap_area_name(), CYAN, 2);
        hud_fill(b, w, h, mx0, my0, mx1, my1, 0xFF000000u, false);
        ps_worldmap_draw(b, w, h, mx0, my0, mx1 - mx0, my1 - my0);
    } else {
        hud_text_scaled(b, w, h, 16, y + 6, "MAP", CYAN, 2);
        hud_text(b, w, h, 16, my0 + 4, "NO MAP YET", GREY);
    }
}

/* test: PS_PANEL_PPM=<file> + PS_PANEL_FRAME=<n> writes the panel of frame n (headless, no window) */
void ps_panel_frame(uint64_t frame)
{
    {   /* test: PS_PANEL_TOGGLE=<n> flips the panel on/off at frame n (as the HUD option does) */
        static long tog = -2;
        if (tog == -2) { const char *t = getenv("PS_PANEL_TOGGLE"); tog = t ? atol(t) : -1; }
        if (tog >= 0 && frame == (uint64_t)tog) hud_set_panel_enabled(!hud_panel_active());
    }
    static int armed = -1; static uint64_t at; static const char *path;
    if (armed < 0) {
        path = getenv("PS_PANEL_PPM"); const char *f = getenv("PS_PANEL_FRAME");
        armed = path && f; if (armed) at = (uint64_t)strtoull(f, NULL, 10);
    }
    if (!armed || frame != at) return;
    static uint32_t buf[HUD_PANEL_W * HUD_PANEL_H];
    ps_panel_draw(buf, HUD_PANEL_W, HUD_PANEL_H);
    FILE *fp = fopen(path, "wb");
    if (!fp) return;
    fprintf(fp, "P6\n%d %d\n255\n", HUD_PANEL_W, HUD_PANEL_H);
    for (int i = 0; i < HUD_PANEL_W * HUD_PANEL_H; i++) { uint8_t c[3] = { (uint8_t)(buf[i] >> 16), (uint8_t)(buf[i] >> 8), (uint8_t)buf[i] }; fwrite(c, 1, 3, fp); }
    fclose(fp);
}
