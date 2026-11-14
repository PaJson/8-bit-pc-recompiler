/*
 * ps_panel.c - Phantasy Star (SMS) side panel for the widescreen window (SEGA AGES style): party
 * status, Meseta and a map box (surroundings map on a planet / in a town, the dungeon map in a
 * dungeon). Drawn by the runner's side-panel hook (hud.h, HUD_PANEL_W x HUD_PANEL_H) every presented
 * frame; read-only like the other PS hooks.
 *
 * Party data (ps1.constants.asm): stats at $C400 + $10 * n (Alis, Myau, Odin, Noah): +0 status
 * (1 = alive), +1 HP, +2 MP, +5 level, +6 max HP, +7 max MP; Party_curr_num $C4F0 (0-based);
 * Current_money $C4E0 (word).
 * Portraits: the game's own character pictures (the cutscene portraits), decoded from the ROM at
 * start-up; ROMs whose picture packer differs (2.5 retranslation) keep the initial-letter tiles.
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
extern const char *ps_automap_floor_name(int floor);
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

/* ---- portraits ----
 * LABEL_46D1(n) (ps1.asm) shows cutscene picture n from LABEL_471E: 5-byte entries {bank, ptr, map};
 * at ptr 16 palette bytes, then the tiles packed for LABEL_3FA (4 bit planes, each RLE: n < $80 =
 * repeat the next byte n times, n >= $80 = copy n & $7F bytes, 0 = end; every 4th byte), written to
 * VRAM $2000 (tile 256); the map (bank 24 in every ROM, found after the call) is 12 rows x 20 name-
 * table words (LABEL_390) = a 160 x 96 picture. Pictures 3..6 = Alis, Myau, Odin, Noah (0 Nero hurt,
 * 1 Nero, 2 Alis praying, 7/8 Myau's winged forms). Found by code pattern in all 10 PS ROMs. */
#define POR_W 160
#define POR_H 96
static uint32_t g_por[4][POR_W * POR_H];
static int      g_por_state;                           /* 0 not tried, 1 ok, -1 not available */
/* face crop (x, y) of a 72 x 72 square in each picture */
static const int k_face[4][2] = { { 52, 4 }, { 60, 8 }, { 20, 8 }, { 38, 6 } };
#define FACE 72
extern uint8_t *glue_rom_image(size_t *size);

static uint32_t por_rgb(uint8_t c) { return 0xFF000000u | (uint32_t)(c & 3) * 85u << 16 | (uint32_t)(c >> 2 & 3) * 85u << 8 | (uint32_t)(c >> 4 & 3) * 85u; }

static void portraits_init(void)
{
    g_por_state = -1;
    size_t n = 0; const uint8_t *rom = glue_rom_image(&n);
    if (!rom || n < 0x10000) return;
    /* ld l,a ; add a,a ; add a,a ; add a,l ; ld l,a ; ld h,0 ; ld de,LABEL_471E */
    static const uint8_t pat[] = { 0x6F, 0x87, 0x87, 0x85, 0x6F, 0x26, 0x00, 0x11 };
    long i = -1;
    for (size_t k = 0; k + sizeof pat + 2 <= 0x8000 && k + sizeof pat + 2 <= n; k++) if (!memcmp(rom + k, pat, sizeof pat)) { i = (long)k; break; }
    if (i < 0) return;
    long tbl = rom[i + 8] | rom[i + 9] << 8;
    /* ... ld de,$6000 ; call LABEL_3FA ; ld hl,$FFFF ; ld (hl),map bank */
    long c = -1, mb = -1;
    for (long k = i; k < i + 64 && k + 6 < (long)n; k++) {
        if (c < 0 && rom[k] == 0x11 && rom[k + 1] == 0x00 && rom[k + 2] == 0x60 && rom[k + 3] == 0xCD) c = rom[k + 4] | rom[k + 5] << 8;
        if (c >= 0 && rom[k] == 0x21 && rom[k + 1] == 0xFF && rom[k + 2] == 0xFF && rom[k + 3] == 0x36) { mb = rom[k + 4]; break; }
    }
    /* the call must go to the standard unpacker (ld b,4 ; push bc ; push de ; call ; pop de ; inc de ;
       pop bc ; djnz ; ret), else the pictures are packed another way (2.5 retranslation) */
    static const uint8_t u1[] = { 0x06, 0x04, 0xC5, 0xD5, 0xCD }, u2[] = { 0xD1, 0x13, 0xC1, 0x10, 0xF6, 0xC9 };
    if (c < 0 || mb < 0 || c + 13 > (long)n || memcmp(rom + c, u1, sizeof u1) || memcmp(rom + c + 7, u2, sizeof u2)) {
        fprintf(stderr, "[panel] character pictures use another packer: portraits shown as letters\n");
        return;
    }
    static uint8_t tiles[0x4000];
    for (int p = 0; p < 4; p++) {
        const uint8_t *e = rom + tbl + (3 + p) * 5;
        long pal = (long)e[0] * 0x4000 + (e[1] | e[2] << 8) - 0x8000, map = mb * 0x4000 + (e[3] | e[4] << 8) - 0x8000;
        if (pal < 0 || map < 0 || pal + 16 >= (long)n || map + 12 * 40 > (long)n) return;
        uint32_t cols[16]; for (int k = 0; k < 16; k++) cols[k] = por_rgb(rom[pal + k]);
        memset(tiles, 0, sizeof tiles);
        long o = pal + 16;
        for (int plane = 0; plane < 4; plane++)
            for (int a = plane;;) {
                if (o >= (long)n) return;
                uint8_t cc = rom[o++];
                if (!cc) break;
                int cnt = cc & 0x7F;
                if (cc & 0x80) { for (int k = 0; k < cnt; k++, a += 4) { if (a < 0x4000 && o < (long)n) tiles[a] = rom[o]; o++; } }
                else { uint8_t b = o < (long)n ? rom[o] : 0; o++; for (int k = 0; k < cnt; k++, a += 4) if (a < 0x4000) tiles[a] = b; }
            }
        for (int y = 0; y < POR_H; y++) for (int x = 0; x < POR_W; x++) {
            long mo = map + ((y >> 3) * 20 + (x >> 3)) * 2;
            unsigned w = rom[mo] | rom[mo + 1] << 8;
            int t = (int)(w & 0x1FF) - 256, fy = (w & 0x400) ? 7 - (y & 7) : (y & 7), fx = (w & 0x200) ? 7 - (x & 7) : (x & 7);
            int col = 0;
            if (t >= 0 && t < 512) {
                const uint8_t *r = tiles + t * 32 + fy * 4; int b = 7 - fx;
                col = (r[0] >> b & 1) | (r[1] >> b & 1) << 1 | (r[2] >> b & 1) << 2 | (r[3] >> b & 1) << 3;
            }
            g_por[p][y * POR_W + x] = cols[col];
        }
    }
    g_por_state = 1;
}

/* the face of character c into (x, y, s x s) of the panel: hi-res layer (sharp at any window size),
   else area-averaged into the panel image; dead = darkened */
static void draw_portrait(uint32_t *b, int w, int h, int c, int x, int y, int s, bool alive)
{
    const int fx = k_face[c][0], fy = k_face[c][1];
    if (hud_hires()) {
        hud_image(HUD_SPACE_PANEL, (float)x, (float)y, (float)s, (float)s, g_por[c], POR_W, POR_H, fx, fy, FACE, FACE, false, 0x9000u + (uint32_t)c);
        if (!alive) hud_rect(HUD_SPACE_PANEL, (float)x, (float)y, (float)s, (float)s, 0xB0000000u);
        return;
    }
    for (int yy = 0; yy < s; yy++) for (int xx = 0; xx < s; xx++) {
        int x0 = fx + xx * FACE / s, x1 = fx + (xx + 1) * FACE / s, y0 = fy + yy * FACE / s, y1 = fy + (yy + 1) * FACE / s;
        unsigned r = 0, g = 0, bl = 0, k = 0;
        for (int sy = y0; sy < y1; sy++) for (int sx = x0; sx < x1; sx++) { uint32_t p = g_por[c][sy * POR_W + sx]; r += p >> 16 & 255; g += p >> 8 & 255; bl += p & 255; k++; }
        if (!k) continue;
        r /= k; g /= k; bl /= k;
        if (!alive) { r = r * 30 / 100; g = g * 30 / 100; bl = bl * 30 / 100; }
        if (x + xx >= 0 && x + xx < w && y + yy >= 0 && y + yy < h) b[(y + yy) * w + x + xx] = 0xFF000000u | r << 16 | g << 8 | bl;
    }
}

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
extern int  ps_worldmap_map_tileset(int map, int fallback);
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
        if (!g_por_state) portraits_init();
        if (g_por_state > 0) draw_portrait(b, w, h, c, 14, y + 7, 48, alive);
        else {                                         /* placeholder: initial letter */
            hud_fill(b, w, h, 14, y + 7, 62, y + 55, alive ? pcol[c] : 0xFF404040u, false);
            char ini[2] = { names[c][0], 0 }; hud_text_scaled(b, w, h, 14 + 13, y + 17, ini, 0xFFFFFFFFu, 4);
        }
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
    hud_text_scaled(b, w, h, 16, y + 8, "MST", GOLD, 2);
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
            ok = ps_worldmap_draw_at(b, w, h, mx0, my0, mx1 - mx0, my1 - my0, HUD_SPACE_PANEL, ps_worldmap_map_tileset(rd(0xC309), rd(0xC308)), rd(0xC309), px, py, &name);
            snprintf(buf, sizeof buf, "SLOT %d %s", slot, name);
        }
        hud_text_scaled(b, w, h, 16, y + 6, buf, CYAN, 2);
        if (!ok) hud_text(b, w, h, 16, my0 + 4, "NO MAP SAVED FOR THIS SLOT", GREY);
        g_blk = NULL;
        return;
    }
    int ctx = ps_map_context();
    if (ctx == 1 && ps_automap_floor() >= 0) {
        /* the floor's name (the first of the places that share this layout), small font if long */
        const char *nm = ps_automap_floor_name(ps_automap_floor());
        if (nm) { size_t n = strcspn(nm, "|"); if (n >= sizeof buf) n = sizeof buf - 1; memcpy(buf, nm, n); buf[n] = 0; }
        else snprintf(buf, sizeof buf, "FLOOR %02X", ps_automap_floor());
        if (!ps_automap_here()) { char t[sizeof buf]; snprintf(t, sizeof t, "Last: %s", buf); memcpy(buf, t, sizeof buf); }
        if ((int)strlen(buf) * 12 <= w - 24) hud_text_scaled(b, w, h, 16, y + 6, buf, CYAN, 2);
        else hud_text(b, w, h, 16, y + 10, buf, CYAN);
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
        if (hop >= 0 && frame == (uint64_t)hop && !hud_is_open()) {
            hud_key(HUD_KEY_TOGGLE);
            /* PS_HUD_KEYS: keys sent right after opening: U D L R = directions, [ ] = page prev / next,
               S = confirm, C = close */
            const char *k = getenv("PS_HUD_KEYS");
            for (; k && *k; k++)
                switch (*k) {
                case 'U': hud_key(HUD_KEY_UP); break;     case 'D': hud_key(HUD_KEY_DOWN); break;
                case 'L': hud_key(HUD_KEY_LEFT); break;   case 'R': hud_key(HUD_KEY_RIGHT); break;
                case '[': hud_key(HUD_KEY_PAGE_PREV); break; case ']': hud_key(HUD_KEY_PAGE_NEXT); break;
                case 'S': hud_key(HUD_KEY_SELECT); break; case 'C': hud_key(HUD_KEY_CLOSE); break;
                }
        }
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
