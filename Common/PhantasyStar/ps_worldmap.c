/*
 * ps_worldmap.c - Phantasy Star (SMS) "surroundings" map for the overworld and towns: a window
 * 3 screens wide centred on the party (moving with it), or the whole planet / town, drawn from the
 * ROM's own map data. Also during the tube walks (GameMode_Road $0F, a scripted walk on a planet).
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
#define MODE_ROAD     0x0F   /* GameMode_Road: the scripted tube walk (LABEL_F0C routes) on a planet map */

#define SCR_W 256
#define SCR_H 192

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
static void dump_all(void);

/* Rendered pictures: a tileset's whole 8 x 8 screen table at full size (2048 x 1536), rendered once
   per tileset (a few ms). Planets wrap. Towns: several towns share one table, so a town picture is
   per town (= group of connected screens, town_clusters) and the screens of the others are dimmed.
   Three buffers (the side panel shows the current map while the HUD's MAP page or the save-slot
   preview may show another one: with fewer they would re-render each other every frame); least
   recently used is replaced. `gen` changes with every render (hi-res texture uploads). */
#define WORLD_W (8 * SCR_W)
#define WORLD_H (8 * SCR_H)
#define N_PICS 3
typedef struct {
    uint32_t *px; int ts, cl; uint32_t pal, gen; bool spr, valid; unsigned used;
    int x0, y0, x1, y1;                         /* the town's screens (inclusive); planets: 0..7 */
} Pic;
static Pic      g_pic[N_PICS];
static Pic     *g_pic_last;                     /* debug_ppm */
static unsigned g_use_clock;
static uint32_t g_gen;
static bool     g_used_spr;                     /* set by draw_screen */
static void invalidate_pictures(void) { for (int i = 0; i < N_PICS; i++) g_pic[i].valid = false; }
static uint32_t g_pal_hash;                     /* hash of the sprite palette (CRAM 16..31) on the map */
static int      g_view;                        /* HUD page: 0 area, 1 whole planet / town (test: PS_MAP_VIEW=1) */
static int town_clusters(int tileset, int cl[64]);

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
    dump_all();
}

/* the current map state; only while the game is on a map (overworld / town) */
void ps_worldmap_frame(void)
{
    static int in_map;                                   /* consecutive frames in map mode */
    uint8_t mode = sms_read8(RAM_GAME_MODE);
    /* the tube walks (GameMode_LoadRoad -> LoadMap) run on a planet map with the normal camera and
       party object, only the input is scripted */
    bool on_map = mode == MODE_MAP || mode == MODE_ROAD;
    in_map = on_map ? in_map + 1 : 0;
    if (g_df1 < 0 || !on_map) return;
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

/* The picture of the current area (g_area): planets = the whole planet; towns = the whole screen
   table with every screen outside the party's town dimmed (other towns sharing the table, filler).
   Rendered once per tileset / town and sprite palette (if a tile uses it); NULL if not possible. */
static Pic *ensure_pic(void)
{
    if (!g_area.valid || g_df1 < 0) return NULL;
    int ts = g_area.tileset;
    if (g_tiles_from_vram && !g_tiles_ok[ts < 4 ? 0 : 1]) return NULL;
    int cl[64], c = 0;
    if (ts >= 4) {
        int px = ((g_area.px % WORLD_W) + WORLD_W) % WORLD_W, py = ((g_area.py % WORLD_H) + WORLD_H) % WORLD_H;
        town_clusters(ts, cl);
        c = cl[(py / SCR_H) * 8 + px / SCR_W];
    }
    Pic *b = NULL;
    for (int i = 0; i < N_PICS; i++)
        if (g_pic[i].valid && g_pic[i].ts == ts && g_pic[i].cl == c && (!g_pic[i].spr || g_pic[i].pal == g_pal_hash)) b = &g_pic[i];
    if (!b) {
        for (int i = 0; i < N_PICS && !b; i++) if (!g_pic[i].valid) b = &g_pic[i];     /* free one, else LRU */
        if (!b) { b = &g_pic[0]; for (int i = 1; i < N_PICS; i++) if (g_pic[i].used < b->used) b = &g_pic[i]; }
        if (!b->px) b->px = malloc((size_t)WORLD_W * WORLD_H * sizeof *b->px);
        if (!b->px) return NULL;
        g_used_spr = false;
        static int dbg = -1; if (dbg < 0) dbg = getenv("PS_WORLDMAP_DEBUG") != NULL;
        clock_t t0 = clock();
        TileSetView v; tileset_view(ts, &v);
        b->x0 = 0; b->y0 = 0; b->x1 = 7; b->y1 = 7;
        if (c) { b->x0 = b->y0 = 8; b->x1 = b->y1 = -1; }
        for (int sy = 0; sy < 8; sy++) for (int sx = 0; sx < 8; sx++) {
            uint32_t *o = b->px + sy * SCR_H * WORLD_W + sx * SCR_W;
            if (c && !cl[sy * 8 + sx]) {      /* filler (no town): the game never shows it; its colour
                                                 is sprite colour 0, which differs per town -> black */
                for (int y = 0; y < SCR_H; y++) for (int x = 0; x < SCR_W; x++) o[y * WORLD_W + x] = 0;
                continue;
            }
            draw_screen(&v, sx, sy, o, WORLD_W);
            if (!c) continue;
            if (cl[sy * 8 + sx] == c) {
                if (sx < b->x0) b->x0 = sx; if (sx > b->x1) b->x1 = sx;
                if (sy < b->y0) b->y0 = sy; if (sy > b->y1) b->y1 = sy;
                continue;
            }
            for (int y = 0; y < SCR_H; y++) for (int x = 0; x < SCR_W; x++) {   /* not this town: 30 % */
                uint32_t p = o[y * WORLD_W + x];
                o[y * WORLD_W + x] = ((p >> 16 & 255) * 77 >> 8) << 16 | ((p >> 8 & 255) * 77 >> 8) << 8 | (p & 255) * 77 >> 8;
            }
        }
        b->spr = g_used_spr; b->pal = g_pal_hash; b->ts = ts; b->cl = c;
        b->gen = ++g_gen; b->valid = true;
        if (dbg) fprintf(stderr, "[worldmap] tileset %d town %d rendered in %ld ms\n", ts, c, (long)((clock() - t0) * 1000 / CLOCKS_PER_SEC));
    }
    b->used = ++g_use_clock; g_pic_last = b;
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

/* Towns: several towns share a tileset's 8 x 8 screen table, the rest of the table is one filler
   screen. A town = a group of connected (4-neighbour, no wrap) non-filler screens. cl[i] = group
   number of screen i (0 = filler), groups numbered in screen order; returns the number of groups. */
static int town_clusters(int tileset, int cl[64])
{
    uint8_t ots = g_area.tileset; g_area.tileset = (uint8_t)tileset;
    unsigned ptr[64], filler = screen_table(ptr);
    g_area.tileset = ots;
    int n = 0;
    for (int i = 0; i < 64; i++) cl[i] = 0;
    for (int i = 0; i < 64; i++) {
        if (cl[i] || ptr[i] == filler) continue;
        int stack[64], sp = 0; stack[sp++] = i; cl[i] = ++n;
        while (sp) {
            int s = stack[--sp], x = s & 7, y = s >> 3;
            const int nb[4] = { x > 0 ? s - 1 : -1, x < 7 ? s + 1 : -1, y > 0 ? s - 8 : -1, y < 7 ? s + 8 : -1 };
            for (int k = 0; k < 4; k++) if (nb[k] >= 0 && !cl[nb[k]] && ptr[nb[k]] != filler) { cl[nb[k]] = n; stack[sp++] = nb[k]; }
        }
    }
    return n;
}

/* debug: PS_WORLDMAP_DUMPALL=<dir> writes <dir>/ts<k>.ppm (the whole 8 x 8 table of every town tileset)
   and prints its town groups (first screen index of each group) */
static void dump_all(void)
{
    const char *dir = getenv("PS_WORLDMAP_DUMPALL");
    if (!dir || g_df1 < 0) return;
    uint32_t *buf = malloc((size_t)8 * SCR_W * 8 * SCR_H * sizeof *buf);
    if (!buf) return;
    for (int ts = 4; ts < 16; ts++) {
        bool used = false;
        for (int m = 0; m < 32 && g_dc1 >= 0; m++) if (g_dc1 + m < (long)g_rom_n && g_rom[g_dc1 + m] == ts && m < 26) used = true;
        if (!used) continue;
        TileSetView v; tileset_view(ts, &v);
        for (int sy = 0; sy < 8; sy++) for (int sx = 0; sx < 8; sx++)
            draw_screen(&v, sx, sy, buf + sy * SCR_H * 8 * SCR_W + sx * SCR_W, 8 * SCR_W);
        char path[512]; snprintf(path, sizeof path, "%s/ts%d.ppm", dir, ts);
        FILE *f = fopen(path, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", 8 * SCR_W, 8 * SCR_H);
            for (long i = 0; i < 64L * SCR_W * SCR_H; i++) { uint8_t c[3] = { (uint8_t)(buf[i] >> 16), (uint8_t)(buf[i] >> 8), (uint8_t)buf[i] }; fwrite(c, 1, 3, f); }
            fclose(f);
        }
        int cl[64], n = town_clusters(ts, cl);
        fprintf(stderr, "[worldmap] tileset %d: %d towns:", ts, n);
        for (int g = 1; g <= n; g++) { int x0 = 8, y0 = 8, x1 = -1, y1 = -1;
            for (int i = 0; i < 64; i++) if (cl[i] == g) { int x = i & 7, y = i >> 3; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
            fprintf(stderr, " [%d: screens x%d-%d y%d-%d]", g, x0, x1, y0, y1); }
        fprintf(stderr, "\n");
        for (int y = 0; y < 8; y++) {
            char row[9]; for (int x = 0; x < 8; x++) row[x] = cl[y * 8 + x] ? (char)('0' + cl[y * 8 + x]) : '.'; row[8] = 0;
            fprintf(stderr, "[worldmap]   %s\n", row);
        }
    }
    free(buf);
}

bool ps_worldmap_valid(void) { return g_area.valid && g_df1 >= 0; }

/* debug: write the full-size picture as a PPM (PS_WORLDMAP_PPM=<file>, written when the map is first drawn) */
static void debug_ppm(void)
{
    const char *path = getenv("PS_WORLDMAP_PPM");
    if (!path || !path[0]) return;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", WORLD_W, WORLD_H);
    const uint32_t *img = g_pic_last ? g_pic_last->px : NULL;
    for (long i = 0; i < (long)WORLD_W * WORLD_H; i++) { uint32_t p = img ? img[i] : 0; uint8_t c[3] = { (uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p }; fwrite(c, 1, 3, f); }
    fclose(f);
}

/* Town names by tileset and screen rectangle. Several towns share a tileset's screen table (and the
   map numbers that use it), so a town is identified by where the party is. Matched 2026-11-14 by
   rendering every town tileset (PS_WORLDMAP_DUMPALL) against the pscave.com town maps (Rev A).
   Corridor screens between two places (tubes) belong to the nearest one. */
static const struct { uint8_t ts, x0, y0, x1, y1; const char *name; } k_towns[] = {
    { 4, 1, 1, 2, 2, "Palma Spaceport" }, { 4, 5, 1, 6, 2, "Camineet" }, { 4, 1, 5, 2, 6, "Parolit" },
    { 5, 1, 1, 2, 2, "Gothic" }, { 5, 4, 1, 4, 2, "Eppi" }, { 5, 6, 1, 6, 2, "Loar" },
    { 5, 1, 4, 2, 5, "Abion" }, { 5, 4, 4, 4, 5, "Bortevo" },
    { 6, 1, 1, 2, 2, "Drasgow" }, { 6, 4, 4, 6, 7, "Scion" },
    { 7, 1, 1, 2, 2, "Paseo" }, { 7, 1, 4, 2, 5, "Motavia Spaceport" }, { 7, 4, 1, 5, 2, "Uzo" },
    { 7, 4, 4, 5, 5, "Casba" }, { 7, 7, 4, 7, 4, "Sopia" },
    { 8, 1, 1, 1, 1, "Skure" }, { 8, 3, 1, 3, 1, "Twintown" },
    { 9, 1, 1, 2, 2, "Twintown" }, { 9, 5, 1, 6, 2, "Skure" },
    { 10, 1, 1, 2, 2, "Air Castle" },
};
static const char *town_name(int ts, int sx, int sy)
{
    const char *best = "Town"; int bd = 3;
    for (unsigned i = 0; i < sizeof k_towns / sizeof k_towns[0]; i++) {
        if (k_towns[i].ts != ts) continue;
        int dx = sx < k_towns[i].x0 ? k_towns[i].x0 - sx : sx > k_towns[i].x1 ? sx - k_towns[i].x1 : 0;
        int dy = sy < k_towns[i].y0 ? k_towns[i].y0 - sy : sy > k_towns[i].y1 ? sy - k_towns[i].y1 : 0;
        int d = dx > dy ? dx : dy;
        if (d < bd) { bd = d; best = k_towns[i].name; }
    }
    return best;
}

/* the name of the current area: planet, or the town the party is in */
const char *ps_worldmap_area_name(void)
{
    switch (g_area.tileset) {
    case 0: return "Palma";
    case 1: return "Motavia";
    case 2: case 3: return "Dezoris";
    default: {
        int px = ((g_area.px % 2048) + 2048) % 2048, py = ((g_area.py % 1536) + 1536) % 1536;
        return town_name(g_area.tileset, px / SCR_W, py / SCR_H);
    }
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

/* the surroundings (planets and towns): 3 screens wide, as tall as the box allows (2..8 screens),
   centred on the party and moving with it pixel by pixel */
static bool view_area(int bw, int bh, View *v)
{
    Pic *b = ensure_pic();
    if (!b) return false;
    int vw = 3 * SCR_W, vh = (int)((long)vw * bh / bw);
    if (vh < 2 * SCR_H) vh = 2 * SCR_H;
    if (vh > WORLD_H) vh = WORLD_H;
    int px = wrap_x(g_area.px), py = wrap_y(g_area.py);
    *v = (View){ b->px, WORLD_W, WORLD_H, px - vw / 2, py - vh / 2, vw, vh, true, b->gen, vw / 2, vh / 2 };
    return true;
}

/* the whole planet, or the whole town (its screens) */
static bool view_whole(View *v)
{
    Pic *b = ensure_pic();
    if (!b) return false;
    int sx = b->x0 * SCR_W, sy = b->y0 * SCR_H;
    *v = (View){ b->px, WORLD_W, WORLD_H, sx, sy, (b->x1 - b->x0 + 1) * SCR_W, (b->y1 - b->y0 + 1) * SCR_H, false, b->gen,
                 wrap_x(g_area.px) - sx, wrap_y(g_area.py) - sy };
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

/* The whole planet (faint screen grid) or the whole town, party marker. */
static bool draw_whole(uint32_t *fb, int w, int h, int bx, int by, int bw, int bh, int space)
{
    View v;
    if (bw < 8 || bh < 8 || !view_whole(&v)) return false;
    draw_view(fb, w, h, bx, by, bw, bh, space, &v, g_area.tileset < 4);
    return true;
}

void ps_worldmap_screen(int *sx, int *sy)
{
    *sx = wrap_x(g_area.px) / SCR_W;
    *sy = wrap_y(g_area.py) / SCR_H;
}

/* HUD page view: 0 = the surroundings of the party, 1 = the whole planet / town; Up/Down switch */
void ps_worldmap_hud_key(int key) { if (key == HUD_KEY_UP || key == HUD_KEY_DOWN) g_view ^= 1; }

/* tileset of a map number (LABEL_DC1; 0..3 planets, 4..23 towns), -1 if unknown */
#define N_MAPS 24                                   /* LABEL_DC1 has 24 entries (LABEL_DD9 follows) */
int ps_worldmap_tileset_of(int map)
{
    if (g_dc1 < 0 || map < 0 || map >= N_MAPS || g_dc1 + map >= (long)g_rom_n) return -1;
    return g_rom[g_dc1 + map];
}
int ps_worldmap_current_map(void) { return g_area.valid ? g_area.map : -1; }
void ps_worldmap_current_pos(int *px, int *py) { *px = g_area.px; *py = g_area.py; }

/* Places for the HUD MAP page list: 0..3 = planets (tilesets 0..3), 4 + i = town k_towns[i]. */
#define N_TOWNS ((int)(sizeof k_towns / sizeof k_towns[0]))
int ps_worldmap_current_place(void)
{
    if (!g_area.valid) return -1;
    if (g_area.tileset < 4) return g_area.tileset;
    int px = ((g_area.px % 2048) + 2048) % 2048, py = ((g_area.py % 1536) + 1536) % 1536;
    const char *nm = town_name(g_area.tileset, px / SCR_W, py / SCR_H);
    for (int i = 0; i < N_TOWNS; i++) if (k_towns[i].ts == g_area.tileset && k_towns[i].name == nm) return 4 + i;
    return -1;
}
int ps_worldmap_place_tileset(int place)
{
    if (place >= 0 && place < 4) return place;
    return place >= 4 && place < 4 + N_TOWNS ? k_towns[place - 4].ts : -1;
}
int ps_worldmap_num_places(void) { return 4 + N_TOWNS; }

/* HUD page: the map in a 4:3 box, an info column to the right: `title`, the screen (here) or "LAST
   VISIT", the zoom hint, then the caller's lines (nav hints) */
static void hud_draw_impl(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, bool here,
                          const char *title, const char *const *lines, int nlines)
{
    const uint32_t GREY = 0xFFA0A0B0, CYAN = 0xFF60D0FF, WHITE = 0xFFFFFFFF, YEL = 0xFFFFD040;
    int bh = y1 - y0 - 2, bw = bh * 4 / 3;
    if (bw > x1 - x0 - 74) { bw = x1 - x0 - 74; bh = bw * 3 / 4; }
    int bx = x0 + 2, by = y0 + 1;
    hud_fill(fb, w, h, bx - 1, by - 1, bx + bw + 1, by + bh + 1, 0xFF0C0C12u, false);
    bool whole = g_view == 1, planet = g_area.valid && g_area.tileset < 4;
    bool ok = whole ? draw_whole(fb, w, h, bx, by, bw, bh, HUD_SPACE_GAME)
                     : ps_worldmap_draw(fb, w, h, bx, by, bw, bh, HUD_SPACE_GAME);
    int tx = bx + bw + 6, ty = by;
    if (!ok) {
        hud_text(fb, w, h, bx + 6, by + 4, "NO MAP YET", GREY);
        hud_text(fb, w, h, bx + 6, by + 18, "SHOWN ONCE YOU ARE ON", GREY);
        hud_text(fb, w, h, bx + 6, by + 28, "A PLANET OR IN A TOWN.", GREY);
    } else {
        {   /* the name, wrapped at a space when it is wider than the column */
            const char *t = title ? title : ps_worldmap_area_name();
            int maxc = (x1 - tx) / 6; size_t n = strlen(t);
            if ((int)n > maxc && strchr(t, ' ')) {
                char a[40]; size_t k = (size_t)maxc < n ? (size_t)maxc : n;
                while (k > 0 && t[k] != ' ') k--;
                if (!k) k = strcspn(t, " ");
                snprintf(a, sizeof a, "%.*s", (int)k, t);
                hud_text(fb, w, h, tx, ty, a, CYAN); ty += 9;
                hud_text(fb, w, h, tx, ty, t + k + 1, CYAN); ty += 12;
            } else { hud_text(fb, w, h, tx, ty, t, CYAN); ty += 12; }
        }
        if (here) {
            char buf[16]; int sx, sy;
            ps_worldmap_screen(&sx, &sy);
            snprintf(buf, sizeof buf, "%d,%d", sx, sy);
            hud_text(fb, w, h, tx, ty, "SCREEN", GREY); ty += 9;
            hud_text(fb, w, h, tx, ty, buf, WHITE); ty += 14;
        } else { hud_text(fb, w, h, tx, ty, "LAST VISIT", GREY); ty += 14; }
        {
            const char *all = planet ? "PLANET" : "TOWN";
            hud_text(fb, w, h, tx, ty, whole ? all : "AREA", YEL); ty += 9;
            hud_text(fb, w, h, tx, ty, "UP/DOWN", GREY); ty += 9;
            char b2[16]; snprintf(b2, sizeof b2, "= %s", whole ? "AREA" : all);
            hud_text(fb, w, h, tx, ty, b2, GREY); ty += 14;
        }
    }
    if (!ok) ty = by + 44;
    for (int i = 0; i < nlines; i++, ty += 9) hud_text(fb, w, h, ok ? tx : bx + 6, ty, lines[i], GREY);
}

void ps_worldmap_hud_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1)
{
    hud_draw_impl(fb, w, h, x0, y0, x1, y1, true, NULL, NULL, 0);
}

/* HUD MAP page for any visited place (planet / town, see ps_worldmap_current_place) with the party's
   last position there; `here` = the party is there now (then the live state is used) */
void ps_worldmap_hud_draw_place(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, int place,
                                int px, int py, bool here, const char *title, const char *const *lines, int nlines)
{
    if (here) { hud_draw_impl(fb, w, h, x0, y0, x1, y1, true, title, lines, nlines); return; }
    int ts = ps_worldmap_place_tileset(place);
    bool valid = g_area.valid; uint8_t ots = g_area.tileset, omp = g_area.map; int ox = g_area.px, oy = g_area.py;
    if (ts >= 0) { g_area.valid = true; g_area.tileset = (uint8_t)ts; g_area.px = px; g_area.py = py; }
    else g_area.valid = false;
    hud_draw_impl(fb, w, h, x0, y0, x1, y1, false, title, lines, nlines);
    g_area.valid = valid; g_area.tileset = ots; g_area.map = omp; g_area.px = ox; g_area.py = oy;
}
