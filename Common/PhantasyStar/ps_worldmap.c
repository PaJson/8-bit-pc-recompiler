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

bool ps_worldmap_draw(uint32_t *fb, int w, int h, int bx, int by, int bw, int bh);

static uint32_t g_img[AREA_W * AREA_H];         /* rendered 3x3 screens */
static int      g_img_key = -1;

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
    g_area.valid = false; g_img_key = -1;
    g_tiles_ok[0] = g_tiles_ok[1] = false; g_tiles_from_vram = false;
    if (!g_rom || g_rom_n < 0x10000) return;
    /* GameMode_LoadMap: ld hl,LABEL_DC1 ; add hl,de ; ld a,(hl) ; ld ($C308),a ; add a,a ; ld h,a ;
       add a,a ; ld l,a ; add a,a ; add a,l ; add a,h ; ld l,a ; ld h,0 ; ld de,LABEL_DF1 */
    static const uint8_t p1[] = { 0x19, 0x7E, 0x32, 0x08, 0xC3, 0x87, 0x67, 0x87, 0x6F, 0x87, 0x85, 0x84, 0x6F, 0x26, 0x00, 0x11 };
    long i = find(p1, sizeof p1, 0x8000);
    if (i >= 0) g_df1 = g_rom[i + sizeof p1] | (g_rom[i + sizeof p1 + 1] << 8);
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
        g_tiles_ok[which] = true; g_img_key = -1;
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
    memcpy(g_area.spr_pal, g_vdp.cram + 16, 16);
    g_area.valid = true;
    {   /* test: PS_WORLDMAP_SELFTEST=1 renders the map every frame (soak tests run without the HUD) */
        static int st = -1; if (st < 0) st = getenv("PS_WORLDMAP_SELFTEST") != NULL;
        static uint32_t scratch[64 * 48];
        if (st) ps_worldmap_draw(scratch, 64, 48, 0, 0, 64, 48);
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

/* render nw x nh screens (<= 3 x 3) with top-left screen (osx, osy) into g_img */
static int g_nw = 3, g_nh = 3;
static void render_area(int tileset, int osx, int osy, int nw, int nh)
{
    const uint8_t *e = g_rom + g_df1 + tileset * 14;
    int which = tileset < 4 ? 0 : 1;
    decode_tiles(which);
    const uint8_t *tiles = g_tiles[which];
    uint32_t pal[32];
    long po = (long)(e[10] | (e[11] << 8));
    for (int k = 0; k < 16; k++) pal[k] = sms_rgb(po + k < (long)g_rom_n ? g_rom[po + k] : 0);
    for (int k = 0; k < 16; k++) pal[16 + k] = sms_rgb(g_area.spr_pal[k]);
    long mt_base = bank_off(e[9], 0x8000);
    for (int gy = 0; gy < nh; gy++) for (int gx = 0; gx < nw; gx++) {
        uint8_t scr[192];
        bool ok = unpack_screen(e, (osx + gx) & 7, (osy + gy) & 7, scr);
        for (int my = 0; my < 12; my++) for (int mx = 0; mx < 16; mx++) {
            long mo = mt_base + scr[my * 16 + mx] * 8;
            for (int q = 0; q < 4; q++) {
                int ox = gx * SCR_W + mx * 16 + (q & 1) * 8, oy = gy * SCR_H + my * 16 + (q >> 1) * 8;
                if (!ok || mo + q * 2 + 1 >= (long)g_rom_n) {
                    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) g_img[(oy + y) * AREA_W + ox + x] = 0;
                    continue;
                }
                uint8_t lo = g_rom[mo + q * 2], hi = g_rom[mo + q * 2 + 1];
                int tile = lo | (hi & 1) << 8, pb = (hi & 8) ? 16 : 0;
                const uint8_t *t = tiles + tile * 32;
                for (int y = 0; y < 8; y++) {
                    const uint8_t *row = t + ((hi & 4) ? 7 - y : y) * 4;
                    for (int x = 0; x < 8; x++) {
                        int b = (hi & 2) ? x : 7 - x;
                        int c = ((row[0] >> b) & 1) | ((row[1] >> b) & 1) << 1 | ((row[2] >> b) & 1) << 2 | ((row[3] >> b) & 1) << 3;
                        g_img[(oy + y) * AREA_W + ox + x] = pal[pb + c];
                    }
                }
            }
        }
    }
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

/* make sure g_img shows the area around the party; returns the party position inside g_img */
static bool prepare(int *rx, int *ry)
{
    if (!g_area.valid || g_df1 < 0) return false;
    if (g_tiles_from_vram && !g_tiles_ok[g_area.tileset < 4 ? 0 : 1]) return false;
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
    if (key != g_img_key) { render_area(g_area.tileset, osx, osy, nw, nh); g_img_key = key; g_nw = nw; g_nh = nh; }
    *rx = (csx - osx) * SCR_W + px % SCR_W; *ry = (csy - osy) * SCR_H + py % SCR_H;
    return true;
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
    for (int i = 0; i < AREA_W * AREA_H; i++) { uint8_t c[3] = { (uint8_t)(g_img[i] >> 16), (uint8_t)(g_img[i] >> 8), (uint8_t)g_img[i] }; fwrite(c, 1, 3, f); }
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

/* Draw the map fitted (aspect kept, centred) into the box (bx, by, bw, bh) of an ARGB buffer: area
   average when scaling down, nearest when scaling up, darkened screen boundaries, party marker.
   Returns false (nothing drawn) when there is no map yet. */
bool ps_worldmap_draw(uint32_t *fb, int w, int h, int bx, int by, int bw, int bh)
{
    int rx, ry;
    if (bw < 8 || bh < 8 || !prepare(&rx, &ry)) return false;
    static bool dumped; if (!dumped) { debug_ppm(); dumped = true; }
    int sw = g_nw * SCR_W, sh = g_nh * SCR_H, dw, dh;
    if ((long)sw * bh > (long)bw * sh) { dw = bw; dh = (int)((long)bw * sh / sw); }
    else { dh = bh; dw = (int)((long)bh * sw / sh); }
    int mx = bx + (bw - dw) / 2, my = by + (bh - dh) / 2;
    for (int y = 0; y < dh; y++) {
        int yy = my + y; if (yy < 0 || yy >= h) continue;
        int sy0 = (int)((long)y * sh / dh), sy1 = (int)((long)(y + 1) * sh / dh); if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < dw; x++) {
            int xx = mx + x; if (xx < 0 || xx >= w) continue;
            int sx0 = (int)((long)x * sw / dw), sx1 = (int)((long)(x + 1) * sw / dw); if (sx1 <= sx0) sx1 = sx0 + 1;
            unsigned r = 0, g = 0, b = 0, n = 0;          /* area average of the source block */
            for (int sy = sy0; sy < sy1; sy++) for (int sx = sx0; sx < sx1; sx++, n++) {
                uint32_t c = g_img[sy * AREA_W + sx];
                r += c >> 16 & 255; g += c >> 8 & 255; b += c & 255;
            }
            r /= n; g /= n; b /= n;
            bool grid = false;                            /* screen boundaries */
            for (int k = 1; k < g_nw; k++) grid |= sx0 <= k * SCR_W && sx1 > k * SCR_W && (sx0 == k * SCR_W || sx1 - sx0 > 1 || dw <= sw);
            for (int k = 1; k < g_nh; k++) grid |= sy0 <= k * SCR_H && sy1 > k * SCR_H && (sy0 == k * SCR_H || sy1 - sy0 > 1 || dh <= sh);
            if (grid) { r = r * 3 / 5; g = g * 3 / 5; b = b * 3 / 5; }
            fb[yy * w + xx] = 0xFF000000u | r << 16 | g << 8 | b;
        }
    }
    {   /* party: red square with a white rim, ~1/40 of the map width (at least 7 px) */
        int cx = mx + (int)((long)rx * dw / sw), cy = my + (int)((long)ry * dh / sh);
        int r = dw / 80; if (r < 2) r = 2;
        hud_fill(fb, w, h, cx - r - 1, cy - r - 1, cx + r + 2, cy + r + 2, 0xFFFFFFFFu, false);
        hud_fill(fb, w, h, cx - r, cy - r, cx + r + 1, cy + r + 1, 0xFFEB3C3Cu, false);
    }
    return true;
}

void ps_worldmap_screen(int *sx, int *sy)
{
    *sx = (((g_area.px % 2048) + 2048) % 2048) / SCR_W;
    *sy = (((g_area.py % 1536) + 1536) % 1536) / SCR_H;
}

/* HUD page: the map in a 4:3 box, an info column to the right */
void ps_worldmap_hud_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1)
{
    const uint32_t GREY = 0xFFA0A0B0, CYAN = 0xFF60D0FF, WHITE = 0xFFFFFFFF;
    int bh = y1 - y0 - 2, bw = bh * 4 / 3;
    if (bw > x1 - x0 - 60) { bw = x1 - x0 - 60; bh = bw * 3 / 4; }
    int bx = x0 + 2, by = y0 + 1;
    hud_fill(fb, w, h, bx - 1, by - 1, bx + bw + 1, by + bh + 1, 0xFF0C0C12u, false);
    if (!ps_worldmap_draw(fb, w, h, bx, by, bw, bh)) {
        hud_text(fb, w, h, x0 + 8, y0 + 4, "NO MAP YET", GREY);
        hud_text(fb, w, h, x0 + 8, y0 + 18, "SHOWN ONCE YOU ARE ON A PLANET", GREY);
        hud_text(fb, w, h, x0 + 8, y0 + 28, "OR IN A TOWN.", GREY);
        return;
    }
    int tx = bx + bw + 6, ty = by, sx, sy;
    hud_text(fb, w, h, tx, ty, ps_worldmap_area_name(), CYAN); ty += 12;
    char buf[16];
    ps_worldmap_screen(&sx, &sy);
    snprintf(buf, sizeof buf, "%d,%d", sx, sy);
    hud_text(fb, w, h, tx, ty, "SCREEN", GREY); ty += 9;
    hud_text(fb, w, h, tx, ty, buf, WHITE);
}
