/*
 * ps_worldmap.c - Phantasy Star (SMS) "surroundings" map for the overworld and towns: the screen
 * the party is on plus the 8 screens around it, drawn from the ROM's own map data.
 *
 * Read-only observer like ps_automap.c: it reads RAM / VDP state in the on_frame_post hook and the
 * ROM, never writes to the game.
 *
 * Map format (ps1disasm-master GameMode_LoadMap, LABEL_723D, LABEL_744B, LABEL_3FA; verified
 * 2026-11-13 by rendering the current view and comparing it with the game's frame: identical except
 * sprites and animated tiles):
 *   $C309 map number -> LABEL_DC1[map] = tileset ($C308; 0 Palma, 1 Motavia, 2/3 Dezoris, >= 4 towns)
 *   LABEL_DF1[tileset], 14 bytes: +0 word screen-table pointer, +2 its bank, +9 metatile bank,
 *          +10 word palette pointer (16 BG colours), +12 dungeon entrance table.
 *   screen table: 9 x 9 words (8 x 8 screens + a wrap column/row), screen (sx, sy) at
 *          (sy * 9 + sx) * 2. A screen = 16 x 12 metatiles (256 x 192 px), RLE packed: n < $80 =
 *          repeat the next byte n times, n >= $80 = copy n & $7F bytes, 0 = end.
 *   metatile m: 8 bytes at $8000 + m * 8 in the metatile bank = 2 x 2 name-table words (tile index
 *          9 bits, bit 9 h-flip, bit 10 v-flip, bit 11 sprite palette).
 *   tiles: LABEL_3FA-packed in ROM (4 bitplanes one after the other, each RLE as above, every 4th
 *          byte), overworld (tileset < 4) and towns (>= 4) have one set each, loaded by LoadMap.
 *   camera: x = $C302:$C301 (screen column : pixel), y = $C306 * 192 + $C305 (screen row, pixel
 *          0..191) = top-left of the view; the party = sprite 0 of the sprite table.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "hud.h"
#include "video/sms_vdp.h"

extern uint8_t  sms_read8(uint16_t addr);
extern uint8_t *glue_rom_image(size_t *size);

#define RAM_GAME_MODE 0xC202
#define MODE_MAP      0x09

#define SCR_W 256
#define SCR_H 192
#define AREA_W (3 * SCR_W)
#define AREA_H (3 * SCR_H)

static const uint8_t *g_rom; static size_t g_rom_n;
static long g_df1 = -1;                         /* file offset of LABEL_DF1 */
static long g_dc1 = -1;                         /* LABEL_DC1: map number -> tileset (26 maps) */
static long g_tile_src[2] = { -1, -1 };         /* packed tiles: [0] overworld, [1] towns */
static uint8_t g_tiles[2][0x4000];              /* decoded tile patterns (VRAM image from 0) */
static bool    g_tiles_ok[2];
static bool    g_tiles_from_vram;               /* ROM uses another packer (2.5 retranslation): copy VRAM */

static struct {
    bool    valid;
    uint8_t tileset, map;
    int     px, py;                             /* party, world pixels */
    uint8_t spr_pal[16];                        /* CRAM 16..31 while on the map (BG tiles may use it) */
} g_area;

bool ps_worldmap_draw(uint32_t *fb, int w, int h, int bx, int by, int bw, int bh, int space);

/* Rendered pictures, two of each kind (the side panel shows the current map while the HUD's MAP page
   may show another one: without a second buffer they would re-render each other every frame).
   Least recently used is replaced. `gen` changes with every render (hi-res texture uploads). */
#define WORLD_W (8 * SCR_W)
#define WORLD_H (8 * SCR_H)
typedef struct { uint32_t *px; uint32_t key; uint32_t gen; bool spr, valid; unsigned used; } WorldBuf;   /* planets */
typedef struct { uint32_t img[AREA_W * AREA_H]; int key; uint32_t pal, gen; bool spr, valid; int nw, nh; unsigned used; } TownBuf;
static WorldBuf g_wb[2];
static TownBuf  g_tb[2];
static TownBuf *g_tb_last;                      /* debug_ppm */
static unsigned g_use_clock;
static uint32_t g_gen;
static bool     g_used_spr;                     /* set by draw_screen */
static void invalidate_pictures(void) { for (int i = 0; i < 2; i++) { g_wb[i].valid = false; g_tb[i].valid = false; } }
static uint32_t g_pal_hash;                     /* hash of the sprite palette (CRAM 16..31) on the map */
static int      g_view;                        /* HUD page: 0 area, 1 whole planet (test: PS_MAP_VIEW=1) */

static long bank_off(int bank, unsigned addr) { return (long)bank * 0x4000 + (long)(addr - 0x8000); }

static long find(const uint8_t *pat, size_t n, size_t lim)
{
    for (size_t i = 0; i + n <= lim && i + n <= g_rom_n; i++) if (!memcmp(g_rom + i, pat, n)) return (long)i;
    return -1;
}

static void decode_tiles(int which)
{
    if (g_tiles_ok[which] || g_tile_src[which] < 0 || g_tiles_from_vram) return;
    long o = g_tile_src[which];
    memset(g_tiles[which], 0, sizeof g_tiles[which]);
    for (int plane = 0; plane < 4; plane++) {
        int a = plane;
        for (;;) {
            if (o >= (long)g_rom_n) return;
            uint8_t c = g_rom[o++];
            if (!c) break;
            int n = c & 0x7F;
            if (c & 0x80) { for (int k = 0; k < n; k++, a += 4) { if (a < 0x4000) g_tiles[which][a] = g_rom[o]; o++; } }
            else { uint8_t b = g_rom[o++]; for (int k = 0; k < n; k++, a += 4) if (a < 0x4000) g_tiles[which][a] = b; }
        }
    }
    g_tiles_ok[which] = true;
}

void ps_worldmap_init(void)
{
    g_rom = glue_rom_image(&g_rom_n);
    g_area.valid = false; invalidate_pictures();
    { const char *v = getenv("PS_MAP_VIEW"); g_view = v ? atoi(v) & 1 : 0; }
    g_tiles_ok[0] = g_tiles_ok[1] = false; g_tiles_from_vram = false;
    if (!g_rom || g_rom_n < 0x10000) return;
    /* GameMode_LoadMap: ld hl,LABEL_DC1 ; add hl,de ; ld a,(hl) ; ld ($C308),a ; add a,a ; ld h,a ;
       add a,a ; ld l,a ; add a,a ; add a,l ; add a,h ; ld l,a ; ld h,0 ; ld de,LABEL_DF1 */
    static const uint8_t p1[] = { 0x19, 0x7E, 0x32, 0x08, 0xC3, 0x87, 0x67, 0x87, 0x6F, 0x87, 0x85, 0x84, 0x6F, 0x26, 0x00, 0x11 };
    long i = find(p1, sizeof p1, 0x8000);
    if (i >= 0) g_df1 = g_rom[i + sizeof p1] | (g_rom[i + sizeof p1 + 1] << 8);
    if (i >= 3 && g_rom[i - 3] == 0x21) g_dc1 = g_rom[i - 2] | (g_rom[i - 1] << 8);   /* ld hl,LABEL_DC1 just before */
    /* ld a,($C308) ; cp 4 ; jr nc,+ ; ld hl,$FFFF ; ld (hl),bank ; ld hl,tiles ; ld de,$4000 ; call ; jr ++
       + ld hl,$FFFF ; ld (hl),bank ; ld hl,tiles ; ld de,$4000 */
    static const uint8_t p2[] = { 0x3A, 0x08, 0xC3, 0xFE, 0x04, 0x30 };
    long j = find(p2, sizeof p2, 0x8000);
    if (j >= 0) {
        const uint8_t *a = g_rom + j + 7;
        for (int k = 0; k < 2; k++, a += 16)              /* second block follows call + jr (16 bytes) */
            if (a[0] == 0x21 && a[1] == 0xFF && a[2] == 0xFF && a[3] == 0x36 && a[5] == 0x21 && a[8] == 0x11 && a[10] == 0x40) {
                g_tile_src[k] = bank_off(a[4], (unsigned)(a[6] | (a[7] << 8)));
                /* the call must go to the standard unpacker (LABEL_3FA: ld b,4 ; push bc ; push de ;
                   call ; pop de ; inc de ; pop bc ; djnz ; ret), else take the tiles from VRAM */
                unsigned t = (unsigned)(a[12] | (a[13] << 8));
                static const uint8_t u1[] = { 0x06, 0x04, 0xC5, 0xD5, 0xCD }, u2[] = { 0xD1, 0x13, 0xC1, 0x10, 0xF6, 0xC9 };
                if (a[11] != 0xCD || t + 11 > g_rom_n || memcmp(g_rom + t, u1, sizeof u1) || memcmp(g_rom + t + 7, u2, sizeof u2))
                    g_tiles_from_vram = true;
            }
    }
    if (g_df1 < 0 || g_tile_src[0] < 0 || g_tile_src[1] < 0)
        fprintf(stderr, "[worldmap] map tables not found (df1 %ld, tiles %ld/%ld): no surroundings map\n",
                g_df1, g_tile_src[0], g_tile_src[1]);
    else if (getenv("PS_WORLDMAP_DEBUG"))
        fprintf(stderr, "[worldmap] LABEL_DF1 %04lX, tiles %05lX / %05lX%s\n", g_df1, g_tile_src[0], g_tile_src[1],
                g_tiles_from_vram ? " (other packer: tiles copied from VRAM)" : "");
}

/* the current map state; only while the game is on a map (overworld / town) */
void ps_worldmap_frame(void)
{
    static int in_map;                                   /* consecutive frames in map mode */
    uint8_t mode = sms_read8(RAM_GAME_MODE);
    in_map = mode == MODE_MAP ? in_map + 1 : 0;
    if (g_df1 < 0 || mode != MODE_MAP) return;
    /* GameMode_LoadMap sets the mode to $09 first and unpacks the tiles during its fade-out
       (several frames), so copy them once the map has been running for a moment */
    if (g_tiles_from_vram && in_map == 30) {
        int which = sms_read8(0xC308) < 4 ? 0 : 1;
        memcpy(g_tiles[which], g_vdp.vram, sizeof g_tiles[which]);
        g_tiles_ok[which] = true; invalidate_pictures();
    }
    int cam_x = sms_read8(0xC302) * SCR_W + sms_read8(0xC301);
    int cam_y = sms_read8(0xC306) * SCR_H + sms_read8(0xC305);
    /* the party leader's object ($C800, LABEL_576A): +2 = screen y, +4 = screen x of the 16 x 16 map
       cell it stands on (the sprite is drawn 8 px higher). Not the sprite table: sprites are parked
       off-screen during fades and some menus. */
    int sx = 128, sy = 96;
    if (sms_read8(0xC800) & 0x7F) { sx = sms_read8(0xC804); sy = sms_read8(0xC802); }
    g_area.tileset = sms_read8(0xC308);
    g_area.map = sms_read8(0xC309);
    g_area.px = cam_x + sx + 8;                          /* centre of that cell */
    g_area.py = cam_y + sy + 8;
    /* sprite palette (BG tiles may use it): the map fades in by stepping CRAM every 4 frames (until
       ~76 frames after LoadMap), so take it only once it has been unchanged for 16 frames */
    {
        static uint8_t cand[16]; static int stable;
        if (memcmp(cand, g_vdp.cram + 16, 16)) { memcpy(cand, g_vdp.cram + 16, 16); stable = 0; }
        else if (stable < 1000) stable++;
        if (stable >= 16 && memcmp(g_area.spr_pal, cand, 16)) {
            memcpy(g_area.spr_pal, cand, 16);
            uint32_t hsh = 2166136261u; for (int k = 0; k < 16; k++) hsh = (hsh ^ cand[k]) * 16777619u;
            g_pal_hash = hsh;
        }
    }
    g_area.valid = true;
    {   /* test: PS_WORLDMAP_SELFTEST=1 renders the map every frame (soak tests run without the HUD) */
        static int st = -1; if (st < 0) st = getenv("PS_WORLDMAP_SELFTEST") != NULL;
        static uint32_t scratch[64 * 48];
        if (st) ps_worldmap_draw(scratch, 64, 48, 0, 0, 64, 48, -1);
    }
}

static uint32_t sms_rgb(uint8_t c) { return (uint32_t)(c & 3) * 85u << 16 | (uint32_t)(c >> 2 & 3) * 85u << 8 | (uint32_t)(c >> 4 & 3) * 85u; }

/* unpack screen (sx, sy) of the tileset into 192 metatile numbers; false if the data looks wrong */
static bool unpack_screen(const uint8_t *e, int sx, int sy, uint8_t out[192])
{
    long tbl = bank_off(e[2], (unsigned)(e[0] | (e[1] << 8)));
    long o = tbl + ((sy & 7) * 9 + (sx & 7)) * 2;
    if (o + 1 >= (long)g_rom_n) return false;
    unsigned ptr = g_rom[o] | (g_rom[o + 1] << 8);
    if (ptr < 0x8000 || ptr >= 0xC000) return false;
    long p = bank_off(e[2], ptr);
    int n = 0;
    memset(out, 0, 192);
    while (p < (long)g_rom_n) {
        uint8_t c = g_rom[p++];
        if (!c) break;
        if (c & 0x80) { for (int k = 0; k < (c & 0x7F); k++, p++) if (n < 192) out[n++] = g_rom[p]; }
        else { uint8_t b = g_rom[p++]; for (int k = 0; k < c; k++) if (n < 192) out[n++] = b; }
    }
    return n == 192;
}

/* palette + tiles + metatile base of a tileset */
typedef struct { const uint8_t *e, *tiles; uint32_t pal[32]; long mt_base; } TileSetView;
static void tileset_view(int tileset, TileSetView *v)
{
    v->e = g_rom + g_df1 + tileset * 14;
    int which = tileset < 4 ? 0 : 1;
    decode_tiles(which);
    v->tiles = g_tiles[which];
    long po = (long)(v->e[10] | (v->e[11] << 8));
    for (int k = 0; k < 16; k++) v->pal[k] = sms_rgb(po + k < (long)g_rom_n ? g_rom[po + k] : 0);
    for (int k = 0; k < 16; k++) v->pal[16 + k] = sms_rgb(g_area.spr_pal[k]);
    v->mt_base = bank_off(v->e[9], 0x8000);
}

/* draw screen (sx, sy) at full size into dst (stride in pixels) */
static void draw_screen(const TileSetView *v, int sx, int sy, uint32_t *dst, int stride)
{
    uint8_t scr[192];
    bool ok = unpack_screen(v->e, sx & 7, sy & 7, scr);
    for (int my = 0; my < 12; my++) for (int mx = 0; mx < 16; mx++) {
        long mo = v->mt_base + scr[my * 16 + mx] * 8;
        for (int q = 0; q < 4; q++) {
            uint32_t *o = dst + (my * 16 + (q >> 1) * 8) * stride + mx * 16 + (q & 1) * 8;
            if (!ok || mo + q * 2 + 1 >= (long)g_rom_n) {
                for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) o[y * stride + x] = 0;
                continue;
            }
            uint8_t lo = g_rom[mo + q * 2], hi = g_rom[mo + q * 2 + 1];
            int tile = lo | (hi & 1) << 8, pb = (hi & 8) ? 16 : 0;
            if (pb) g_used_spr = true;
            const uint8_t *t = v->tiles + tile * 32;
            for (int y = 0; y < 8; y++) {
                const uint8_t *row = t + ((hi & 4) ? 7 - y : y) * 4;
                for (int x = 0; x < 8; x++) {
                    int b = (hi & 2) ? x : 7 - x;
                    int c = ((row[0] >> b) & 1) | ((row[1] >> b) & 1) << 1 | ((row[2] >> b) & 1) << 2 | ((row[3] >> b) & 1) << 3;
                    o[y * stride + x] = v->pal[pb + c];
                }
            }
        }
    }
}

/* towns: render nw x nh screens (<= 3 x 3) with top-left screen (osx, osy) into t */
static void render_area(TownBuf *t, int tileset, int osx, int osy, int nw, int nh)
{
    TileSetView v; tileset_view(tileset, &v);
    for (int gy = 0; gy < nh; gy++) for (int gx = 0; gx < nw; gx++)
        draw_screen(&v, osx + gx, osy + gy, t->img + gy * SCR_H * AREA_W + gx * SCR_W, AREA_W);
    t->gen = ++g_gen;
}

/* planets: the whole planet (8 x 8 screens = 2048 x 1536) at full size, rendered once per tileset
   and sprite palette (a few ms); the surroundings view is a moving window into it (it wraps) */
static WorldBuf *ensure_world(void)
{
    if (!g_area.valid || g_df1 < 0 || g_area.tileset >= 4) return NULL;
    if (g_tiles_from_vram && !g_tiles_ok[0]) return NULL;
    WorldBuf *b = NULL;
    for (int i = 0; i < 2; i++) if (g_wb[i].valid && g_wb[i].key >> 24 == g_area.tileset) b = &g_wb[i];
    /* key: tileset, plus the sprite palette only if a tile of this planet uses it */
    if (!b || (b->spr && b->key != ((uint32_t)g_area.tileset << 24 ^ (g_pal_hash & 0xFFFFFF)))) {
        if (!b) b = !g_wb[0].valid ? &g_wb[0] : !g_wb[1].valid ? &g_wb[1] : g_wb[0].used < g_wb[1].used ? &g_wb[0] : &g_wb[1];
        if (!b->px) b->px = malloc((size_t)WORLD_W * WORLD_H * sizeof *b->px);
        if (!b->px) return NULL;
        g_used_spr = false;
        static int dbg = -1; if (dbg < 0) dbg = getenv("PS_WORLDMAP_DEBUG") != NULL;
        clock_t t0 = clock();
        TileSetView v; tileset_view(g_area.tileset, &v);
        for (int sy = 0; sy < 8; sy++) for (int sx = 0; sx < 8; sx++)
            draw_screen(&v, sx, sy, b->px + sy * SCR_H * WORLD_W + sx * SCR_W, WORLD_W);
        b->spr = g_used_spr;
        b->key = (uint32_t)g_area.tileset << 24 ^ (b->spr ? g_pal_hash & 0xFFFFFF : 0);
        b->gen = ++g_gen; b->valid = true;
        if (dbg) fprintf(stderr, "[worldmap] planet %d rendered in %ld ms\n", g_area.tileset, (long)((clock() - t0) * 1000 / CLOCKS_PER_SEC));
    }
    b->used = ++g_use_clock;
    return b;
}


/* screen table of the current tileset as 8 x 8 pointers; returns the most common one (filler) */
static unsigned screen_table(unsigned ptr[64])
{
    const uint8_t *e = g_rom + g_df1 + g_area.tileset * 14;
    long tbl = bank_off(e[2], (unsigned)(e[0] | (e[1] << 8)));
    unsigned filler = 0; int best = -1;
    for (int i = 0; i < 64; i++) {
        long o = tbl + ((i >> 3) * 9 + (i & 7)) * 2;
        ptr[i] = o + 1 < (long)g_rom_n ? (unsigned)(g_rom[o] | (g_rom[o + 1] << 8)) : 0;
    }
    for (int i = 0; i < 64; i++) { int n = 0; for (int k = 0; k < 64; k++) n += ptr[k] == ptr[i]; if (n > best) { best = n; filler = ptr[i]; } }
    return filler;
}

/* render (or find) the town around the party; returns its buffer and the party position in it */
static TownBuf *prepare(int *rx, int *ry)
{
    if (!g_area.valid || g_df1 < 0) return NULL;
    if (g_tiles_from_vram && !g_tiles_ok[g_area.tileset < 4 ? 0 : 1]) return NULL;
    int px = ((g_area.px % 2048) + 2048) % 2048, py = ((g_area.py % 1536) + 1536) % 1536;
    int csx = px / SCR_W, csy = py / SCR_H;
    int osx = csx - 1, osy = csy - 1, nw = 3, nh = 3;    /* planets: 3 x 3 centred on the party's screen */
    if (g_area.tileset >= 4) {
        /* towns: a few screens in a table full of one filler screen (several towns share a table).
           Show the town = the screens connected to the party's screen, at most 3 x 3 around it. */
        unsigned ptr[64], filler = screen_table(ptr);
        bool in[3][3] = { { false } };
        if (ptr[csy * 8 + csx] != filler) {
            in[1][1] = true;
            for (int pass = 0; pass < 4; pass++)                  /* grow within the 3 x 3 box */
                for (int gy = 0; gy < 3; gy++) for (int gx = 0; gx < 3; gx++) {
                    if (in[gy][gx] || ptr[((csy + gy - 1) & 7) * 8 + ((csx + gx - 1) & 7)] == filler) continue;
                    if ((gx > 0 && in[gy][gx - 1]) || (gx < 2 && in[gy][gx + 1]) || (gy > 0 && in[gy - 1][gx]) || (gy < 2 && in[gy + 1][gx]))
                        in[gy][gx] = true;
                }
            int x0 = 3, x1 = -1, y0 = 3, y1 = -1;
            for (int gy = 0; gy < 3; gy++) for (int gx = 0; gx < 3; gx++) if (in[gy][gx]) {
                if (gx < x0) x0 = gx; if (gx > x1) x1 = gx; if (gy < y0) y0 = gy; if (gy > y1) y1 = gy;
            }
            osx = csx - 1 + x0; osy = csy - 1 + y0; nw = x1 - x0 + 1; nh = y1 - y0 + 1;
        }
    }
    int key = g_area.tileset << 16 | (osy & 7) << 12 | (osx & 7) << 8 | nh << 4 | nw;
    TownBuf *t = NULL;
    for (int i = 0; i < 2; i++) if (g_tb[i].valid && g_tb[i].key == key) t = &g_tb[i];
    if (!t || (t->spr && t->pal != g_pal_hash)) {
        if (!t) t = !g_tb[0].valid ? &g_tb[0] : !g_tb[1].valid ? &g_tb[1] : g_tb[0].used < g_tb[1].used ? &g_tb[0] : &g_tb[1];
        g_used_spr = false;
        render_area(t, g_area.tileset, osx, osy, nw, nh);
        t->key = key; t->pal = g_pal_hash; t->spr = g_used_spr; t->nw = nw; t->nh = nh; t->valid = true;
    }
    t->used = ++g_use_clock; g_tb_last = t;
    *rx = (csx - osx) * SCR_W + px % SCR_W; *ry = (csy - osy) * SCR_H + py % SCR_H;
    return t;
}

bool ps_worldmap_valid(void) { return g_area.valid && g_df1 >= 0; }

/* debug: write the full-size render as a PPM (PS_WORLDMAP_PPM=<file>, written when the map is first drawn) */
static void debug_ppm(void)
{
    const char *path = getenv("PS_WORLDMAP_PPM");
    if (!path || !path[0]) return;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", AREA_W, AREA_H);
    const uint32_t *img = g_tb_last ? g_tb_last->img : NULL;
    for (int i = 0; i < AREA_W * AREA_H; i++) { uint32_t p = img ? img[i] : 0; uint8_t c[3] = { (uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p }; fwrite(c, 1, 3, f); }
    fclose(f);
}

const char *ps_worldmap_area_name(void)
{
    switch (g_area.tileset) {
    case 0: return "PALMA";
    case 1: return "MOTAVIA";
    case 2: case 3: return "DEZORIS";
    default: return "TOWN";
    }
}

/* Fit src (sw x sh, stride) into the box keeping the aspect (centred): area average when scaling
   down, nearest when scaling up; source lines at multiples of (gx, gy) are darkened (screen grid,
   0 = none). Returns the destination rectangle. */

/* ---- drawing ----
 * A view = a source rectangle of a planet picture (wrapping) or a town picture plus the party position in it. With
 * the SDL window (hud_hires()) the picture is drawn by the host at the window's real resolution
 * (hud_image); otherwise (headless tests, GPU path) it is area-averaged into the ARGB buffer. */
typedef struct { const uint32_t *img; int iw, ih, sx, sy, sw, sh; bool wrap; uint32_t gen; int mx, my; } View;

static int wrap_x(int x) { return ((x % WORLD_W) + WORLD_W) % WORLD_W; }
static int wrap_y(int y) { return ((y % WORLD_H) + WORLD_H) % WORLD_H; }

/* the surroundings: planets = 3 screens wide, as tall as the box allows (2..8 screens), centred on
   the party and moving with it; towns = the whole town (<= 3 x 3 screens) */
static bool view_area(int bw, int bh, View *v)
{
    if (!g_area.valid || g_df1 < 0) return false;
    if (g_area.tileset < 4) {
        WorldBuf *b = ensure_world();
        if (!b) return false;
        int vw = 3 * SCR_W, vh = (int)((long)vw * bh / bw);
        if (vh < 2 * SCR_H) vh = 2 * SCR_H;
        if (vh > WORLD_H) vh = WORLD_H;
        int px = wrap_x(g_area.px), py = wrap_y(g_area.py);
        *v = (View){ b->px, WORLD_W, WORLD_H, px - vw / 2, py - vh / 2, vw, vh, true, b->gen, vw / 2, vh / 2 };
        return true;
    }
    int rx, ry;
    TownBuf *t = prepare(&rx, &ry);
    if (!t) return false;
    *v = (View){ t->img, AREA_W, AREA_H, 0, 0, t->nw * SCR_W, t->nh * SCR_H, false, t->gen, rx, ry };
    return true;
}

static bool view_planet(View *v)
{
    WorldBuf *b = ensure_world();
    if (!b) return false;
    *v = (View){ b->px, WORLD_W, WORLD_H, 0, 0, WORLD_W, WORLD_H, false, b->gen, wrap_x(g_area.px), wrap_y(g_area.py) };
    return true;
}

/* software path: area average (enlarging = nearest) of the view into (mx, my, dw, dh) */
static void sample_view(uint32_t *fb, int w, int h, int mx, int my, int dw, int dh, const View *v)
{
    for (int y = 0; y < dh; y++) {
        int yy = my + y; if (yy < 0 || yy >= h) continue;
        int y0 = (int)((long)y * v->sh / dh), y1 = (int)((long)(y + 1) * v->sh / dh); if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dw; x++) {
            int xx = mx + x; if (xx < 0 || xx >= w) continue;
            int x0 = (int)((long)x * v->sw / dw), x1 = (int)((long)(x + 1) * v->sw / dw); if (x1 <= x0) x1 = x0 + 1;
            unsigned r = 0, g = 0, b = 0, n = 0;
            for (int sy = y0; sy < y1; sy++) {
                int iy = v->sy + sy; iy = v->wrap ? ((iy % v->ih) + v->ih) % v->ih : iy;
                if (iy < 0 || iy >= v->ih) continue;
                for (int sx = x0; sx < x1; sx++) {
                    int ix = v->sx + sx; ix = v->wrap ? ((ix % v->iw) + v->iw) % v->iw : ix;
                    if (ix < 0 || ix >= v->iw) continue;
                    uint32_t c = v->img[iy * v->iw + ix];
                    r += c >> 16 & 255; g += c >> 8 & 255; b += c & 255; n++;
                }
            }
            if (n) { r /= n; g /= n; b /= n; }
            fb[yy * w + xx] = 0xFF000000u | r << 16 | g << 8 | b;
        }
    }
}

/* Fit the view into the box (keeping its aspect, centred), optional screen grid (8 x 8, planet
   view), party marker (red square, rim blinking white / black). space = HUD_SPACE_* of the buffer,
   or -1 = always software. */
static void draw_view(uint32_t *fb, int w, int h, int bx, int by, int bw, int bh, int space, const View *v, bool grid)
{
    static unsigned calls;
    uint32_t rim = (calls++ / 20) & 1 ? 0xFF000000u : 0xFFFFFFFFu;
    float dw = (float)bw, dh = (float)bw * v->sh / v->sw;
    if (dh > bh) { dh = (float)bh; dw = (float)bh * v->sw / v->sh; }
    float mx = bx + (bw - dw) / 2, my = by + (bh - dh) / 2;
    float cx = mx + v->mx * dw / v->sw, cy = my + v->my * dh / v->sh, r = dw / 70;
    if (space >= 0 && hud_hires()) {
        hud_fill(fb, w, h, (int)mx, (int)my, (int)(mx + dw), (int)(my + dh), 0xFF000000u, false);
        hud_image(space, mx, my, dw, dh, v->img, v->iw, v->ih, v->sx, v->sy, v->sw, v->sh, v->wrap, v->gen);
        if (grid) {
            float lw = space == HUD_SPACE_GAME ? 0.34f : 1.0f;
            for (int k = 1; k < 8; k++) {
                hud_rect(space, mx + dw * k / 8 - lw / 2, my, lw, dh, 0x60000000u);
                hud_rect(space, mx, my + dh * k / 8 - lw / 2, dw, lw, 0x60000000u);
            }
        }
        if (r < 2) r = 2;
        float e = r * 0.45f;
        hud_rect(space, cx - r - e, cy - r - e, 2 * (r + e), 2 * (r + e), rim);
        hud_rect(space, cx - r, cy - r, 2 * r, 2 * r, 0xFFEB3C3Cu);
        return;
    }
    int ix = (int)mx, iy = (int)my, iw = (int)dw, ih = (int)dh;
    sample_view(fb, w, h, ix, iy, iw, ih, v);
    if (grid) for (int k = 1; k < 8; k++) {
        hud_fill(fb, w, h, ix + iw * k / 8, iy, ix + iw * k / 8 + 1, iy + ih, 0xFF000000u, true);
        hud_fill(fb, w, h, ix, iy + ih * k / 8, ix + iw, iy + ih * k / 8 + 1, 0xFF000000u, true);
    }
    int ir = (int)r; if (ir < 2) ir = 2;
    hud_fill(fb, w, h, (int)cx - ir - 1, (int)cy - ir - 1, (int)cx + ir + 2, (int)cy + ir + 2, rim, false);
    hud_fill(fb, w, h, (int)cx - ir, (int)cy - ir, (int)cx + ir + 1, (int)cy + ir + 1, 0xFFEB3C3Cu, false);
}

/* Draw the surroundings map fitted into the box (bx, by, bw, bh) of an ARGB buffer (space =
   HUD_SPACE_GAME / HUD_SPACE_PANEL, -1 = software only), with the party marker. Returns false
   (nothing drawn) when there is no map yet. */
bool ps_worldmap_draw(uint32_t *fb, int w, int h, int bx, int by, int bw, int bh, int space)
{
    View v;
    if (bw < 8 || bh < 8 || !view_area(bw, bh, &v)) return false;
    static bool dumped; if (!dumped) { debug_ppm(); dumped = true; }
    draw_view(fb, w, h, bx, by, bw, bh, space, &v, false);
    return true;
}

/* Save-slot preview: the surroundings map of another position (tileset / map / party world pixels,
   e.g. from a save slot's camera), drawn like ps_worldmap_draw. *name gets the area name. */
bool ps_worldmap_draw_at(uint32_t *fb, int w, int h, int bx, int by, int bw, int bh, int space,
                         int tileset, int map, int px, int py, const char **name)
{
    if (g_df1 < 0 || tileset < 0 || tileset >= 32) return false;
    bool valid = g_area.valid; uint8_t ts = g_area.tileset, mp = g_area.map; int ox = g_area.px, oy = g_area.py;
    g_area.valid = true; g_area.tileset = (uint8_t)tileset; g_area.map = (uint8_t)map; g_area.px = px; g_area.py = py;
    if (name) *name = ps_worldmap_area_name();
    bool ok = ps_worldmap_draw(fb, w, h, bx, by, bw, bh, space);
    g_area.valid = valid; g_area.tileset = ts; g_area.map = mp; g_area.px = ox; g_area.py = oy;
    return ok;
}

/* The whole planet (planets only), faint screen grid, party marker. */
bool ps_worldmap_draw_planet(uint32_t *fb, int w, int h, int bx, int by, int bw, int bh, int space)
{
    View v;
    if (bw < 8 || bh < 8 || !view_planet(&v)) return false;
    draw_view(fb, w, h, bx, by, bw, bh, space, &v, true);
    return true;
}

void ps_worldmap_screen(int *sx, int *sy)
{
    *sx = wrap_x(g_area.px) / SCR_W;
    *sy = wrap_y(g_area.py) / SCR_H;
}

/* HUD page view: 0 = the screens around the party, 1 = the whole planet; Up/Down switch */
void ps_worldmap_hud_key(int key) { if (key == HUD_KEY_UP || key == HUD_KEY_DOWN) g_view ^= 1; }

/* tileset of a map number (LABEL_DC1; 0..3 planets, 4..25 towns), -1 if unknown */
#define N_MAPS 26
int ps_worldmap_tileset_of(int map)
{
    if (g_dc1 < 0 || map < 0 || map >= N_MAPS || g_dc1 + map >= (long)g_rom_n) return -1;
    return g_rom[g_dc1 + map];
}
int ps_worldmap_current_map(void) { return g_area.valid ? g_area.map : -1; }
void ps_worldmap_current_pos(int *px, int *py) { *px = g_area.px; *py = g_area.py; }

/* HUD page: the map in a 4:3 box, an info column to the right: `title`, the screen (here) or "LAST
   VISIT", the zoom hint (planets), then the caller's lines (nav hints) */
static void hud_draw_impl(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, bool here,
                          const char *title, const char *const *lines, int nlines)
{
    const uint32_t GREY = 0xFFA0A0B0, CYAN = 0xFF60D0FF, WHITE = 0xFFFFFFFF, YEL = 0xFFFFD040;
    int bh = y1 - y0 - 2, bw = bh * 4 / 3;
    if (bw > x1 - x0 - 74) { bw = x1 - x0 - 74; bh = bw * 3 / 4; }
    int bx = x0 + 2, by = y0 + 1;
    hud_fill(fb, w, h, bx - 1, by - 1, bx + bw + 1, by + bh + 1, 0xFF0C0C12u, false);
    bool planet = g_view == 1 && g_area.valid && g_area.tileset < 4;
    bool ok = planet ? ps_worldmap_draw_planet(fb, w, h, bx, by, bw, bh, HUD_SPACE_GAME)
                     : ps_worldmap_draw(fb, w, h, bx, by, bw, bh, HUD_SPACE_GAME);
    int tx = bx + bw + 6, ty = by;
    if (!ok) {
        hud_text(fb, w, h, bx + 6, by + 4, "NO MAP YET", GREY);
        hud_text(fb, w, h, bx + 6, by + 18, "SHOWN ONCE YOU ARE ON", GREY);
        hud_text(fb, w, h, bx + 6, by + 28, "A PLANET OR IN A TOWN.", GREY);
    } else {
        hud_text(fb, w, h, tx, ty, title ? title : ps_worldmap_area_name(), CYAN); ty += 12;
        if (here) {
            char buf[16]; int sx, sy;
            ps_worldmap_screen(&sx, &sy);
            snprintf(buf, sizeof buf, "%d,%d", sx, sy);
            hud_text(fb, w, h, tx, ty, "SCREEN", GREY); ty += 9;
            hud_text(fb, w, h, tx, ty, buf, WHITE); ty += 14;
        } else { hud_text(fb, w, h, tx, ty, "LAST VISIT", GREY); ty += 14; }
        if (g_area.tileset < 4) {
            hud_text(fb, w, h, tx, ty, planet ? "PLANET" : "AREA", YEL); ty += 9;
            hud_text(fb, w, h, tx, ty, "UP/DOWN", GREY); ty += 9;
            hud_text(fb, w, h, tx, ty, planet ? "= AREA" : "= PLANET", GREY); ty += 14;
        }
    }
    if (!ok) ty = by + 44;
    for (int i = 0; i < nlines; i++, ty += 9) hud_text(fb, w, h, ok ? tx : bx + 6, ty, lines[i], GREY);
}

void ps_worldmap_hud_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1)
{
    hud_draw_impl(fb, w, h, x0, y0, x1, y1, true, NULL, NULL, 0);
}

/* HUD MAP page for any visited map (planet / town) with the party's last position there; `here` =
   it is the map the party is on (then the live state is used) */
void ps_worldmap_hud_draw_place(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, int map,
                                int px, int py, bool here, const char *title, const char *const *lines, int nlines)
{
    if (here) { hud_draw_impl(fb, w, h, x0, y0, x1, y1, true, title, lines, nlines); return; }
    int ts = ps_worldmap_tileset_of(map);
    bool valid = g_area.valid; uint8_t ots = g_area.tileset, omp = g_area.map; int ox = g_area.px, oy = g_area.py;
    if (ts >= 0) { g_area.valid = true; g_area.tileset = (uint8_t)ts; g_area.map = (uint8_t)map; g_area.px = px; g_area.py = py; }
    else g_area.valid = false;
    hud_draw_impl(fb, w, h, x0, y0, x1, y1, false, title, lines, nlines);
    g_area.valid = valid; g_area.tileset = ots; g_area.map = omp; g_area.px = ox; g_area.py = oy;
}
