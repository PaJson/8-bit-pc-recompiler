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
extern bool        ps_worldmap_draw(uint32_t *fb, int w, int h, int bx, int by, int bw, int bh, int space);
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

/* ---- save-slot preview ----
 * The slot menu of CONTINUE / SAVE / delete (ps1.asm LABEL_39B1 -> CheckOptionSelect): Cursor_pos
 * $C269 = $78EE, Option_total_num $C26E = 4 (5 slots), $C268 = $FF while choosing, cursor index
 * $C26B (0-based). Battery RAM: slot n's copy of RAM $C300-$C6FF at offset n * $400, slot-used flag
 * at $200 + n (LABEL_73A). While the menu is open the panel shows the highlighted slot. */
extern const uint8_t *glue_sram_image(size_t *size);
extern bool ps_worldmap_draw_at(uint32_t *fb, int w, int h, int bx, int by, int bw, int bh, int space,
                                int tileset, int map, int px, int py, const char **name);
extern bool ps_automap_draw_slot(uint32_t *fb, int w, int h, int x, int y, int cs, int slot, const uint8_t *blk);

static const uint8_t *g_blk;                           /* previewed slot block, NULL = live RAM */
static uint8_t rd(uint16_t a)
{
    if (g_blk && a >= 0xC300 && a < 0xC700) return g_blk[a - 0xC300];
    return sms_read8(a);
}

/* the highlighted slot (1..5) while the slot menu is open, else 0; *used = slot holds a save */
static int slot_menu(const uint8_t **blk, bool *used)
{
    if (sms_read8(0xC268) != 0xFF || sms_read8(0xC269) != 0xEE || sms_read8(0xC26A) != 0x78 || sms_read8(0xC26E) != 4)
        return 0;
    int slot = sms_read8(0xC26B) + 1;
    size_t n = 0; const uint8_t *sram = glue_sram_image(&n);
    if (slot < 1 || slot > 5 || !sram || n < (size_t)(slot + 1) * 0x400) return 0;
    *used = sram[0x200 + slot] != 0;
    *blk = sram + slot * 0x400;
    return slot;
}

void ps_panel_draw(uint32_t *b, int w, int h)
{
    for (int i = 0; i < w * h; i++) b[i] = BG;
    char buf[32];
    bool used = false; const uint8_t *blk = NULL;
    int slot = slot_menu(&blk, &used);
    if (slot && !used) {                               /* empty slot */
        box(b, w, h, 8, 8, w - 8, h - 8);
        snprintf(buf, sizeof buf, "SLOT %d", slot);
        hud_text_scaled(b, w, h, (w - 6 * 12) / 2, h / 2 - 30, buf, CYAN, 2);
        hud_text_scaled(b, w, h, (w - 5 * 12) / 2, h / 2 - 6, "EMPTY", GREY, 2);
        return;
    }
    g_blk = slot ? blk : NULL;
    /* title / intro, or nothing loaded yet (the CONTINUE question comes before any party data) */
    if (!slot && (sms_read8(0xC202) <= 3 || rd(0xC405) == 0)) {
        box(b, w, h, 8, 8, w - 8, h - 8);
        hud_text_scaled(b, w, h, (w - 8 * 12) / 2, h / 2 - 30, "PHANTASY", CYAN, 2);
        hud_text_scaled(b, w, h, (w - 4 * 12) / 2, h / 2 - 6, "STAR", CYAN, 2);
        return;
    }
    static const char *const names[4] = { "ALIS", "MYAU", "ODIN", "NOAH" };
    static const uint32_t pcol[4] = { 0xFFC85A78u, 0xFFD2B43Cu, 0xFF5A82C8u, 0xFF64B48Cu };
    int y = 8, n = rd(0xC4F0) + 1;
    if (n < 1 || n > 4) n = 1;
    for (int c = 0; c < 4; c++, y += 68) {
        if (c >= n) continue;
        uint16_t s = (uint16_t)(0xC400 + 0x10 * c);
        bool alive = rd(s) & 1;
        box(b, w, h, 8, y, w - 8, y + 62);
        /* portrait placeholder */
        hud_fill(b, w, h, 14, y + 7, 62, y + 55, alive ? pcol[c] : 0xFF404040u, false);
        { char ini[2] = { names[c][0], 0 }; hud_text_scaled(b, w, h, 14 + 13, y + 17, ini, 0xFFFFFFFFu, 4); }
        int tx = 70, tr = w - 14;
        hud_text_scaled(b, w, h, tx, y + 6, names[c], alive ? WHITE : DEADCOL, 2);
        snprintf(buf, sizeof buf, "LV%u", rd(s + 5));
        text_right(b, w, h, tr, y + 6, buf, CYAN, 2);
        unsigned hp = rd(s + 1), mhp = rd(s + 6), mp = rd(s + 2), mmp = rd(s + 7);
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
    snprintf(buf, sizeof buf, "%u", rd(0xC4E0) | (rd(0xC4E1) << 8));
    text_right(b, w, h, w - 16, y + 8, buf, WHITE, 2);
    /* map box */
    y += 36;
    box(b, w, h, 8, y, w - 8, h - 8);
    int mx0 = 14, mx1 = w - 14, my0 = y + 24, my1 = h - 14;
    int cs = (mx1 - mx0) / 16; if ((my1 - my0) / 16 < cs) cs = (my1 - my0) / 16;
    if (slot) {                                        /* the slot's save position */
        bool ok;
        if (rd(0xC316) == 0x0B) {                      /* saved in a dungeon (LABEL_678: -> LoadDungeon) */
            snprintf(buf, sizeof buf, "SLOT %d FLOOR %02X", slot, rd(0xC30D));
            ok = ps_automap_draw_slot(b, w, h, mx0 + (mx1 - mx0 - 16 * cs) / 2, my0 + (my1 - my0 - 16 * cs) / 2, cs, slot, blk);
        } else {                                       /* planet / town: camera + the party's cell (128, 96) */
            const char *name = "MAP";
            hud_fill(b, w, h, mx0, my0, mx1, my1, 0xFF000000u, false);
            int px = rd(0xC302) * 256 + rd(0xC301) + 136, py = rd(0xC306) * 192 + rd(0xC305) + 104;
            ok = ps_worldmap_draw_at(b, w, h, mx0, my0, mx1 - mx0, my1 - my0, HUD_SPACE_PANEL, rd(0xC308), rd(0xC309), px, py, &name);
            snprintf(buf, sizeof buf, "SLOT %d %s", slot, name);
        }
        hud_text_scaled(b, w, h, 16, y + 6, buf, CYAN, 2);
        if (!ok) hud_text(b, w, h, 16, my0 + 4, "NO MAP SAVED FOR THIS SLOT", GREY);
        g_blk = NULL;
        return;
    }
    int ctx = ps_map_context();
    if (ctx == 1 && ps_automap_floor() >= 0) {
        snprintf(buf, sizeof buf, "%s %02X", ps_automap_here() ? "FLOOR" : "LAST FLOOR", ps_automap_floor());
        hud_text_scaled(b, w, h, 16, y + 6, buf, CYAN, 2);
        ps_automap_draw(b, w, h, mx0 + (mx1 - mx0 - 16 * cs) / 2, my0 + (my1 - my0 - 16 * cs) / 2, cs);
    } else if (ctx == 2) {
        hud_text_scaled(b, w, h, 16, y + 6, ps_worldmap_area_name(), CYAN, 2);
        hud_fill(b, w, h, mx0, my0, mx1, my1, 0xFF000000u, false);
        ps_worldmap_draw(b, w, h, mx0, my0, mx1 - mx0, my1 - my0, HUD_SPACE_PANEL);
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
        /* test: PS_HUD_OPEN=<n> opens the HUD at frame n (window tests: SMS_WINDOW_SHOT + _EXIT) */
        static long hop = -2;
        if (hop == -2) { const char *t = getenv("PS_HUD_OPEN"); hop = t ? atol(t) : -1; }
        if (hop >= 0 && frame == (uint64_t)hop && !hud_is_open()) hud_key(HUD_KEY_TOGGLE);
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
