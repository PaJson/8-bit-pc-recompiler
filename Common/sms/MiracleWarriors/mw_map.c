/*
 * mw_map.c - Miracle Warriors maps: HUD MAP tab + side panel (found in the ROM 2026-11-22).
 *
 * The game shows a 5 x 5 cell window of the map at the top right (name table rows 0-9, columns
 * 21-30; one cell = 2 x 2 tiles of 8 px). All maps are plain byte arrays in bank 2:
 *   overworld   (map mode $C440 = 1): $8000, 98 x 76 cells (the playable part x 2..95, y 2..73),
 *               party cell = $C441 (16-bit index, rows of 98), cell under the party $C443;
 *               cell -> tiles: table $7340 (4 tile bytes per cell type, tile 256 + byte);
 *   towns       (mode 2): 9 maps of 364 bytes = 18 x 20 (inside x 2..17, y 2..17) from $9D1C,
 *               map = $C44D, party = $C444, tiles $7480 (entered with button 2 on the town's
 *               overworld cell, types 11-19, handlers $3565..$35DF);
 *   caverns     (mode 3, dark): 9 floors of the same size right after the towns ($A9E8 + 364 n),
 *               map = $C44F, party = $C447, tiles $757C. Only the party's own cell is shown, 3 x 3
 *               with $C401, 5 x 5 with $C402 ($6600 / $6699 / $674A).
 * Map tiles: bank 3 $8000 / $9270 / $AAAB, unpacked by $3F60 to VRAM $2000 (4 planes, each RLE:
 * n = 0 end, n >= $80 = n & $7F literal bytes, else the next byte n times; every 4th byte), the
 * window's palette = the first 16 colours of $3E8B (overworld, towns) / $3E6B (caverns). So every
 * picture here is drawn from the ROM; checked against VRAM / the game's window.
 *
 * Explored cells: every frame the window on screen is compared with the map data (each cell's 4
 * name-table entries = 256 + its table bytes, or blank = tile 256 in the dark); when it matches,
 * the cells it shows are marked. Kept per save slot (the game has 5: RAM $C400-$C8FF ->
 * battery RAM $8100 + $500 n, "OK" at $8020 + 2n): [insn_hooks] at the save routines'
 * `ld hl,$4B4F` ($31D3 + $2D n: DE = the end of the copy) copy the current set into slot n, the
 * load ($6F8C, HL = $8100 + $500 n) copies slot n back (a slot without map data keeps the current
 * one), New Game ($02AE, the start position) empties it. File "<save>.mwmap".
 * Option MAP REVEAL: EXPLORED (default) / WHOLE MAP.
 * Test env: MW_MAP_LOG=1 (slot saves / loads / New Game), MW_MAP_DEBUG=1 (every cell marked, every frame
 * whose window does not match the map), MW_MAP_PPM=<file> + MW_MAP_FRAME=n (the party's map picture),
 * MW_HUD_KEYS=<L R ...> (keys for the MAP tab when it is drawn first); mw_qol.c: MW_HUD_OPEN, MW_HUD_PAGE,
 * MW_TEST_CALL.
 * Names: the ROM has town names only in the dialogue ("WELCOME TO ORUK"); the 18 town / cavern maps
 * were matched against the user's map pictures (ROMs/Maps, by colour / walls, one clear match each):
 * towns 1-9 = Garia, Oruk, Doris, Torif, Kadia, Menos, Minos, Tegea, Eratia; caverns 1-9 = Julus,
 * Rokhod, Sehod, Ohod, Iphis, Silvius, Gelkis F1, F2, F3 (Gelkis = the final cavern, 3 floors).
 *
 * Party box (side panel): the game's status rows show two bars per member (name table rows 16-23,
 * column 5: EXP bar $7C0A + $80 n = (+6) of (+8), HP bar $7C4A + $80 n = (+$A) of (+$C), $45B9 ...);
 * the panel shows those numbers. Members = blocks $C500 + $40 n: +0 alive (cleared at HP 0, $0414),
 * +1 level (0 = not in the party yet), +6 bar, +8 bar limit, +$A HP, +$C max HP. Names: the hero =
 * RAM $C464 (4 tiles: 1-26 A-Z, $1C '.', $1D ',', $3E '!', $3F '?', $41 '-', $46-$4F digits; the row
 * above at $C460 only holds accent marks), the others = the ROM strings the game draws ($25FC / $2613 /
 * $262A + 4: GUY, MEDI, TREO).
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hud.h"
#include "safe_file.h"
#include "sms_runtime.h"
#include "video/sms_vdp.h"

extern uint8_t *glue_rom_image(size_t *size);
extern const char *glue_save_path(void);

#define NAREA   19                                  /* 0 overworld, 1..9 towns, 10..18 cavern floors */
#define OW_W    98
#define OW_H    76
#define SM_W    18
#define SM_H    20
#define SM_FIRST 0x9D1C
#define SM_SIZE 364
#define CELLS   (OW_W * OW_H + 18 * SM_W * SM_H)   /* one set of explored flags */
#define NSET    6                                   /* 0 = the game being played, 1..5 = save slots */
#define CS      16                                  /* cell size in pixels */
#define OVS     4                                   /* overview picture: pixels per cell */

#define BG      0xFF0A1024u
#define BOX     0xFF141E3Cu
#define FRAME   0xFF3C6EA0u
#define WHITE   0xFFFFFFFFu
#define GREY    0xFF8C96AAu
#define CYAN    0xFF60D0FFu
#define YEL     0xFFFFE040u
#define FOG     0xFF161A24u
#define RED     0xFFFF5050u

int mw_map_reveal;                                  /* option MAP REVEAL: 0 explored, 1 whole map */

static const uint8_t *s_rom; static size_t s_rom_n;
static uint8_t  s_set[NSET][CELLS];
static bool     s_has[NSET];                        /* slot n has map data */
static bool     s_dirty;
static uint32_t *s_pic[NAREA]; static uint32_t s_gen[NAREA];
static uint32_t s_ov[94 * OVS * 72 * OVS]; static uint32_t s_ov_gen = 1;   /* overworld, 4 px per cell */
static uint32_t *s_cellpix[3];                      /* per kind: 256 cell types x 16 x 16 ARGB */
static int      s_area = -1, s_pos;                 /* where the party is (RAM), -1 = not on a map */
static int      s_view = -1;                        /* MAP tab: the map shown */

static int kind(int a) { return a == 0 ? 0 : a <= 9 ? 1 : 2; }
static int aw(int a) { return a ? SM_W : OW_W; }
static int ah(int a) { return a ? SM_H : OW_H; }
/* inside: 2 border rows at the top and bottom, 2 border columns at the left (towns / caverns: these are
   also the right border, of the next row) + 2 more at the right on the overworld */
static int iw(int a) { return a ? SM_W - 2 : OW_W - 4; }
static int ih(int a) { return ah(a) - 4; }
static int afile(int a) { return a ? SM_FIRST + SM_SIZE * (a - 1) : 0x8000; }
static int aoff(int a) { return a ? OW_W * OW_H + (a - 1) * SM_W * SM_H : 0; }
static uint8_t rom(int o) { return o >= 0 && (size_t)o < s_rom_n ? s_rom[o] : 0; }

static void area_name(int a, char *t, size_t n)
{
    static const char *const k_names[NAREA] = { "OVERWORLD",
        "GARIA", "ORUK", "DORIS", "TORIF", "KADIA", "MENOS", "MINOS", "TEGEA", "ERATIA",
        "JULUS CAVERN", "ROKHOD CAVERN", "SEHOD CAVERN", "OHOD CAVERN", "IPHIS CAVERN", "SILVIUS CAVERN",
        "GELKIS F1", "GELKIS F2", "GELKIS F3" };
    snprintf(t, n, "%s", a >= 0 && a < NAREA ? k_names[a] : "?");
}

/* map tiles of one kind, from the ROM (as $3F60) */
static void make_cellpix(int k)
{
    static const uint16_t tiles[3] = { 0x8000, 0x9270, 0xAAAB }, tbl[3] = { 0x7340, 0x7480, 0x757C }, pal[3] = { 0x3E8B, 0x3E8B, 0x3E6B };
    static uint8_t v[0x4000];
    memset(v, 0, sizeof v);
    int o = 0xC000 + tiles[k] - 0x8000;                 /* bank 3 */
    for (int p = 0; p < 4; p++)
        for (int d = p;;) {
            int n = rom(o++);
            if (!n) break;
            if (n & 0x80) for (int i = 0; i < (n & 0x7F); i++, d += 4) { uint8_t b = rom(o++); if (d < 0x4000) v[d] = b; }
            else { uint8_t b = rom(o++); for (int i = 0; i < n; i++, d += 4) if (d < 0x4000) v[d] = b; }
        }
    uint32_t c[16];
    for (int i = 0; i < 16; i++) {
        uint8_t x = rom(pal[k] + i);
        c[i] = 0xFF000000u | (uint32_t)((x & 3) * 85) << 16 | (uint32_t)(((x >> 2) & 3) * 85) << 8 | (uint32_t)(((x >> 4) & 3) * 85);
    }
    uint32_t *out = s_cellpix[k] = malloc(256 * CS * CS * 4);
    for (int t = 0; t < 256; t++)
        for (int y = 0; y < CS; y++)
            for (int x = 0; x < CS; x++) {
                int tile = rom(tbl[k] + 4 * t + (y >> 3) * 2 + (x >> 3)) & 0xFF;
                const uint8_t *r = v + tile * 32 + (y & 7) * 4;
                int b = 7 - (x & 7), ci = (r[0] >> b & 1) | (r[1] >> b & 1) << 1 | (r[2] >> b & 1) << 2 | (r[3] >> b & 1) << 3;
                out[(t * CS + y) * CS + x] = c[ci];
            }
}

/* a set of explored flags: s_set[0] (the game being played), a save slot's, or none */
static bool seen_in(const uint8_t *set, int a, int col, int row) { return set[aoff(a) + (row + 2) * aw(a) + col + 2] != 0; }
static bool seen(int a, int col, int row) { return seen_in(s_set[0], a, col, row); }
static uint32_t s_set_ver = 1;                      /* changes when a slot's set or MAP REVEAL changes */
/* one cell (inside coordinates) of `set` into the map picture `pic` and, for the overworld, the
   overview `ov` (4 x 4 averages of the cell picture); either may be NULL */
static void cell_to(const uint8_t *set, int a, int col, int row, uint32_t *pic, uint32_t *ov)
{
    bool show = mw_map_reveal || seen_in(set, a, col, row);
    int t = rom(afile(a) + (row + 2) * aw(a) + col + 2);
    const uint32_t *s = s_cellpix[kind(a)] + t * CS * CS;
    if (pic) {
        int pw = iw(a) * CS;
        uint32_t *d = pic + (size_t)row * CS * pw + col * CS;
        for (int y = 0; y < CS; y++)
            for (int x = 0; x < CS; x++) d[(size_t)y * pw + x] = show ? s[y * CS + x] : FOG;
    }
    if (ov && a == 0)
        for (int y = 0; y < OVS; y++)
            for (int x = 0; x < OVS; x++) {
                unsigned r = 0, g = 0, b = 0, q = CS / OVS;
                for (int yy = 0; yy < (int)q; yy++)
                    for (int xx = 0; xx < (int)q; xx++) {
                        uint32_t p = show ? s[(y * q + yy) * CS + x * q + xx] : FOG;
                        r += p >> 16 & 255; g += p >> 8 & 255; b += p & 255;
                    }
                r /= q * q; g /= q * q; b /= q * q;
                ov[(size_t)(row * OVS + y) * (94 * OVS) + col * OVS + x] = 0xFF000000u | r << 16 | g << 8 | b;
            }
}
static void draw_cell(int a, int col, int row)      /* inside coordinates */
{
    if (!s_pic[a]) return;
    cell_to(s_set[0], a, col, row, s_pic[a], s_ov);
    if (a == 0) s_ov_gen++;
    s_gen[a]++;
}
static bool need_cellpix(void)
{
    if (!s_rom) return false;
    for (int k = 0; k < 3; k++) if (!s_cellpix[k]) make_cellpix(k);
    return true;
}
static void redraw(int a)
{
    if (!need_cellpix()) return;
    if (!s_pic[a]) s_pic[a] = malloc((size_t)iw(a) * CS * ih(a) * CS * 4);
    for (int r = 0; r < ih(a); r++) for (int c = 0; c < iw(a); c++) draw_cell(a, c, r);
}
static void redraw_all(void) { for (int a = 0; a < NAREA; a++) if (s_pic[a] || a == 0) redraw(a); }
void mw_map_reveal_changed(void) { s_set_ver++; redraw_all(); }
static int explored_pct_in(const uint8_t *set, int a)
{
    int n = 0, t = iw(a) * ih(a);
    for (int r = 0; r < ih(a); r++) for (int c = 0; c < iw(a); c++) n += seen_in(set, a, c, r);
    return t ? (n * 100 + t - 1) / t : 0;
}
static int explored_pct(int a) { return explored_pct_in(s_set[0], a); }
static bool any_seen(int a)
{
    for (int r = 0; r < ih(a); r++) for (int c = 0; c < iw(a); c++) if (seen(a, c, r)) return true;
    return false;
}

/* ---- the file: "MWM1", then per set: present byte + CELLS bits ---- */
static void map_path(char *out, size_t n)
{
    const char *sp = glue_save_path();
    out[0] = 0;
    if (!sp || !sp[0]) return;
    snprintf(out, n, "%s", sp);
    size_t l = strlen(out);
    if (l > 4 && !strcmp(out + l - 4, ".sav")) out[l - 4] = 0;
    strncat(out, ".mwmap", n - strlen(out) - 1);
}
void mw_map_save(void)
{
    if (!s_dirty || getenv("SMS_NO_SAVE")) return;
    char p[1100]; map_path(p, sizeof p);
    if (!p[0]) return;
    FILE *f = safe_fopen_w(p, "wb"); if (!f) return;
    fwrite("MWM1", 1, 4, f);
    for (int s = 0; s < NSET; s++) {
        fputc(s == 0 || s_has[s], f);
        for (int i = 0; i < CELLS; i += 8) {
            uint8_t b = 0;
            for (int j = 0; j < 8 && i + j < CELLS; j++) b |= (uint8_t)((s_set[s][i + j] != 0) << j);
            fputc(b, f);
        }
    }
    if (safe_fclose(f, p)) s_dirty = false;
}
static void map_load(void)
{
    char p[1100]; map_path(p, sizeof p);
    if (!p[0]) return;
    FILE *f = fopen(p, "rb"); if (!f) return;
    char m[4];
    if (fread(m, 1, 4, f) == 4 && !memcmp(m, "MWM1", 4))
        for (int s = 0; s < NSET; s++) {
            int pr = fgetc(f); if (pr == EOF) break;
            uint8_t bits[(CELLS + 7) / 8];
            if (fread(bits, 1, sizeof bits, f) != sizeof bits) break;
            for (int i = 0; i < CELLS; i++) s_set[s][i] = bits[i / 8] >> (i & 7) & 1;
            if (s) s_has[s] = pr != 0;
        }
    fclose(f);
}

static bool mlog(void) { static int on = -1; if (on < 0) on = getenv("MW_MAP_LOG") != NULL; return on; }
/* [insn_hooks] $31D3 / $3200 / $322D / $325A / $3287: the save routines' `ld hl,$4B4F` after the ldir
   (DE = $8100 + $500 n + $500) */
int mw_map_saved(void)
{
    int de = g_z80.d << 8 | g_z80.e, n = (de - 0x8600) / 0x500 + 1;
    if (n < 1 || n > 5 || (de - 0x8600) % 0x500) return 0;
    memcpy(s_set[n], s_set[0], CELLS); s_has[n] = true; s_dirty = true; s_set_ver++;
    if (mlog()) fprintf(stderr, "[mw_map] saved to slot %d (overworld %d%% explored)\n", n, explored_pct(0));
    mw_map_save();
    return 0;
}
/* [insn_hooks] $6F8C (`ld de,$C400` of the load: HL = $8100 + $500 n) */
int mw_map_loaded(void)
{
    int hl = g_z80.h << 8 | g_z80.l, n = (hl - 0x8100) / 0x500 + 1;
    if (n < 1 || n > 5 || (hl - 0x8100) % 0x500) return 0;
    if (s_has[n]) { memcpy(s_set[0], s_set[n], CELLS); s_dirty = true; redraw_all(); }
    if (mlog()) fprintf(stderr, "[mw_map] loaded slot %d (%s; overworld %d%% explored)\n", n, s_has[n] ? "its map" : "no map data: kept", explored_pct(0));
    mw_map_save();
    return 0;
}
/* [insn_hooks] $02AE (New Game: `ld hl,$0558`, the start position) */
int mw_map_new_game(void)
{
    memset(s_set[0], 0, CELLS); s_dirty = true; redraw_all();
    if (mlog()) fprintf(stderr, "[mw_map] new game: map emptied\n");
    mw_map_save();
    return 0;
}

/* game RAM $C400-$C8FF: live, or a save slot's copy of it (blk, from battery RAM) */
static uint8_t rd8(const uint8_t *blk, uint16_t a) { return blk && a >= 0xC400 && a < 0xC900 ? blk[a - 0xC400] : sms_read8(a); }
static int rd16(const uint8_t *blk, uint16_t a) { return rd8(blk, a) | rd8(blk, (uint16_t)(a + 1)) << 8; }

/* where the party is: area + linear cell index, from RAM (blk: a save slot's copy) */
static int party_area_in(const uint8_t *blk, int *pos)
{
    int m = rd8(blk, 0xC440), base;
    if (m == 1) { *pos = rd16(blk, 0xC441); return *pos < OW_W * OW_H ? 0 : -1; }
    if (m == 2) { base = rd16(blk, 0xC44D); *pos = rd16(blk, 0xC444); }
    else if (m == 3) { base = rd16(blk, 0xC44F); *pos = rd16(blk, 0xC447); }
    else return -1;
    int d = base - SM_FIRST;
    if (d < 0 || d % SM_SIZE || d / SM_SIZE > 17 || *pos >= SM_W * SM_H) return -1;
    int a = 1 + d / SM_SIZE;
    if ((m == 2) != (a <= 9)) return -1;
    return a;
}
static int party_area(int *pos) { return party_area_in(NULL, pos); }

/* every frame: mark what the game's map window shows */
void mw_map_frame(void)
{
    if (!s_rom) { s_rom = glue_rom_image(&s_rom_n); if (!s_rom || s_rom_n < 0x10000) { s_rom = NULL; return; } redraw(0); }
    int pos, a = party_area(&pos);
    s_area = a; s_pos = pos;
    if (a < 0) return;
    if (!s_pic[a]) redraw(a);
    static const uint16_t tbl[3] = { 0x7340, 0x7480, 0x757C };
    int k = kind(a), w = aw(a), nt = (g_vdp.reg[2] & 0x0E) << 10, mark[25], nm = 0;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int p = pos + dy * w + dx, t = rom(afile(a) + p), match = 1, blank = 1;
            for (int q = 0; q < 4; q++) {
                int row = (dy + 2) * 2 + (q >> 1), col = 21 + (dx + 2) * 2 + (q & 1);
                int e = (g_vdp.vram[(nt + 2 * (row * 32 + col)) & 0x3FFF] | g_vdp.vram[(nt + 2 * (row * 32 + col) + 1) & 0x3FFF] << 8) & 0x1FF;
                if (e != 0x100 + rom(tbl[k] + 4 * t + q)) match = 0;
                if (e != 0x100) blank = 0;
            }
            if (match) mark[nm++] = p;
            else if (!blank || (dx == 0 && dy == 0)) {         /* not the map (yet): another screen, a step being drawn */
                if (getenv("MW_MAP_DEBUG")) fprintf(stderr, "[mw_map] area %d pos %d: cell %d,%d (type %d) not shown\n", a, pos, dx, dy, t);
                return;
            }
        }
    for (int i = 0; i < nm; i++) {
        int p = mark[i], col = p % w - 2, row = p / w - 2;
        if (col < 0 || row < 0 || col >= iw(a) || row >= ih(a) || s_set[0][aoff(a) + p]) continue;
        s_set[0][aoff(a) + p] = 1; s_dirty = true;
        if (getenv("MW_MAP_DEBUG")) fprintf(stderr, "[mw_map] area %d: cell %d,%d explored\n", a, col, row);
        draw_cell(a, col, row);
    }
}

/* ---- drawing helpers ---- */
static void blit(uint32_t *b, int bw, int bh, int space, float dx, float dy, float dw, float dh,
                 const uint32_t *pic, int pw, int ph, int sx, int sy, int sw, int sh, uint32_t gen)
{
    if (hud_hires()) { hud_image(space, dx, dy, dw, dh, pic, pw, ph, sx, sy, sw, sh, false, gen); return; }
    for (int y = 0; y < (int)dh; y++)
        for (int x = 0; x < (int)dw; x++) {
            int X = (int)dx + x, Y = (int)dy + y;
            if (X < 0 || Y < 0 || X >= bw || Y >= bh) continue;
            b[(size_t)Y * bw + X] = pic[(size_t)(sy + (int)(y * sh / dh)) * pw + sx + (int)(x * sw / dw)];
        }
}
static void marker(uint32_t *b, int bw, int bh, int space, float cx, float cy, float r)
{
    if (hud_hires()) { hud_marker(space, cx, cy, r, r > 3 ? 1.5f : 1.f, YEL, 0xFF000000u); return; }
    int R = (int)(r + 0.5f); if (R < 1) R = 1;
    hud_fill(b, bw, bh, (int)cx - R - 1, (int)cy - R - 1, (int)cx + R + 1, (int)cy + R + 1, 0xFF000000u, false);
    hud_fill(b, bw, bh, (int)cx - R, (int)cy - R, (int)cx + R, (int)cy + R, YEL, false);
}
static void frame_rect(uint32_t *b, int bw, int bh, int space, float x, float y, float w, float h, float t, uint32_t c)
{
    if (hud_hires()) {
        hud_rect(space, x, y, w, t, c); hud_rect(space, x, y + h - t, w, t, c);
        hud_rect(space, x, y, t, h, c); hud_rect(space, x + w - t, y, t, h, c);
        return;
    }
    hud_fill(b, bw, bh, (int)x, (int)y, (int)(x + w), (int)y + 1, c, false);
    hud_fill(b, bw, bh, (int)x, (int)(y + h) - 1, (int)(x + w), (int)(y + h), c, false);
    hud_fill(b, bw, bh, (int)x, (int)y, (int)x + 1, (int)(y + h), c, false);
    hud_fill(b, bw, bh, (int)(x + w) - 1, (int)y, (int)(x + w), (int)(y + h), c, false);
}
static void box(uint32_t *b, int w, int h, int x0, int y0, int x1, int y1)
{
    hud_fill(b, w, h, x0, y0, x1, y1, FRAME, false);
    hud_fill(b, w, h, x0 + 2, y0 + 2, x1 - 2, y1 - 2, BOX, false);
}
/* a map fitted into (x, y, w, h); returns the scale and the picture's top left */
static float fit(int a, float x, float y, float w, float h, float *ox, float *oy)
{
    float pw = (float)iw(a) * CS, ph = (float)ih(a) * CS, s = w / pw < h / ph ? w / pw : h / ph;
    *ox = x + (w - pw * s) / 2; *oy = y + (h - ph * s) / 2;
    return s;
}

/* ---- HUD MAP tab ---- */
static void map_key(int key)
{
    if (key != HUD_KEY_LEFT && key != HUD_KEY_RIGHT) return;
    int d = key == HUD_KEY_RIGHT ? 1 : -1;
    for (int i = 1; i <= NAREA; i++) {
        int a = ((s_view + d * i) % NAREA + NAREA) % NAREA;
        if (a == 0 || mw_map_reveal || any_seen(a) || a == s_area) { s_view = a; if (!s_pic[a]) redraw(a); return; }
    }
}
void mw_map_hud_closed(void) { s_view = -1; mw_map_save(); }
static void map_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1)
{
    if (!s_rom) { hud_text(fb, w, h, x0 + 4, y0, "NO MAP", GREY); return; }
    if (s_view < 0) s_view = s_area >= 0 ? s_area : 0;
    int a = s_view;
    if (!s_pic[a]) redraw(a);
    {   static int sent; const char *k = getenv("MW_HUD_KEYS");      /* test: keys for this page */
        if (k && !sent) { sent = 1; for (; *k; k++) map_key(*k == 'L' ? HUD_KEY_LEFT : *k == 'R' ? HUD_KEY_RIGHT : -1); a = s_view; } }
    char t[48], n[24];
    area_name(a, n, sizeof n);
    hud_text(fb, w, h, x0 + 4, y0, n, CYAN);
    snprintf(t, sizeof t, "%d%% EXPLORED", explored_pct(a));
    hud_text(fb, w, h, x1 - 4 - 6 * (int)strlen(t), y0, t, GREY);
    int ly = y1 - 9;
    hud_text(fb, w, h, x0 + 4, ly, "LEFT/RIGHT: OTHER MAPS", GREY);
    float ox, oy, bx = (float)x0 + 4, by = (float)y0 + 11, bw = (float)(x1 - x0 - 8), bh = (float)(ly - 2 - by);
    float s = fit(a, bx, by, bw, bh, &ox, &oy), pw = iw(a) * CS * s, ph = ih(a) * CS * s;
    hud_fill(fb, w, h, (int)ox - 1, (int)oy - 1, (int)(ox + pw) + 1, (int)(oy + ph) + 1, 0xFF000000u, false);
    blit(fb, w, h, HUD_SPACE_GAME, ox, oy, pw, ph, s_pic[a], iw(a) * CS, ih(a) * CS, 0, 0, iw(a) * CS, ih(a) * CS, s_gen[a]);
    int pa = a == s_area ? s_pos : a == 0 && s_area > 0 ? sms_read16(0xC441) : -1;   /* in a town: its overworld cell */
    static unsigned blink; blink++;
    if (pa >= 0 && (blink / 18) % 2 == 0) {
        float cs = CS * s;
        marker(fb, w, h, HUD_SPACE_GAME, ox + (pa % aw(a) - 2 + 0.5f) * cs, oy + (pa / aw(a) - 2 + 0.5f) * cs, cs * 0.45f < 1.5f ? 1.5f : cs * 0.45f);
    }
}

/* ---- side panel ---- */
#define VW 232                                      /* surroundings box: 14.5 x 12.5 cells at 1:1 */
#define VH 192

static char name_char(uint8_t c)
{
    if (c == 0) return ' ';
    if (c <= 26) return (char)('A' + c - 1);
    if (c >= 0x46 && c <= 0x4F) return (char)('0' + c - 0x46);
    switch (c) { case 0x1C: return '.'; case 0x1D: return ','; case 0x3E: return '!'; case 0x3F: return '?'; case 0x41: return '-'; }
    return '*';
}
/* the 4 members: name, LV, HP x/y, EXP x/y (the game's two bars as numbers), from RAM or a save
   slot's copy of it (blk); returns the box height */
static int party_box(uint32_t *b, int w, int h, int y, const uint8_t *blk)
{
    static const uint16_t k_names[4] = { 0, 0x2600, 0x2617, 0x262E };
    int n = 0;
    for (int m = 0; m < 4; m++) if (rd8(blk, (uint16_t)(0xC501 + 0x40 * m))) n++;
    if (!n) return 0;
    int bh = 8 + 12 * n + 4;
    box(b, w, h, 8, y, w - 8, y + bh);
    int ly = y + 7;
    for (int m = 0; m < 4; m++) {
        uint16_t p = (uint16_t)(0xC500 + 0x40 * m);
        int lv = rd8(blk, (uint16_t)(p + 1));
        if (!lv) continue;
        char nm[5], t[24];
        for (int i = 0; i < 4; i++) nm[i] = name_char(m ? rom(k_names[m] + i) : rd8(blk, (uint16_t)(0xC464 + i)));
        nm[4] = 0;
        bool alive = rd8(blk, p) != 0;
        int hp = rd16(blk, (uint16_t)(p + 0x0A)), mhp = rd16(blk, (uint16_t)(p + 0x0C));
        int ex = rd16(blk, (uint16_t)(p + 6)), lim = rd16(blk, (uint16_t)(p + 8));
        int x = 16;
        hud_text(b, w, h, x, ly, nm, alive ? WHITE : GREY);
        x += 6 * 5;
        hud_text(b, w, h, x, ly, "LV", GREY);
        snprintf(t, sizeof t, "%d", lv); hud_text(b, w, h, x + 12, ly, t, WHITE);
        x += 6 * 5;
        hud_text(b, w, h, x, ly, "HP", GREY);
        if (alive) snprintf(t, sizeof t, "%d/%d", hp, mhp); else snprintf(t, sizeof t, "DEAD");
        hud_text(b, w, h, x + 15, ly, t, !alive || hp * 4 <= mhp ? RED : WHITE);
        x += 6 * 11;
        hud_text(b, w, h, x, ly, "EXP", GREY);
        if (lv >= 15) snprintf(t, sizeof t, "MAX"); else snprintf(t, sizeof t, "%d/%d", ex, lim);
        hud_text(b, w, h, x + 21, ly, t, WHITE);
        ly += 12;
    }
    return bh;
}

/* ---- save-slot preview ----
 * The game's 5 save slots (battery RAM, first 16 KB, mapped at $8000): slot n = a copy of RAM
 * $C400-$C8FF at $8100 + $500 (n - 1), "OK" ($4B4F) at $8020 + 2 (n - 1) when it holds a save.
 * The two slot choices (found 2026-11-24):
 *   title CONTINUE ("SELECT PLEASE" NO. 1-5): game state ($C481 & $7F) = 5 (only the title sets it,
 *   $6D85; handler $6ED6), cursor $C30A = 0..4 (cleared at $6F02, moved at $6F35, load at $6F5C);
 *   QUIT -> SAVE: the game's menu ($40DD: definition $D3D8 copied to $C170..; entries $27 NO SAVE,
 *   $28-$2C SAVE 1-5, count $C179 = 6, cursor $C178; $C160-$C17F are cleared when a menu closes, $4177).
 * While one is open the panel shows the highlighted slot: party, guilders / fangs / character points
 * and where it was saved, on that slot's own explored map (none if it was saved before the map). */
extern const uint8_t *glue_sram_image(size_t *size);
static int slot_menu(const uint8_t **blk, bool *used)
{
    int slot = 0;
    if ((sms_read8(0xC481) & 0x7F) == 5) slot = sms_read8(0xC30A) + 1;
    else if (sms_read8(0xC179) == 6) {
        slot = sms_read8(0xC178);                   /* 0 = NO SAVE */
        for (int i = 0; i < 6; i++) if (sms_read8((uint16_t)(0xC170 + i)) != 0x27 + i) slot = 0;
    }
    size_t n = 0; const uint8_t *sram = glue_sram_image(&n);
    if (slot < 1 || slot > 5 || !sram || n < 0x100 + 0x500u * (unsigned)slot) return 0;
    const uint8_t *ok = sram + 0x20 + 2 * (slot - 1);
    *used = ok[0] == 0x4F && ok[1] == 0x4B;
    *blk = sram + 0x100 + 0x500 * (slot - 1);
    return slot;
}

static uint8_t   s_none[CELLS];                     /* a slot without map data: nothing explored */
static uint32_t *s_pv_pic; static uint32_t s_pv_gen = 1;            /* the slot's map of its area */
static uint32_t  s_pv_ov[94 * OVS * 72 * OVS]; static uint32_t s_pv_ov_gen = 1;
static const uint8_t *slot_set(int slot) { return s_has[slot] ? s_set[slot] : s_none; }
static bool pv_build(int slot, int a)
{
    static int ls = -1, la = -1; static uint32_t lv;
    if (!need_cellpix()) return false;
    if (!s_pv_pic) s_pv_pic = malloc((size_t)iw(0) * CS * ih(0) * CS * 4);   /* the largest map */
    if (!s_pv_pic) return false;
    if (slot == ls && a == la && lv == s_set_ver) return true;
    const uint8_t *set = slot_set(slot);
    for (int r = 0; r < ih(a); r++) for (int c = 0; c < iw(a); c++) cell_to(set, a, c, r, s_pv_pic, NULL);
    s_pv_gen++;
    if (slot != ls || lv != s_set_ver) {
        for (int r = 0; r < ih(0); r++) for (int c = 0; c < iw(0); c++) cell_to(set, 0, c, r, NULL, s_pv_ov);
        s_pv_ov_gen++;
    }
    ls = slot; la = a; lv = s_set_ver;
    return true;
}

/* what the panel shows below the party: the game being played, or a save slot */
typedef struct {
    const uint8_t *blk;                             /* NULL = live RAM, else the slot's copy */
    int area, pos;                                  /* the map shown, the party's cell on it */
    bool on_map;                                    /* the party is on that map (marker) */
    const uint32_t *pic; uint32_t gen;              /* that map's picture */
    const uint32_t *ov; uint32_t ov_gen;            /* the overworld overview */
    int pct, ow_pct;                                /* explored % */
} View;

/* "SLOT n" + guilders (stored in tens: shown with a trailing 0, as the game does), fangs,
   character points (a new game starts at 32000 = "+0") */
static void slot_header(uint32_t *b, int w, int h, int y, int slot, const uint8_t *blk)
{
    char t[24];
    box(b, w, h, 8, y, w - 8, y + 56);
    snprintf(t, sizeof t, "SLOT %d", slot);
    hud_text_scaled(b, w, h, 16, y + 9, t, CYAN, 2);
    int x = 16, ly = y + 37;
    hud_text(b, w, h, x, ly, "$", GREY);
    snprintf(t, sizeof t, "%d", rd16(blk, 0xC430) * 10); hud_text(b, w, h, x + 12, ly, t, WHITE);
    x += 12 + 6 * (int)strlen(t) + 12;
    hud_text(b, w, h, x, ly, "FANGS", GREY);
    snprintf(t, sizeof t, "%d", rd16(blk, 0xC434)); hud_text(b, w, h, x + 36, ly, t, WHITE);
    x += 36 + 6 * (int)strlen(t) + 12;
    hud_text(b, w, h, x, ly, "C", GREY);
    snprintf(t, sizeof t, "%+d", rd16(blk, 0xC436) - 32000); hud_text(b, w, h, x + 12, ly, t, WHITE);
}

/* the map the party is on, centred on it (1:1), or the whole town / floor; then the whole overworld
   (explored part) with the surroundings window framed and the party (in a town / cavern: its
   overworld cell) */
static void draw_view(uint32_t *b, int w, int h, int y, const View *v)
{
    char t[48];
    int a = v->area, pos = v->pos;
    box(b, w, h, 8, y, w - 8, y + 24 + VH + 8);
    area_name(a, t, sizeof t);
    hud_text_scaled(b, w, h, 16, y + 6, t, CYAN, 2);
    snprintf(t, sizeof t, "%d%%", v->pct);
    hud_text(b, w, h, w - 16 - 6 * (int)strlen(t), y + 10, t, GREY);
    float mx = 12, my = (float)y + 24;
    hud_fill(b, w, h, (int)mx, (int)my, (int)mx + VW, (int)my + VH, 0xFF000000u, false);
    int pw = iw(a) * CS, ph = ih(a) * CS, col = pos % aw(a) - 2, row = pos / aw(a) - 2;
    float px, py, cs;
    int vx = 0, vy = 0;                               /* overworld window (picture pixels) */
    if (pw > VW * 3 / 2) {
        vx = col * CS + CS / 2 - VW / 2; vy = row * CS + CS / 2 - VH / 2;
        if (vx < 0) vx = 0; if (vx > pw - VW) vx = pw - VW;
        if (vy < 0) vy = 0; if (vy > ph - VH) vy = ph - VH;
        blit(b, w, h, HUD_SPACE_PANEL, mx, my, VW, VH, v->pic, pw, ph, vx, vy, VW, VH, v->gen);
        cs = CS; px = mx + col * CS - vx; py = my + row * CS - vy;
    } else {
        float ox, oy, s = fit(a, mx, my, VW, VH, &ox, &oy);
        blit(b, w, h, HUD_SPACE_PANEL, ox, oy, pw * s, ph * s, v->pic, pw, ph, 0, 0, pw, ph, v->gen);
        cs = CS * s; px = ox + col * cs; py = oy + row * cs;
    }
    static unsigned blink; blink++;
    if (v->on_map && (blink / 20) % 3) marker(b, w, h, HUD_SPACE_PANEL, px + cs / 2, py + cs / 2, cs * 0.3f);
    y += 24 + VH + 8 + 8;

    int avail = h - 8 - y - 24 - 8;
    float ow = (float)(w - 24), sc = ow / (94 * OVS);
    if (72 * OVS * sc > avail) sc = (float)avail / (72 * OVS);
    if (sc <= 0) return;
    float dw = 94 * OVS * sc, dh = 72 * OVS * sc, ox = 8 + ((w - 16) - dw) / 2, oy = (float)y + 24;
    box(b, w, h, 8, y, w - 8, y + 24 + (int)dh + 8);
    hud_text_scaled(b, w, h, 16, y + 6, "WORLD", CYAN, 2);
    snprintf(t, sizeof t, "%d%%", v->ow_pct);
    hud_text(b, w, h, w - 16 - 6 * (int)strlen(t), y + 10, t, GREY);
    blit(b, w, h, HUD_SPACE_PANEL, ox, oy, dw, dh, v->ov, 94 * OVS, 72 * OVS, 0, 0, 94 * OVS, 72 * OVS, v->ov_gen);
    float c = OVS * sc;
    if (a == 0) frame_rect(b, w, h, HUD_SPACE_PANEL, ox + vx / (float)CS * c, oy + vy / (float)CS * c, VW / (float)CS * c, VH / (float)CS * c, 1.f, CYAN);
    int op = rd16(v->blk, 0xC441);
    if (op < OW_W * OW_H && (blink / 20) % 3)
        marker(b, w, h, HUD_SPACE_PANEL, ox + (op % OW_W - 2 + 0.5f) * c, oy + (op / OW_W - 2 + 0.5f) * c, 2.f);
}

void mw_map_panel(uint32_t *b, int w, int h)
{
    for (int i = 0; i < w * h; i++) b[i] = BG;
    bool used = false; const uint8_t *blk = NULL;
    int slot = s_rom ? slot_menu(&blk, &used) : 0;
    if (slot && !used) {                              /* empty slot */
        char t[16];
        box(b, w, h, 8, 8, w - 8, h - 8);
        snprintf(t, sizeof t, "SLOT %d", slot);
        hud_text_scaled(b, w, h, (w - 6 * 12) / 2, h / 2 - 30, t, CYAN, 2);
        hud_text_scaled(b, w, h, (w - 5 * 12) / 2, h / 2 - 6, "EMPTY", GREY, 2);
        return;
    }
    if (!slot && (!s_rom || sms_read8(0xC440) == 0)) {   /* title / name entry */
        box(b, w, h, 8, 8, w - 8, h - 8);
        if (hud_logo(NULL, NULL)) hud_draw_logo(b, w, h, HUD_SPACE_PANEL, 24.f, (float)(h / 2 - 70), (float)(w - 48), 120.f, true);
        else {                                        /* two lines, like Phantasy Star's panel */
            hud_text_scaled(b, w, h, (w - 7 * 12) / 2, h / 2 - 30, "MIRACLE", CYAN, 2);
            hud_text_scaled(b, w, h, (w - 8 * 12) / 2, h / 2 - 6, "WARRIORS", CYAN, 2);
        }
        return;
    }
    int y = 8;
    if (slot) slot_header(b, w, h, y, slot, blk);
    else {
        box(b, w, h, 8, y, w - 8, y + 56);
        if (hud_logo(NULL, NULL)) hud_draw_logo(b, w, h, HUD_SPACE_PANEL, 16.f, (float)y + 5, (float)(w - 32), 46.f, true);
        else hud_text_scaled(b, w, h, (w - 16 * 12) / 2, y + 21, "MIRACLE WARRIORS", CYAN, 2);
    }
    y += 64;
    int pb = party_box(b, w, h, y, blk);
    if (pb) y += pb + 8;

    View v = { .blk = blk };
    if (slot) {
        int pos, a = party_area_in(blk, &pos);
        v.on_map = a >= 0;
        v.area = a >= 0 ? a : 0; v.pos = a >= 0 ? pos : rd16(blk, 0xC441);
        if (!pv_build(slot, v.area)) return;
        v.pic = s_pv_pic; v.gen = s_pv_gen; v.ov = s_pv_ov; v.ov_gen = s_pv_ov_gen;
        v.pct = explored_pct_in(slot_set(slot), v.area); v.ow_pct = explored_pct_in(slot_set(slot), 0);
    } else {
        v.on_map = s_area >= 0;
        v.area = s_area >= 0 ? s_area : 0; v.pos = s_area >= 0 ? s_pos : sms_read16(0xC441);
        if (!s_pic[v.area]) redraw(v.area);
        if (!s_pic[v.area]) return;
        v.pic = s_pic[v.area]; v.gen = s_gen[v.area]; v.ov = s_ov; v.ov_gen = s_ov_gen;
        v.pct = explored_pct(v.area); v.ow_pct = explored_pct(0);
    }
    draw_view(b, w, h, y, &v);
}

/* test: MW_MAP_PPM=<file> + MW_MAP_FRAME=n writes the picture of the party's map at frame n */
void mw_map_test(uint64_t frame)
{
    const char *p = getenv("MW_MAP_PPM"), *f = getenv("MW_MAP_FRAME");
    if (!p || !f || (uint64_t)atoll(f) != frame || s_area < 0 || !s_pic[s_area]) return;
    FILE *o = fopen(p, "wb"); if (!o) return;
    int pw = iw(s_area) * CS, ph = ih(s_area) * CS;
    fprintf(o, "P6\n%d %d\n255\n", pw, ph);
    for (int i = 0; i < pw * ph; i++) { uint32_t q = s_pic[s_area][i]; uint8_t c[3] = { (uint8_t)(q >> 16), (uint8_t)(q >> 8), (uint8_t)q }; fwrite(c, 1, 3, o); }
    fclose(o);
    fprintf(stderr, "[mw_map] frame %llu area %d pos %d explored %d%% -> %s\n", (unsigned long long)frame, s_area, s_pos, explored_pct(s_area), p);
}

void mw_map_init(void)
{
    static bool once;
    if (once) return;
    once = true;
    map_load();
    hud_add_page_ex("MAP", map_draw, map_key, true);
    atexit(mw_map_save);
}
