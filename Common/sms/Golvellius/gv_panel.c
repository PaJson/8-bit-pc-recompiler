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
 * sword clears (bushes) and routes that leave the window plus one ring of screens around it.
 *
 * Drawing (all from the ROM + the game's RAM / VRAM tables, nothing stored in the project):
 *   layout  : screen -> room byte `$15890 + s`, room records `$15947 + 7 * room` (byte 0 = layout id:
 *             < $5B bank 3 `$C000 + id * 180`, else bank 4 `$10000 + (id - $5B) * 180`; 15 x 12
 *             metatiles); the hero's own screen from RAM $CA00 (opened caves, cut bushes).
 *   metatile: 4 tile numbers at RAM `$CC00 + 4 * m` (TL TR BL BR), tile = 256 + n, palette 0 ($0E34
 *             with $C047 = 1 on the overworld); tiles from VRAM.
 *   palette : per screen, bank 5 table `$15E48 + s` low nibble (0 = the previous screen's), id ->
 *             code `$1A77[id]` -> 16 colours via the asset pointer table of bank code >> 4 ($0E0B).
 *   tiles   : tiles, metatiles and terrain depend on the screen's area (see "Set" below): the hero's
 *             screen uses what the game has loaded, the others are rebuilt from the ROM per area.
 * A screen is also shown when it was visited before (gv_map.c), even if the fill does not reach it.
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
extern const char *gv_area_name(int scr);
extern bool gv_cave_label(int scr, char *out, size_t n, int *done);
extern bool gv_cave_known(int scr);
extern bool gv_map_seen(int idx);
extern bool gv_is_plus(void);
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
static uint32_t s_img[WIN * SW * WIN * SH];
static uint32_t s_gen = 1;
static uint8_t  s_reach[WIN * WIN];
static int      s_c0 = -1, s_r0 = -1;
static uint32_t s_key = 0xFFFFFFFFu;

/* What the game loads for a screen, rebuilt from the ROM (its full load $1978: $1BD8 common part, then
   $1A3D / $1A80 the area's part). The game itself only has the set of the area it is in, so drawing
   the neighbours with the live tables gave other areas' screens scrambled tiles (user report: K6).
     common   : VRAM $2000 <- asset $D0 (4-plane RLE, $038F); metatiles $CC00 <- asset $84 ($160 bytes);
                terrain $D000 <- asset $8B (RLE $1AAA), then $D000 = $80;
     $C84C = 1 (screens whose layout id is >= $53, $1BC5): VRAM $2780 / $28E0 / $2AE0 <- asset $8D
                ($60 / $100 / $80 bytes), metatiles $CCD8 / $CD14 <- asset $8E ($18 / $20), terrain <- $8F;
     area id  : palette asset $1A77[id], VRAM $2E00 <- asset $1A77[id] + 1 (4-plane RLE), metatiles
                $CD60 <- asset $1ABB[id] ($2A0), terrain $D058 <- (asset $8C)[id] (RLE).
   Area id of a screen = $15E48[s] low nibble; 0 = keep the loaded set when walking in ($1A1D), the
   screen to the left's on a full load ($1A3D): the panel uses the full-load rule. */
typedef struct { uint8_t vram[0x2000]; uint8_t mt[0x400]; uint8_t ter[0x100]; bool ok; } Set;
static Set s_set[16][2];

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
    /* CRAM = an area's palette (not a cave's, not mid-fade). Not "$C819's": walking into a screen of
       area id 0 sets $C819 = 0 and keeps the loaded set (and palette) of the area it came from. */
    for (int id = 1; id <= 8; id++) { const uint8_t *p = palette(id); if (p && memcmp(p, g_vdp.cram, 16) == 0) return true; }
    return false;
}
bool gv_panel_overworld_shown(void) { if (!s_rom) s_rom = glue_rom_image(&s_rom_n); return s_rom && s_rom_n >= 0x20000 && on_overworld(); }

/* $038F: 4 bit planes one after another, each RLE (n < $80: n literal bytes, n >= $80: the next byte
   (n & $7F) times (0 = 256), 0 = end of the plane), written to every 4th byte from plane p */
static void unpack4(int o, uint8_t *dst, int n)
{
    if (o < 0) return;
    const uint8_t *p = s_rom + o, *end = s_rom + s_rom_n;
    for (int pl = 0; pl < 4; pl++) {
        int d = pl;
        for (;;) {
            if (p >= end) return;
            int c = *p++;
            if (!c) break;
            if (c & 0x80) {
                int k = c & 0x7F; if (!k) k = 256;
                if (p >= end) return;
                uint8_t v = *p++;
                while (k--) { if (d < n) dst[d] = v; d += 4; }
            } else
                while (c--) { if (p >= end) return; if (d < n) dst[d] = *p; p++; d += 4; }
        }
    }
}
/* $1AAA: records (last low address byte, value), 0 = end; fills from e up to that byte */
static void rle_ter(size_t o, uint8_t *ter, int e)
{
    while (o + 1 < s_rom_n && s_rom[o]) {
        int last = s_rom[o]; uint8_t v = s_rom[o + 1]; o += 2;
        for (int k = 0; k < 256; k++) { int old = e; ter[e] = v; e = (e + 1) & 255; if (old == last) break; }
    }
}
static void copy_rom(uint8_t *dst, int o, int n) { if (o >= 0 && (size_t)o + (size_t)n <= s_rom_n) memcpy(dst, s_rom + o, (size_t)n); }
static const Set *area_set(int id, int fl)
{
    Set *S = &s_set[id & 15][fl & 1];
    if (S->ok) return S;
    memset(S, 0, sizeof *S);
    unpack4(asset(0xD0), S->vram, 0x2000);
    copy_rom(S->mt, asset(0x84), 0x160);
    int o = asset(0x8B); if (o >= 0) rle_ter((size_t)o, S->ter, 0);
    S->ter[0] = 0x80;
    if (fl) {
        o = asset(0x8D);
        if (o >= 0) { copy_rom(S->vram + 0x780, o, 0x60); copy_rom(S->vram + 0x8E0, o + 0x60, 0x100); copy_rom(S->vram + 0xAE0, o + 0x160, 0x80); }
        o = asset(0x8E);
        if (o >= 0) { copy_rom(S->mt + 0xD8, o, 0x18); copy_rom(S->mt + 0x114, o + 0x18, 0x20); }
        o = asset(0x8F); if (o >= 0) rle_ter((size_t)o, S->ter, 0);
    }
    if (id <= 8) {                                  /* the tables $1A77 / $1ABB have 9 entries */
        int pc = s_rom[0x1A77 + id];
        unpack4(asset(pc + 1), S->vram + 0xE00, 0x2000 - 0xE00);
        copy_rom(S->mt + 0x160, asset(s_rom[0x1ABB + id]), 0x2A0);
        o = asset(0x8C);
        if (o >= 0) {
            int p = s_rom[o + 2 * id] | s_rom[o + 2 * id + 1] << 8;
            if (p >= 0x8000 && p < 0xC000) rle_ter((size_t)8 * 0x4000 + (size_t)(p - 0x8000), S->ter, 0x58);
        }
    }
    S->ok = true;
    return S;
}
static int scr_flag(int s) { return s_rom[0x15947 + s_rom[0x15890 + s] * 7] >= 0x53; }   /* $C84C ($1BC5) */

/* The cave entrance of screen s as the game draws it once the entrance is open: the room record's
   byte 6 (metatile) at its byte 2 (cell = row * 15 + col); hidden caves have a bush / rock / tree
   there in the layout until they are revealed ($1E3B), and a revisit hides them again. Out: the
   16 x 16 metatile as SMS colour bytes (the screen's area set and palette, the same rules as the
   panel) and its cell. False when the screen has no entrance (L14) or no ROM. MAP FIXES (screen H4's
   entrance $94 -> $2B): the original build patches the ROM image itself; Golvellius Plus does it in
   its own code at $193D when its option (RAM $C0D2) is on. */
bool gv_panel_entrance(int s, int *cell, uint8_t px[256])
{
    if (!s_rom) s_rom = glue_rom_image(&s_rom_n);
    if (!s_rom || s_rom_n < 0x20000 || s < 0 || s >= NSCR) return false;
    int room = s_rom[0x15890 + s];
    const uint8_t *r = s_rom + 0x15947 + room * 7;
    uint8_t m = r[6];
    if (room == 175 && m == 0x94 && gv_is_plus() && sms_read8(0xC0D2)) m = 0x2B;
    if (!m || r[2] >= 180) return false;
    int id = pal_id(s);
    const Set *S = area_set(id, scr_flag(s));
    const uint8_t *pal = palette(id);
    for (int q = 0; q < 4; q++) {
        const uint8_t *tile = S->vram + S->mt[m * 4 + q] * 32;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int b = 7 - x, row = y * 4;
                int c = ((tile[row] >> b) & 1) | ((tile[row + 1] >> b) & 1) << 1 | ((tile[row + 2] >> b) & 1) << 2 | ((tile[row + 3] >> b) & 1) << 3;
                px[((q >> 1) * 8 + y) * 16 + (q & 1) * 8 + x] = (pal ? pal[c] : g_vdp.cram[c]) & 0x3F;
            }
    }
    *cell = r[2];
    return true;
}

/* the cells of screen s as the game shows them: the hero's screen live ($CA00: opened caves, cut
   bushes), others from the ROM + the progress patches of $1AC4 (high nibble of $15E48[s] -> table
   $1B03: a mask of $C845, then (cell, metatile) pairs up to $FF; 4 screens, e.g. bridges). prog = the
   $C845 to use (the live one, or a save slot's) */
static void cells_p(int s, int hero, uint8_t prog, uint8_t out[180])
{
    if (s == hero) { for (int i = 0; i < 180; i++) out[i] = sms_read8((uint16_t)(0xCA00 + i)); return; }
    const uint8_t *l = layout(s);
    if (!l) { memset(out, 0xFF, 180); return; }
    memcpy(out, l, 180);
    int t = asset(0x5D), hi = t >= 0 ? s_rom[t + s] >> 4 : 0;
    if (!hi || hi > 4) return;                      /* the table has 4 entries */
    int p = s_rom[0x1B03 + (hi - 1) * 2] | s_rom[0x1B03 + (hi - 1) * 2 + 1] << 8;
    if (p >= 0x8000 || !(s_rom[p] & prog)) return;
    for (p++; (size_t)p + 1 < s_rom_n && s_rom[p] != 0xFF; p += 2) if (s_rom[p] < 180) out[s_rom[p]] = s_rom[p + 1];
}
static void cells(int s, int hero, uint8_t out[180]) { cells_p(s, hero, sms_read8(0xC845), out); }
/* terrain attribute of a metatile on screen s (the hero's: the live table) */
static uint8_t terrain(int s, int hero, uint8_t m)
{
    if (s == hero) return sms_read8((uint16_t)(0xD000 + m));
    return area_set(pal_id(s), scr_flag(s))->ter[m];
}

/* Flood fill from the hero's cell over the 3 x 3 window plus one ring of screens around it (routes
   that leave the window: user report L11, K12 / M12 stayed black), clipped to the grid. A cell is
   passable when its attribute is 1 .. $C817. */
#define EXT (WIN + 2)
static void reach(int hero)
{
    enum { GW = 15 * EXT, GH = 12 * EXT };
    static uint8_t pass[GW * GH], seen[GW * GH]; static int q[GW * GH];
    uint8_t lvl = sms_read8(0xC817);
    int c0 = s_c0 - 1, r0 = s_r0 - 1;               /* top-left screen of the extended window */
    memset(pass, 0, sizeof pass);
    for (int wy = 0; wy < EXT; wy++)
        for (int wx = 0; wx < EXT; wx++) {
            int c = c0 + wx, r = r0 + wy;
            if (c < 0 || r < 0 || c >= COLS || r >= ROWS) continue;
            int s = r * COLS + c; uint8_t cl[180];
            cells(s, hero, cl);
            for (int cell = 0; cell < 180; cell++) {
                uint8_t a = terrain(s, hero, cl[cell]);
                pass[(wy * 12 + cell / 15) * GW + wx * 15 + cell % 15] = a >= 1 && a <= lvl;   /* 0 = openings (into a cave / house) */
            }
        }
    memset(seen, 0, sizeof seen); memset(s_reach, 0, sizeof s_reach);
    int hr = (sms_read8(0xC101) - 8) / 16, hc = (sms_read8(0xC102) - 16) / 16;
    if (hr < 0) hr = 0; if (hr > 11) hr = 11; if (hc < 0) hc = 0; if (hc > 14) hc = 14;
    int start = ((hero / COLS - r0) * 12 + hr) * GW + (hero % COLS - c0) * 15 + hc;
    int qn = 0; q[qn++] = start; seen[start] = 1;
    for (int i = 0; i < qn; i++) {
        int p = q[i], x = p % GW, y = p / GW;
        int wx = x / 15 - 1, wy = y / 12 - 1;       /* window coordinates */
        if (wx >= 0 && wy >= 0 && wx < WIN && wy < WIN) s_reach[wy * WIN + wx] = 1;
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

/* screen s into img (row length stride) at ox, oy. A save slot's view: hero = -1, prog = its $C845,
   ent = its known caves (NULL = the live game's, gv_cave_known) */
static void draw_screen_ex(uint32_t *img, int stride, int s, int ox, int oy, int hero, uint8_t prog, const uint8_t *ent)
{
    const uint8_t *tiles, *mt; uint32_t col[16];
    if (s == hero) {                                /* what the game has loaded (on_overworld() holds) */
        tiles = g_vdp.vram + 0x2000; mt = NULL;
        for (int i = 0; i < 16; i++) col[i] = argb(g_vdp.cram[i]);
    } else {
        int id = pal_id(s);
        const Set *S = area_set(id, scr_flag(s));
        const uint8_t *pal = palette(id);
        tiles = S->vram; mt = S->mt;
        for (int i = 0; i < 16; i++) col[i] = argb(pal ? pal[i] : g_vdp.cram[i]);
    }
    uint8_t cl[180]; cells_p(s, hero, prog, cl);
    if (s != hero && (ent ? ent[s] != 0 : gv_cave_known(s))) {    /* a known cave: its entrance, also when hidden (as the MAP tab) */
        int room = s_rom[0x15890 + s]; const uint8_t *r = s_rom + 0x15947 + room * 7;
        uint8_t m = r[6];
        if (room == 175 && m == 0x94 && gv_is_plus() && sms_read8(0xC0D2)) m = 0x2B;
        if (m && r[2] < 180) cl[r[2]] = m;
    }
    for (int cell = 0; cell < 180; cell++) {
        uint8_t m = cl[cell];
        for (int q = 0; q < 4; q++) {
            int t = mt ? mt[m * 4 + q] : sms_read8((uint16_t)(0xCC00 + m * 4 + q));      /* tile 256 + t */
            int px = ox + (cell % 15) * 16 + (q & 1) * 8, py = oy + (cell / 15) * 16 + (q >> 1) * 8;
            for (int r = 0; r < 8; r++) {
                uint32_t *dst = img + (size_t)(py + r) * stride + px;
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
static void draw_screen(int s, int ox, int oy, int hero) { draw_screen_ex(s_img, WIN * SW, s, ox, oy, hero, sms_read8(0xC845), NULL); }
static void fill_screen_ex(uint32_t *img, int ox, int oy, uint32_t c)
{
    for (int y = 0; y < SH; y++) for (int x = 0; x < SW; x++) img[(size_t)(oy + y) * (WIN * SW) + ox + x] = c;
}
static void fill_screen(int ox, int oy, uint32_t c) { fill_screen_ex(s_img, ox, oy, c); }

/* re-render when the window, the hero's cell / screen data, the terrain level, the progress flags
   or the set of visited screens change */
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
    h = (h ^ sms_read8(0xC845)) * 16777619u;
    for (int i = 0; i < WIN * WIN; i++) h = (h ^ (uint32_t)gv_map_seen((r0 + i / WIN) * COLS + c0 + i % WIN)) * 16777619u;
    key ^= h;
    if (key == s_key && c0 == s_c0 && r0 == s_r0) return;
    s_key = key; s_c0 = c0; s_r0 = r0;
    reach(hero);
    for (int wy = 0; wy < WIN; wy++)
        for (int wx = 0; wx < WIN; wx++) {
            int s = (r0 + wy) * COLS + c0 + wx;
            /* reachable now, or visited before (the hero has seen it: e.g. across a river crossed
               before, or by a route the flood fill does not see) */
            if (s_reach[wy * WIN + wx] || gv_map_seen(s)) draw_screen(s, wx * SW, wy * SH, hero);
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

/* ---- save-slot preview (Golvellius Plus) ----
 * While one of the ROM's slot lists is open, the panel shows the slot under its cursor (SaveCur, RAM
 * $C0AB) from battery RAM instead of the game: its name, play time, crystals, gold / max / power, the
 * 3 x 3 screens around where it was saved (the hero there) and the WORLD overview, both with that
 * slot's own explored screens and known caves. The two lists (golvellius_plus.asm):
 *   SAVE page (title -> CONTINUE -> SAVE, AUTO + slots 1-5): a page on name table $3000 (VDP reg 2
 *     $FD) in the title's high tile set, "SAVE" at row 1, column 14 (the OPTIONS page has "OPTIONS").
 *     Loading halves the gold (the game's LAST GAME CONTINUE, asset $79 = bank 7 $8014: the BCD gold
 *     $C83F-$C841 halved digit by digit = rounded down) unless KEEP GOLD (OptVals+5 = RAM $C0D6) is on:
 *     the gold line shows what a load gives, with the saved amount beside it.
 *   SAVE GAME (Pause on the overworld + button 1, and after Game Over's password; slots 1-5): the game's
 *     name table $3800 cleared, "SAVE GAME" on row 1 at a column that follows the scroll, in the low
 *     font at a tile offset ($C0 on the Pause screen, 0 after Game Over): found as the text at any
 *     column with one constant offset. Shows the saved gold (it is what would be overwritten).
 * Slot n (0 = AUTO): record $8100 + 64 n (+0 $A5, +1 screen, +2 hero y, +3 x, +4 play time: frames,
 * seconds (both binary), minutes, hours (both BCD), +10 $C839-$C84B, +63 sum of +0..+62 xor $5A), map block $8300 + 64 n
 * (+0 $A5, +1 screens seen 23 bytes, +24 caves entered 23, +47 sum of +0..+46 xor $5A; none for a slot
 * saved before the ROM had its map). The PC build's own maps of the slot (gv_slots.c:
 * <save>.save<n>.gvmap / .gvcave) are added: their visited screens / entered caves count too. The
 * screens are drawn from the ROM like the panel's neighbours (the slot's progress flags $C845), so a
 * slot saved before any map existed still shows its own screen. */
extern const uint8_t *glue_sram_image(size_t *size);
extern void gv_slots_plus_file(int n, const char *ext, char *out, size_t sz);
extern int gv_cave_info_all;
#define OCW (SW / 4)                                /* overview cell, as gv_map.c */
#define OCH (SH / 4)
static struct {
    int slot, list; uint32_t key, flags; bool fresh;
    uint8_t rec[64], seen[NSCR], ent[NSCR];
    int c0, r0, nseen; uint32_t gen, ov_gen;
} s_pv = { -1, 0, 0, 0, false, { 0 }, { 0 }, { 0 }, 0, 0, 0, 1, 1 };
static uint32_t s_pv_img[WIN * SW * WIN * SH];
static uint32_t s_pv_ov[COLS * OCW * ROWS * OCH];

static bool nt_is(int nt, int row, int col, const char *t, int ofs, int hi)
{
    for (int i = 0; t[i]; i++) {
        int c = t[i] == ' ' ? 0x40 : t[i];
        int a = nt + (row * 32 + ((col + i) & 31)) * 2;
        if (g_vdp.vram[a] != (uint8_t)(c + ofs) || (hi >= 0 && (g_vdp.vram[a + 1] & 1) != hi)) return false;
    }
    return true;
}
/* 1 = the SAVE page, 2 = SAVE GAME, 0 = neither (or not Golvellius Plus) */
static int slot_list(void)
{
    if (!gv_is_plus()) return 0;
    int nt = (g_vdp.reg[2] & 0x0E) << 10;
    if (nt == 0x3000) return nt_is(nt, 1, 14, "SAVE", 0, 1) ? 1 : 0;
    if (nt != 0x3800) return 0;
    for (int col = 0; col < 32; col++) {
        int ofs = (g_vdp.vram[nt + (32 + col) * 2] - 'S') & 0xFF;
        if (nt_is(nt, 1, col, "SAVE GAME", ofs, -1)) return 2;
    }
    return 0;
}
static uint32_t fnv(uint32_t h, const uint8_t *p, size_t n) { while (n--) h = (h ^ *p++) * 16777619u; return h; }
static void pv_files(int slot)                      /* the PC build's maps of the slot: seen / entered flags */
{
    char p[1100]; FILE *f; uint8_t v[NSCR]; char mg[4];
    gv_slots_plus_file(slot, ".gvmap", p, sizeof p);
    if ((f = fopen(p, "rb"))) {                     /* "GVM1/2/3" + the visited flags first (GVM2: 183, the first 182 = screens) */
        if (fread(mg, 1, 4, f) == 4 && !memcmp(mg, "GVM", 3) && mg[3] >= '1' && mg[3] <= '3' && fread(v, 1, NSCR, f) == NSCR)
            for (int i = 0; i < NSCR; i++) s_pv.seen[i] |= v[i] != 0;
        fclose(f);
    }
    gv_slots_plus_file(slot, ".gvcave", p, sizeof p);
    if ((f = fopen(p, "rb"))) {                     /* "GVC2": the entered flags are the file's last 182 bytes */
        if (fread(mg, 1, 4, f) == 4 && !memcmp(mg, "GVC2", 4) && !fseek(f, -(long)NSCR, SEEK_END) && fread(v, 1, NSCR, f) == NSCR)
            for (int i = 0; i < NSCR; i++) s_pv.ent[i] |= v[i] != 0;
        fclose(f);
    }
}
/* the slot's pictures: drawn again when the slot, its record / map block or an option that changes the
   drawing changed, or its map files say something new (read again every 30 frames: gv_slots.c copies
   the maps to a slot when it sees the ROM's save, which may be a frame after the panel saw it) */
static void pv_update(int list, int slot, const uint8_t *rec, const uint8_t *mb)
{
    static unsigned tick;
    uint8_t opt[2] = { sms_read8(0xC0D2), (uint8_t)gv_cave_info_all };
    uint32_t key = fnv(fnv(fnv(2166136261u ^ (uint32_t)slot, rec, 64), mb, 48), opt, 2);
    bool same = slot == s_pv.slot && key == s_pv.key && s_pv.fresh;
    if (same && ++tick % 30) return;
    memset(s_pv.seen, 0, sizeof s_pv.seen); memset(s_pv.ent, 0, sizeof s_pv.ent);
    uint8_t s = 0; for (int i = 0; i < 47; i++) s = (uint8_t)(s + mb[i]);
    if (mb[0] == 0xA5 && (uint8_t)(s ^ 0x5A) == mb[47])
        for (int i = 0; i < NSCR; i++) { s_pv.seen[i] = mb[1 + i / 8] >> (i & 7) & 1; s_pv.ent[i] = mb[24 + i / 8] >> (i & 7) & 1; }
    pv_files(slot);
    int scr = rec[1] < NSCR ? rec[1] : 180;
    s_pv.seen[scr] = 1;                             /* the hero was there */
    for (int i = 0; i < NSCR; i++)                  /* as gv_cave_known(): CAVE INFO = all, or L14's start house (cave id 0) once seen */
        if (gv_cave_info_all || (s_pv.seen[i] && s_rom[0x15947 + s_rom[0x15890 + i] * 7 + 3] == 0)) s_pv.ent[i] = 1;
    uint32_t flags = fnv(fnv(2166136261u, s_pv.seen, NSCR), s_pv.ent, NSCR);
    if (same && flags == s_pv.flags) return;
    s_pv.slot = slot; s_pv.list = list; s_pv.key = key; s_pv.flags = flags; s_pv.fresh = true;
    memcpy(s_pv.rec, rec, 64);
    s_pv.nseen = 0; for (int i = 0; i < NSCR; i++) s_pv.nseen += s_pv.seen[i];
    uint8_t prog = rec[10 + (0xC845 - 0xC839)];
    int c0 = scr % COLS - 1, r0 = scr / COLS - 1;
    if (c0 < 0) c0 = 0; if (c0 > COLS - WIN) c0 = COLS - WIN;
    if (r0 < 0) r0 = 0; if (r0 > ROWS - WIN) r0 = ROWS - WIN;
    s_pv.c0 = c0; s_pv.r0 = r0;
    for (int wy = 0; wy < WIN; wy++)
        for (int wx = 0; wx < WIN; wx++) {
            int k = (r0 + wy) * COLS + c0 + wx;
            if (s_pv.seen[k]) draw_screen_ex(s_pv_img, WIN * SW, k, wx * SW, wy * SH, -1, prog, s_pv.ent);
            else fill_screen_ex(s_pv_img, wx * SW, wy * SH, 0xFF000000u);
        }
    static uint32_t one[SW * SH];                   /* the overview: each seen screen at 1/4 (4 x 4 average) */
    for (int k = 0; k < NSCR; k++) {
        int cx = (k % COLS) * OCW, cy = (k / COLS) * OCH;
        if (s_pv.seen[k]) draw_screen_ex(one, SW, k, 0, 0, -1, prog, s_pv.ent);
        for (int y = 0; y < OCH; y++)
            for (int x = 0; x < OCW; x++) {
                uint32_t *d = &s_pv_ov[(size_t)(cy + y) * (COLS * OCW) + cx + x];
                if (!s_pv.seen[k]) { *d = 0; continue; }
                unsigned r = 0, g = 0, bl = 0;
                for (int dy = 0; dy < 4; dy++)
                    for (int dx = 0; dx < 4; dx++) { uint32_t c = one[(y * 4 + dy) * SW + x * 4 + dx]; r += c >> 16 & 255; g += c >> 8 & 255; bl += c & 255; }
                *d = 0xFF000000u | (r / 16) << 16 | (g / 16) << 8 | (bl / 16);
            }
    }
    s_pv.gen++; s_pv.ov_gen++;
}
static long bcd_n(const uint8_t *p, int n) { long v = 0; for (int i = 0; i < n; i++) v = v * 100 + bcd(p[i]); return v; }

/* true = a slot list is open: the panel shows its slot */
static bool slot_preview(uint32_t *b, int w, int h)
{
    int list = slot_list();
    size_t n = 0; const uint8_t *sram = glue_sram_image(&n);
    int slot = sms_read8(0xC0AB);
    {   static int last = -1; int now = list ? list * 8 + slot : 0;      /* test: GV_PANEL_LOG=1 */
        if (now != last && getenv("GV_PANEL_LOG")) fprintf(stderr, "[gv_panel] slot preview: %s slot %d\n", !list ? "off" : list == 1 ? "SAVE page" : "SAVE GAME", slot);
        last = now; }
    if (!list || !sram || n < 0x500 || slot > 5 || (list == 2 && slot < 1)) { s_pv.fresh = false; return false; }
    if (!s_rom) s_rom = glue_rom_image(&s_rom_n);
    if (!s_rom || s_rom_n < 0x40000) return false;
    const uint8_t *rec = sram + 0x100 + 64 * slot, *mb = sram + 0x300 + 64 * slot;
    uint8_t sum = 0; for (int i = 0; i < 63; i++) sum = (uint8_t)(sum + rec[i]);
    char t[48], nm[8];
    if (slot) snprintf(nm, sizeof nm, "SLOT %d", slot); else snprintf(nm, sizeof nm, "AUTO");
    for (int i = 0; i < w * h; i++) b[i] = BG;
    if (rec[0] != 0xA5 || (uint8_t)(sum ^ 0x5A) != rec[63]) {          /* empty (or damaged: the ROM shows EMPTY too) */
        s_pv.fresh = false;
        box(b, w, h, 8, 8, w - 8, h - 8);
        hud_text_scaled(b, w, h, (w - 12 * (int)strlen(nm)) / 2, h / 2 - 30, nm, CYAN, 2);
        hud_text_scaled(b, w, h, (w - 5 * 12) / 2, h / 2 - 6, "EMPTY", GREY, 2);
        return true;
    }
    if (list != s_pv.list) s_pv.fresh = false;      /* a list opened again: read the slot's map files again */
    pv_update(list, slot, rec, mb);
    const uint8_t *g = s_pv.rec + 10;               /* $C839.. */
    int scr = s_pv.rec[1] < NSCR ? s_pv.rec[1] : 180;
    int y = 8;

    /* name, play time, crystals */
    box(b, w, h, 8, y, w - 8, y + 70);
    hud_text_scaled(b, w, h, 16, y + 8, nm, CYAN, 2);
    snprintf(t, sizeof t, "%d:%02d:%02d", bcd(s_pv.rec[7]), bcd(s_pv.rec[6]), s_pv.rec[5] % 60);   /* seconds binary (PlayTick) */
    hud_text_scaled(b, w, h, w - 16 - 12 * (int)strlen(t), y + 8, t, WHITE, 2);
    hud_text(b, w, h, w - 16 - 6 * 9, y + 26, "PLAY TIME", GREY);
    int cr = 0; for (int i = 0; i < 7; i++) cr += g[0xC847 - 0xC839] >> i & 1;
    hud_text_scaled(b, w, h, 16, y + 44, "CRYSTALS", CYAN, 2);
    snprintf(t, sizeof t, "%d/7", cr); hud_text_scaled(b, w, h, w - 16 - 12 * (int)strlen(t), y + 44, t, WHITE, 2);
    y += 78;

    /* where it was saved: area, screen (+ what its cave holds, when known), the 3 x 3 screens */
    const char *area = gv_area_name(scr);
    if (area[0]) snprintf(t, sizeof t, "%s", area); else snprintf(t, sizeof t, "SCREEN %c%d", 'A' + scr % COLS, scr / COLS + 1);
    int bx0 = 8, bx1 = w - 8, mw = bx1 - bx0 - 8, mh = mw * (WIN * SH) / (WIN * SW);
    box(b, w, h, bx0, y, bx1, y + 36 + mh + 8);
    hud_text_scaled(b, w, h, 16, y + 6, t, CYAN, (int)strlen(t) * 12 <= w - 32 ? 2 : 1);
    {   char cl[48], line[16]; int done = 0;
        snprintf(line, sizeof line, "%c%d", 'A' + scr % COLS, scr / COLS + 1);
        hud_text(b, w, h, 16, y + 24, line, WHITE);
        if (s_pv.ent[scr] && gv_cave_label(scr, cl, sizeof cl, &done))      /* (bought / taken: the live game's flags, not shown) */
            hud_text(b, w, h, 16 + 6 * 4, y + 24, cl, gv_pw_cave(scr) ? 0xFFFF40FFu : GOLDC);
        else hud_text(b, w, h, 16 + 6 * 4, y + 24, "?", GREY);
    }
    float mx = (float)bx0 + 4, my = (float)y + 36, sc = (float)mw / (WIN * SW);
    if (hud_hires()) hud_image(HUD_SPACE_PANEL, mx, my, (float)mw, (float)mh, s_pv_img, WIN * SW, WIN * SH, 0, 0, WIN * SW, WIN * SH, false, s_pv.gen);
    else
        for (int yy = 0; yy < mh; yy++)
            for (int xx = 0; xx < mw; xx++)
                b[(size_t)((int)my + yy) * w + (int)mx + xx] = s_pv_img[(size_t)(yy * WIN * SH / mh) * (WIN * SW) + xx * WIN * SW / mw];
    for (int i = 1; i < WIN; i++) {
        hud_rect(HUD_SPACE_PANEL, mx + i * SW * sc, my, 0.5f, (float)mh, 0x50FFFFFFu);
        hud_rect(HUD_SPACE_PANEL, mx, my + i * SH * sc, (float)mw, 0.5f, 0x50FFFFFFu);
    }
    static unsigned blink; blink++;
    if ((blink / 20) % 3) {                         /* the hero where the slot starts (y, x as $C101 / $C102) */
        float hx = (float)((scr % COLS - s_pv.c0) * SW + s_pv.rec[3] - 8), hy = (float)((scr / COLS - s_pv.r0) * SH + s_pv.rec[2]);
        hud_marker(HUD_SPACE_PANEL, mx + hx * sc, my + hy * sc, 3.f, 1.f, 0xFFFFE040u, 0xFF000000u);
    }
    y += 36 + mh + 8 + 8;

    /* gold (the SAVE page: what a load gives) + power */
    box(b, w, h, 8, y, w - 8, y + 92);
    long saved = bcd_n(g + (0xC83F - 0xC839), 3), gold = saved;
    if (list == 1 && !sms_read8(0xC0D6)) gold = saved / 2;           /* KEEP GOLD off: the continue halves it */
    long gmax = bcd_n(g + (0xC842 - 0xC839), 3);
    hud_text_scaled(b, w, h, 16, y + 8, "GOLD", GOLDC, 2);
    snprintf(t, sizeof t, "%ld", gold * 10); hud_text_scaled(b, w, h, w - 16 - 12 * (int)strlen(t), y + 8, t, WHITE, 2);
    if (gold != saved) { snprintf(t, sizeof t, "(SAVED %ld)", saved * 10); hud_text(b, w, h, 16 + 4 * 12 + 6, y + 12, t, GREY); }
    hud_text_scaled(b, w, h, 16, y + 28, "MAX", GREY, 2);
    snprintf(t, sizeof t, "%ld", gmax * 10); hud_text_scaled(b, w, h, w - 16 - 12 * (int)strlen(t), y + 28, t, GREY, 2);
    int pmax = bcd(g[0]);                           /* the slot keeps only the maximum: a continue starts with full power */
    hud_text_scaled(b, w, h, 16, y + 50, "POWER", POWC, 2);
    snprintf(t, sizeof t, "%d/%d", pmax, pmax); hud_text_scaled(b, w, h, w - 16 - 12 * (int)strlen(t), y + 50, t, WHITE, 2);
    hud_fill(b, w, h, 16, y + 72, w - 16, y + 80, pmax > 0 ? POWC : 0xFF000000u, false);
    y += 100;

    /* WORLD: the slot's explored screens, the 3 x 3 window framed, the hero's screen */
    int ow = COLS * OCW, oh = ROWS * OCH, avail = h - 8 - y - 24 - 8;
    float ms = (float)(w - 24) / ow; if (oh * ms > avail) ms = (float)avail / oh;
    if (ms <= 0 || avail <= 40) return true;
    float dw = ow * ms, dh = oh * ms;
    box(b, w, h, 8, y, w - 8, y + 24 + (int)dh + 8);
    hud_text_scaled(b, w, h, 16, y + 6, "WORLD", CYAN, 2);
    snprintf(t, sizeof t, "%d/%d", s_pv.nseen, NSCR); hud_text(b, w, h, w - 16 - 6 * (int)strlen(t), y + 10, t, GREY);
    float ox = 8 + ((w - 16) - dw) / 2, oy = (float)y + 24, cw = dw / COLS, ch = dh / ROWS;
    hud_fill(b, w, h, (int)ox, (int)oy, (int)(ox + dw), (int)(oy + dh), 0xFF000000u, false);
    if (hud_hires()) {
        hud_image_alpha(HUD_SPACE_PANEL, ox, oy, dw, dh, s_pv_ov, ow, oh, s_pv.ov_gen);
        for (int i = 0; i < NSCR; i++)
            if (s_pv.seen[i] && gv_pw_cave(i))
                hud_rect(HUD_SPACE_PANEL, ox + (i % COLS + 1) * cw - cw * 0.35f, oy + (i / COLS) * ch, cw * 0.35f, cw * 0.35f, 0xFFFF40FFu);
        float fx = ox + s_pv.c0 * cw, fy = oy + s_pv.r0 * ch, lw = 1.f;
        hud_rect(HUD_SPACE_PANEL, fx, fy, WIN * cw, lw, CYAN); hud_rect(HUD_SPACE_PANEL, fx, fy + WIN * ch - lw, WIN * cw, lw, CYAN);
        hud_rect(HUD_SPACE_PANEL, fx, fy, lw, WIN * ch, CYAN); hud_rect(HUD_SPACE_PANEL, fx + WIN * cw - lw, fy, lw, WIN * ch, CYAN);
        hud_marker(HUD_SPACE_PANEL, ox + (scr % COLS + 0.5f) * cw, oy + (scr / COLS + 0.5f) * ch, 2.f, 1.f, 0xFFFFE040u, 0xFF000000u);
    }
    return true;
}

extern bool gv_cave_panel(uint32_t *b, int w, int h);
void gv_panel_draw(uint32_t *b, int w, int h)
{
    if (slot_preview(b, w, h)) return;              /* Golvellius Plus: a save slot list is open */
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
    char t[48];
    int y = 8;
    /* logo / title */
    box(b, w, h, 8, y, w - 8, y + 70);
    if (hud_logo(NULL, NULL)) hud_draw_logo(b, w, h, HUD_SPACE_PANEL, 16.f, (float)y + 6, (float)(w - 32), 58.f, true);
    else hud_text_scaled(b, w, h, (w - 10 * 12) / 2, y + 28, "GOLVELLIUS", CYAN, 2);
    y += 78;

    /* surroundings */
    /* area name, then the screen + what its cave holds (gv_info.c; option CAVE INFO) */
    const char *area = gv_area_name(hero);
    if (area[0]) snprintf(t, sizeof t, "%s", area); else snprintf(t, sizeof t, "SCREEN %c%d", 'A' + hero % COLS, hero / COLS + 1);
    int bx0 = 8, bx1 = w - 8, mw = bx1 - bx0 - 8, mh = mw * (WIN * SH) / (WIN * SW);
    box(b, w, h, bx0, y, bx1, y + 36 + mh + 8);
    hud_text_scaled(b, w, h, 16, y + 6, t, CYAN, (int)strlen(t) * 12 <= w - 32 ? 2 : 1);
    {   char cl[48], line[64]; int done = 0;
        snprintf(line, sizeof line, "%c%d", 'A' + hero % COLS, hero / COLS + 1);
        hud_text(b, w, h, 16, y + 24, line, WHITE);
        if (gv_cave_known(hero) && gv_cave_label(hero, cl, sizeof cl, &done)) {
            hud_text(b, w, h, 16 + 6 * 4, y + 24, cl, gv_pw_cave(hero) ? 0xFFFF40FFu : GOLDC);
            if (done) hud_text(b, w, h, 16 + 6 * (5 + (int)strlen(cl)), y + 24, done == 2 ? "TAKEN" : "BOUGHT", 0xFF60E060u);
        } else hud_text(b, w, h, 16 + 6 * 4, y + 24, "?", GREY);      /* not been inside yet */
    }
    float mx = (float)bx0 + 4, my = (float)y + 36;
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
    y += 36 + mh + 8 + 8;

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
    uint32_t g0 = s_gen;
    if (on_overworld()) render(hero);
    if (s_gen != g0 && getenv("GV_PANEL_LOG")) {
        fprintf(stderr, "[gv_panel] frame %llu render screen %d hero y %d x %d mode %d reach", (unsigned long long)frame, hero, sms_read8(0xC101), sms_read8(0xC102), sms_read8(0xC802));
        for (int i = 0; i < WIN * WIN; i++) fprintf(stderr, " %d", s_reach[i]);
        fprintf(stderr, "\n");
    }
    /* GV_PANEL_SELFTEST=1: the hero's screen drawn from the ROM-built set (as for a neighbour) must
       equal the live drawing (the game's own tables): prints the number of differing pixels */
    if (getenv("GV_PANEL_SELFTEST") && on_overworld() && s_c0 >= 0) {
        static int last = -1, still = 0;
        still = hero == last ? still + 1 : 0; last = hero;
        if (still == 60) {
            int ox = (hero % COLS - s_c0) * SW, oy = (hero / COLS - s_r0) * SH;
            static uint32_t live[SW * SH];
            draw_screen(hero, ox, oy, hero);
            for (int y = 0; y < SH; y++) memcpy(live + y * SW, s_img + (size_t)(oy + y) * (WIN * SW) + ox, SW * 4);
            draw_screen(hero, ox, oy, -1);
            int diff = 0, cdiff = 0; uint8_t cl[180]; cells(hero, -1, cl);
            for (int y = 0; y < SH; y++) for (int x = 0; x < SW; x++) diff += live[y * SW + x] != s_img[(size_t)(oy + y) * (WIN * SW) + ox + x];
            for (int i = 0; i < 180; i++) cdiff += cl[i] != sms_read8((uint16_t)(0xCA00 + i));
            fprintf(stderr, "[gv_panel] selftest screen %c%d area %d flag %d loaded C819 %d C84C %d: %d pixels differ (%d cells differ from $CA00)\n",
                    'A' + hero % COLS, hero / COLS + 1, pal_id(hero), scr_flag(hero), sms_read8(0xC819), sms_read8(0xC84C), diff, cdiff);
            draw_screen(hero, ox, oy, hero);
        }
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
