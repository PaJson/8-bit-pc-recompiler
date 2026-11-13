/*
 * ps_automap.c - Phantasy Star (SMS) dungeon auto-map, a SEGA-AGES-style QoL feature.
 *
 * Purely an observer: it reads the game's RAM (and the object table in ROM) once per frame through
 * the on_frame_post hook and never writes to the game, so gameplay is bit-identical to the ROM.
 *
 *  - The map is a page of the settings HUD (F1 / pad Back; opens on the MAP page inside a dungeon,
 *    pages switch with Left/Right on the tab row, Q/E, PgUp/PgDn or pad LB/RB). Outside a dungeon
 *    the page shows the last dungeon visited. An optional side window shows the same map.
 *  - View options (HUD / ps_qol.ini): REVEAL = explored cells only | the whole floor; SECRETS =
 *    shown (secret walls, pits, chests and events are marked wherever the map is drawn, like the
 *    SEGA AGES map) | hidden (they only appear once you have stood on the cell).
 *  - Explored cells are kept PER SAVE SLOT: when the game saves into slot N (RAM $C300-$C6FF is
 *    copied to battery RAM $8000+N*$400, N = 1..5, used flag $8200+N; ps1.asm PlayerMenu_Save) the
 *    current memory becomes slot N's and is written to "<save>.automap" next to the .sav; when the
 *    title screen's CONTINUE loads slot N (ps1.asm LABEL_678), slot N's memory is restored; a new
 *    game starts empty. Detection compares the RAM block with the battery-RAM slots, so no game code
 *    is hooked. Nothing is written with SMS_NO_SAVE.
 *
 * Dungeon data (ps1disasm-master; verified in the running game 2026-11-13):
 *   $C202  Game_mode ($0B = GameMode_Dungeon, <= 3 = title/intro)
 *   $C30A  Dungeon_direction & 3: 0 = north (-y), 1 = east (+x), 2 = south (+y), 3 = west (-x)
 *          (movement table at LABEL_6AE1-2 = $F0,$01,$10,$FF; view table LABEL_6C06)
 *   $C30C  Dungeon_position (y*16 + x)      $C30D  Dungeon_index (floor number, 0..$3C)
 *   $CB00  16x16 cells, loaded from 4-bit layouts in bank 15 (B15_DungeonLayouts); cell & 7:
 *          0 floor, 1 wall, 2 stairs to floor+1 (up), 3 stairs to floor-1 (down), 4 door (button
 *          opens), 5 locked door (Dungeon Key), 6 magic door (Open spell / Miracle Key), 7 SECRET
 *          WALL (drawn as a plain wall in the 3D view, but you walk through it - tested in Maharu
 *          Cave B1F); bit 3 = object/event. A cell of exactly $08 is an object if B03_ObjectData has
 *          an entry for (floor, position), else a PIT (LABEL_65EE: you fall to floor-1).
 *   B03_ObjectData (bank 3, found by its code pattern; Rev A $AF5C, 186 entries of 7 bytes):
 *          floor, position, flag pointer (word; the flag byte becomes $FF when the chest is taken /
 *          the event is done), kind (0 item chest, 1 meseta chest, 2 battle / event), 2 data bytes.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "hud.h"

extern uint8_t        sms_read8(uint16_t addr);
extern uint8_t       *glue_rom_image(size_t *size);
extern const uint8_t *glue_sram_image(size_t *size);
extern const char    *glue_save_path(void);

#define RAM_GAME_MODE 0xC202
#define RAM_DNG_DIR   0xC30A
#define RAM_DNG_POS   0xC30C
#define RAM_DNG_IDX   0xC30D
#define RAM_DNG_MAP   0xCB00
#define RAM_SAVE_BLK  0xC300            /* the 1 KB block the game saves / loads */
#define MODE_DUNGEON  0x0B
#define N_SLOTS       6                 /* index 1..5 = the game's save slots (0 unused) */

enum { SEEN = 1, VISITED = 2 };
#define MAX_DUNGEONS 96

/* key = floor number (Dungeon_index); `index` kept for the file format */
typedef struct { uint32_t key; uint8_t index; uint8_t cells[256]; uint8_t flags[256]; } DungeonMemory;
typedef struct { DungeonMemory d[MAX_DUNGEONS]; int n; } MapMemory;

static MapMemory      g_work;           /* the game being played */
static MapMemory      g_slot[N_SLOTS];  /* per save slot (persisted) */
static DungeonMemory *g_cur;            /* current / last dungeon floor */
static int            g_cur_px, g_cur_py, g_cur_dir;
static bool           g_in_dungeon;
static int            g_reveal_all;     /* option: 0 explored cells, 1 whole floor */
static int            g_show_secrets = 1;   /* option: 1 mark secrets everywhere, 0 only where visited */

static const int DX[4] = { 0, 1, 0, -1 };   /* north, east, south, west */
static const int DY[4] = { -1, 0, 1, 0 };
static const char *const k_face[4] = { "NORTH", "EAST", "SOUTH", "WEST" };

static DungeonMemory *memory_for(MapMemory *m, uint32_t key)
{
    for (int i = 0; i < m->n; i++) if (m->d[i].key == key) return &m->d[i];
    if (m->n >= MAX_DUNGEONS) m->n = 0;                  /* overwrite oldest; never expected */
    DungeonMemory *d = &m->d[m->n++];
    memset(d, 0, sizeof *d); d->key = key;
    return d;
}

static void reveal(DungeonMemory *m, int x, int y, uint8_t f)
{
    if (x < 0 || x > 15 || y < 0 || y > 15) return;
    m->flags[y * 16 + x] |= f;
}

/* ---- object table (chests / events) from ROM ---- */
typedef struct { uint8_t floor, pos, kind; uint16_t flag; } MapObject;
static MapObject g_obj[320];
static int       g_nobj;

static void load_objects(void)
{
    g_nobj = 0;
    size_t n = 0; const uint8_t *rom = glue_rom_image(&n);
    if (!rom || n < 0x10000) return;
    /* ld hl,$FFFF ; ld (hl),3 ; ld hl,B03_ObjectData ; ld de,6   (LABEL_65EE / LABEL_6963 / LABEL_684A) */
    static const uint8_t pat[] = { 0x21, 0xFF, 0xFF, 0x36, 0x03, 0x21 };
    long tbl = -1;
    for (size_t i = 0; i + 11 <= 0x8000; i++)
        if (!memcmp(rom + i, pat, sizeof pat) && rom[i + 8] == 0x11 && rom[i + 9] == 0x06 && rom[i + 10] == 0x00) {
            uint16_t a = (uint16_t)(rom[i + 6] | (rom[i + 7] << 8));
            if (a >= 0x8000 && a < 0xC000) { tbl = 3L * 0x4000 + (a - 0x8000); break; }
        }
    if (tbl < 0) { fprintf(stderr, "[automap] object table not found: chests/pits not marked\n"); return; }
    for (long o = tbl; o + 7 <= (long)n && rom[o] != 0xFF && g_nobj < (int)(sizeof g_obj / sizeof g_obj[0]); o += 7) {
        MapObject *m = &g_obj[g_nobj++];
        m->floor = rom[o]; m->pos = rom[o + 1]; m->flag = (uint16_t)(rom[o + 2] | (rom[o + 3] << 8)); m->kind = rom[o + 4];
    }
}

static const MapObject *object_at(uint8_t floor, int pos)
{
    for (int i = 0; i < g_nobj; i++) if (g_obj[i].floor == floor && g_obj[i].pos == pos) return &g_obj[i];
    return NULL;
}

/* ---- cell classification ---- */
enum { K_FLOOR, K_WALL, K_UP, K_DOWN, K_DOOR, K_LOCKED, K_MAGIC, K_SECRET, K_PIT, K_CHEST, K_CHEST_OPEN,
       K_EVENT, K_EVENT_DONE, K_N };
static int classify(uint8_t floor, int pos, uint8_t cell, bool *event)
{
    int v = cell & 0x0F, t = v & 7;
    *event = false;
    if (v == 8) {
        const MapObject *o = object_at(floor, pos);
        if (!o) return K_PIT;
        bool done = o->flag >= 0xC000 && sms_read8(o->flag) == 0xFF;
        if (o->kind <= 1) return done ? K_CHEST_OPEN : K_CHEST;
        return done ? K_EVENT_DONE : K_EVENT;
    }
    *event = (v & 8) && t != 0;                          /* door / stairs with an event */
    static const int k[8] = { K_FLOOR, K_WALL, K_UP, K_DOWN, K_DOOR, K_LOCKED, K_MAGIC, K_SECRET };
    return k[t];
}
static bool is_secret(int k) { return k == K_SECRET || k == K_PIT || k == K_CHEST || k == K_CHEST_OPEN || k == K_EVENT || k == K_EVENT_DONE; }

/* ---- drawing (shared by the HUD page and the side window) ---- */
typedef void (*FillFn)(int x0, int y0, int x1, int y1, uint32_t rgb);   /* [x0,x1) x [y0,y1) */
#define C_BG      0x0C0C12u
#define C_FLOOR   0x2A3E46u
#define C_FLOOR_V 0x3C6048u
#define C_WALL    0x707A98u
#define C_SECRET  0x8C5CB4u
#define C_DOOR    0xB07A34u
#define C_LOCKED  0xE86A20u
#define C_MAGIC   0x40C8E8u
#define C_STAIRS  0xF0F0F0u
#define C_PIT     0x000000u
#define C_PIT_RIM 0xC03C3Cu
#define C_CHEST   0xF0C030u
#define C_OPENED  0x6A6A6Au
#define C_EVENT   0xF04848u
#define C_YOU     0xEB3C3Cu
#define C_NOSE    0xFFE678u

static void draw_glyph(FillFn f, int k, bool visited, int sx, int sy, int cs)
{
    int e = cs - 1, q = cs / 4 > 0 ? cs / 4 : 1;
    uint32_t floor = visited ? C_FLOOR_V : C_FLOOR;
    switch (k) {
    case K_WALL:   f(sx, sy, sx + e, sy + e, C_WALL); break;
    case K_SECRET: f(sx, sy, sx + e, sy + e, C_SECRET);                  /* wall with a hole pattern */
                   for (int y = 0; y < e; y += 2) for (int x = (y / 2) & 1; x < e; x += 2) f(sx + x, sy + y, sx + x + 1, sy + y + 1, C_WALL);
                   break;
    case K_DOOR: case K_LOCKED: case K_MAGIC:
        f(sx, sy, sx + e, sy + e, C_WALL);
        f(sx + q, sy + q, sx + e - q, sy + e - q, k == K_DOOR ? C_DOOR : k == K_LOCKED ? C_LOCKED : C_MAGIC);
        break;
    case K_UP: case K_DOWN:
        f(sx, sy, sx + e, sy + e, floor);
        {   /* centred triangle, 2 px wider every 2 rows, 1 px margin; pointing up or down */
            int rows = 0, wid[16];
            for (int i = 0; i < 16; i++) {
                int wd = (e % 2 == 0) ? 2 * (i / 2 + 1) : 2 * (i / 2) + 1;
                if (wd > (e - 2 > 1 ? e - 2 : 1)) break;
                wid[rows++] = wd;
            }
            int top = sy + (e - rows) / 2;
            for (int i = 0; i < rows; i++) {
                int wd = wid[k == K_UP ? i : rows - 1 - i], x0 = sx + (e - wd) / 2;
                f(x0, top + i, x0 + wd, top + i + 1, C_STAIRS);
            }
        }
        break;
    case K_PIT:
        f(sx, sy, sx + e, sy + e, floor);
        f(sx + 1, sy + 1, sx + e - 1, sy + e - 1, C_PIT_RIM);
        f(sx + 2, sy + 2, sx + e - 2, sy + e - 2, C_PIT);
        break;
    case K_CHEST: case K_CHEST_OPEN:
        f(sx, sy, sx + e, sy + e, floor);
        f(sx + q, sy + q, sx + e - q, sy + e - q, k == K_CHEST ? C_CHEST : C_OPENED);
        break;
    case K_EVENT: case K_EVENT_DONE:
        f(sx, sy, sx + e, sy + e, floor);           /* "!" */
        {   uint32_t c = k == K_EVENT ? C_EVENT : C_OPENED;
            int bw = e >= 6 ? 2 : 1, x0 = sx + (e - bw) / 2;
            if (e >= 5) { f(x0, sy + 1, x0 + bw, sy + e - 3, c); f(x0, sy + e - 2, x0 + bw, sy + e - 1, c); }
            else f(x0, sy, x0 + bw, sy + e, c);
        }
        break;
    default:       f(sx, sy, sx + e, sy + e, floor); break;
    }
}

/* Draws the 16x16 map at (mx,my) with cell size cs; returns the number of explored cells. */
static int draw_map(FillFn f, int mx, int my, int cs)
{
    f(mx - 1, my - 1, mx + 16 * cs + 1, my + 16 * cs + 1, C_BG);
    int seen = 0;
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        int p = y * 16 + x;
        uint8_t fl = g_cur->flags[p];
        if (fl & (SEEN | VISITED)) seen++;
        if (!(fl & (SEEN | VISITED)) && !g_reveal_all) continue;
        bool ev; int k = classify(g_cur->index, p, g_cur->cells[p], &ev);
        if (is_secret(k) && !g_show_secrets && !(fl & VISITED)) {   /* looks like what it pretends to be */
            k = (k == K_SECRET) ? K_WALL : K_FLOOR; ev = false;
        }
        int sx = mx + x * cs, sy = my + y * cs;
        draw_glyph(f, k, (fl & VISITED) != 0, sx, sy, cs);
        if (ev && (g_show_secrets || (fl & VISITED))) f(sx, sy, sx + 2, sy + 2, C_EVENT);   /* event corner */
    }
    if (g_in_dungeon) {                                  /* player: red square + yellow nose */
        int cx = mx + g_cur_px * cs + cs / 2, cy = my + g_cur_py * cs + cs / 2, r = cs / 2 - 1; if (r < 1) r = 1;
        f(cx - r, cy - r, cx + r, cy + r, C_YOU);
        int nx = cx + DX[g_cur_dir] * r, ny = cy + DY[g_cur_dir] * r;
        f(nx - 1, ny - 1, nx + 1, ny + 1, C_NOSE);
    }
    return seen;
}

/* ---- HUD page ---- */
static uint32_t *g_hfb; static int g_hw, g_hh;
static void hud_fill_rgb(int x0, int y0, int x1, int y1, uint32_t rgb) { hud_fill(g_hfb, g_hw, g_hh, x0, y0, x1, y1, 0xFF000000u | rgb, false); }

void ps_automap_hud_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1)
{
    const uint32_t WHITE = 0xFFFFFFFF, GREY = 0xFFA0A0B0, CYAN = 0xFF60D0FF;
    if (!g_cur) {
        hud_text(fb, w, h, x0 + 8, y0 + 4, "NO DUNGEON EXPLORED YET", GREY);
        hud_text(fb, w, h, x0 + 8, y0 + 18, "THE MAP FILLS IN AS YOU WALK", GREY);
        hud_text(fb, w, h, x0 + 8, y0 + 28, "THROUGH A DUNGEON.", GREY);
        return;
    }
    g_hfb = fb; g_hw = w; g_hh = h;
    int avail = (y1 - y0) < (x1 - x0) ? (y1 - y0) : (x1 - x0);
    int cs = avail / 16; if (cs > 9) cs = 9; if (cs < 3) cs = 3;
    int mx = x0 + 2, my = y0 + 1;
    int seen = draw_map(hud_fill_rgb, mx, my, cs);
    /* info + legend to the right of the map */
    int tx = mx + 16 * cs + 8, ty = my;
    char buf[48];
    snprintf(buf, sizeof buf, "%s %02X", g_in_dungeon ? "FLOOR" : "LAST FLOOR", g_cur->index);
    hud_text(fb, w, h, tx, ty, buf, CYAN); ty += 9;
    if (g_in_dungeon) {
        snprintf(buf, sizeof buf, "X%d Y%d %s", g_cur_px, g_cur_py, k_face[g_cur_dir]); hud_text(fb, w, h, tx, ty, buf, WHITE); ty += 9;
    }
    snprintf(buf, sizeof buf, "EXPLORED %d%%", seen * 100 / 256); hud_text(fb, w, h, tx, ty, buf, GREY); ty += 12;
    static const struct { int k; const char *t; } leg[] = {
        { K_UP, "STAIRS UP" }, { K_DOWN, "STAIRS DOWN" }, { K_DOOR, "DOOR" }, { K_LOCKED, "LOCKED DOOR" },
        { K_MAGIC, "MAGIC DOOR" }, { K_SECRET, "SECRET WALL" }, { K_PIT, "PIT" }, { K_CHEST, "CHEST" },
        { K_EVENT, "EVENT/BATTLE" } };
    for (unsigned i = 0; i < sizeof leg / sizeof leg[0] && ty + 8 <= y1; i++, ty += 9) {
        draw_glyph(hud_fill_rgb, leg[i].k, true, tx, ty, 8);
        hud_text(fb, w, h, tx + 11, ty, leg[i].t, GREY);
    }
}

int  ps_automap_in_dungeon(void) { return sms_read8(RAM_GAME_MODE) == MODE_DUNGEON; }
void ps_automap_set_view(int reveal_all, int show_secrets) { g_reveal_all = reveal_all; g_show_secrets = show_secrets; }

/* ---- persistence: "<save>.automap" = "PSAM" v2, then per slot 1..5: u16 count, entries ---- */
static char g_path[320];

static void build_path(void)
{
    const char *sp = glue_save_path();
    g_path[0] = 0;
    if (!sp || !sp[0]) return;                           /* SMS_NO_SAVE / no save file */
    snprintf(g_path, sizeof g_path, "%s", sp);
    size_t n = strlen(g_path);
    if (n > 4 && !strcmp(g_path + n - 4, ".sav")) g_path[n - 4] = 0;
    strncat(g_path, ".automap", sizeof g_path - strlen(g_path) - 1);
}

static void save_file(void)
{
    if (!g_path[0]) return;
    FILE *f = fopen(g_path, "wb");
    if (!f) { fprintf(stderr, "[automap] cannot write %s\n", g_path); return; }
    fwrite("PSAM\2", 1, 5, f);                           /* v2: key = floor number */
    for (int s = 1; s < N_SLOTS; s++) {
        uint8_t c[2] = { (uint8_t)g_slot[s].n, (uint8_t)(g_slot[s].n >> 8) };
        fwrite(c, 1, 2, f);
        for (int i = 0; i < g_slot[s].n; i++) {
            const DungeonMemory *d = &g_slot[s].d[i];
            uint8_t k[5] = { (uint8_t)d->key, (uint8_t)(d->key >> 8), (uint8_t)(d->key >> 16), (uint8_t)(d->key >> 24), d->index };
            fwrite(k, 1, 5, f); fwrite(d->cells, 1, 256, f); fwrite(d->flags, 1, 256, f);
        }
    }
    fclose(f);
}

static void load_file(void)
{
    memset(g_slot, 0, sizeof g_slot);
    if (!g_path[0]) return;
    FILE *f = fopen(g_path, "rb");
    if (!f) return;
    uint8_t hdr[5];
    if (fread(hdr, 1, 5, f) != 5 || memcmp(hdr, "PSAM", 4) || (hdr[4] != 1 && hdr[4] != 2)) { fclose(f); return; }
    for (int s = 1; s < N_SLOTS; s++) {
        uint8_t c[2]; if (fread(c, 1, 2, f) != 2) break;
        int n = c[0] | (c[1] << 8); if (n > MAX_DUNGEONS) n = MAX_DUNGEONS;
        for (int i = 0; i < n; i++) {
            DungeonMemory *d = &g_slot[s].d[i]; uint8_t k[5];
            if (fread(k, 1, 5, f) != 5 || fread(d->cells, 1, 256, f) != 256 || fread(d->flags, 1, 256, f) != 256) { n = i; break; }
            d->index = k[4];
            d->key = (hdr[4] == 1) ? d->index : (k[0] | (k[1] << 8) | (k[2] << 16) | ((uint32_t)k[3] << 24));   /* v1 keyed by wall hash */
        }
        g_slot[s].n = n;
    }
    fclose(f);
}

/* ---- save / load detection (see the header comment) ---- */
static uint32_t fnv(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}
static uint32_t g_slot_h[N_SLOTS];
static int      g_prev_mode = -1;
static int      g_load_window;      /* frames left to recognise a CONTINUE load */

static void track_saves(uint8_t mode)
{
    size_t sn = 0; const uint8_t *sram = glue_sram_image(&sn);
    uint8_t blk[0x400];
    for (int i = 0; i < 0x400; i++) blk[i] = sms_read8((uint16_t)(RAM_SAVE_BLK + i));
    uint32_t rh = fnv(blk, sizeof blk);
    bool first = (g_prev_mode < 0);
    for (int s = 1; s < N_SLOTS && sram && sn >= 0x8000u; s++) {
        uint32_t h = fnv(sram + s * 0x400, 0x400);
        bool used = sram[0x200 + s] != 0;
        if (!first && h != g_slot_h[s] && h == rh && used) {          /* the game just saved into slot s */
            g_slot[s] = g_work;                                         /* last floor visited goes last */
            if (g_cur && g_cur >= g_work.d && g_cur < g_work.d + g_work.n && g_work.n > 1) {
                int li = (int)(g_cur - g_work.d); DungeonMemory t = g_slot[s].d[li];
                g_slot[s].d[li] = g_slot[s].d[g_work.n - 1]; g_slot[s].d[g_work.n - 1] = t;
            }
            save_file();
            fprintf(stderr, "[automap] saved explored map with save slot %d (%d floors)\n", s, g_work.n);
        }
        g_slot_h[s] = h;
    }
    /* Leaving the title (mode <= 3 -> game) starts a new game's empty map; CONTINUE copies the slot
     * into $C300 a little later (the mode changes first), so for the next 10 s a frame whose party
     * block $C400-$C4FF (stats, inventory, money; not touched by the map loader) equals a used
     * slot's and whose whole block is >= 85% equal restores that slot's map (the map loader changes
     * some scroll/map bytes of the block). A new game never matches (unless it is identical). */
    if (g_prev_mode >= 0 && g_prev_mode <= 3 && mode > 3) {
        memset(&g_work, 0, sizeof g_work); g_cur = NULL;
        g_load_window = 600;
    }
    if (g_load_window > 0) {
        g_load_window--;
        int best = 0, best_eq = 0;
        for (int s = 1; s < N_SLOTS && sram && sn >= 0x8000u; s++) {
            if (!sram[0x200 + s]) continue;
            if (memcmp(sram + s * 0x400 + 0x100, blk + 0x100, 0x100)) continue;   /* party block */
            int eq = 0; for (int i = 0; i < 0x400; i++) eq += (sram[s * 0x400 + i] == blk[i]);
            if (eq > best_eq) { best_eq = eq; best = s; }
        }
        if (best && best_eq >= 870) {
            g_work = g_slot[best]; g_load_window = 0;
            g_cur = g_work.n ? &g_work.d[g_work.n - 1] : NULL;           /* shown as the last dungeon */
            fprintf(stderr, "[automap] restored explored map of save slot %d (%d floors; %d/1024 bytes equal)\n", best, g_work.n, best_eq);
        }
    }
    (void)rh;
    g_prev_mode = mode;
}

/* Headless/debug: PS_AUTOMAP_ASCII=1 prints the remembered map to stderr whenever the position changes. */
static void ascii_dump(int px, int py, int dir)
{
    static int armed = -1; static int last = -1;
    if (armed < 0) armed = getenv("PS_AUTOMAP_ASCII") ? 1 : 0;
    if (!armed || last == (py * 16 + px) + 256 * dir) return;
    last = (py * 16 + px) + 256 * dir;
    static const char arrow[4] = { '^', '>', 'v', '<' };
    static const char sym[K_N] = { '.', '#', 'U', 'D', 'd', 'l', 'm', 's', 'o', 'c', 'C', 'e', 'E' };
    fprintf(stderr, "[automap] floor %02X pos=(%d,%d) facing %s\n", g_cur->index, px, py, k_face[dir]);
    for (int y = 0; y < 16; y++) {
        char row[17]; row[16] = 0;
        for (int x = 0; x < 16; x++) {
            bool ev; int k = classify(g_cur->index, y * 16 + x, g_cur->cells[y * 16 + x], &ev);
            char c = (g_cur->flags[y * 16 + x] & (SEEN | VISITED)) ? sym[k] : ' ';
            if (x == px && y == py) c = arrow[dir];
            row[x] = c;
        }
        fprintf(stderr, "[automap] %s\n", row);
    }
}

void ps_automap_init(void)
{
    build_path();
    load_file();
    load_objects();
    memset(&g_work, 0, sizeof g_work); g_cur = NULL;
    g_prev_mode = -1; g_load_window = 0;
}

#ifdef SMS_HAVE_SDL
#include <SDL.h>
/* ---- optional side window (HUD option "MAP SIDE WINDOW") ---- */
#define CELL   20                 /* pixels per map cell */
#define MARGIN 8
#define WIN_PX (MARGIN * 2 + 16 * CELL)
static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static bool          g_failed;
static bool          g_shown;
static uint32_t      g_last_sig = 0xFFFFFFFFu;

static bool ensure_window(void)
{
    if (g_win) return true;
    if (g_failed || !SDL_WasInit(SDL_INIT_VIDEO)) return false;   /* headless run: no map window */
    g_win = SDL_CreateWindow("Phantasy Star - Auto-Map", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             WIN_PX, WIN_PX, SDL_WINDOW_HIDDEN);
    if (!g_win) { g_failed = true; return false; }
    SDL_SetWindowData(g_win, "aux", (void *)1);                  /* tells host_sdl: closing = hide */
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (!g_ren) { SDL_DestroyWindow(g_win); g_win = NULL; g_failed = true; return false; }
    return true;
}
static void wfill(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    SDL_SetRenderDrawColor(g_ren, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255);
    SDL_Rect rc = { x0, y0, x1 - x0, y1 - y0 };
    SDL_RenderFillRect(g_ren, &rc);
}
static void window_hide(void)
{
    if (g_shown && g_win) { SDL_HideWindow(g_win); g_shown = false; g_last_sig = 0xFFFFFFFFu; }
}
static void window_update(void)
{
    if (!ensure_window()) return;
    uint32_t sig = (uint32_t)(g_cur_py * 16 + g_cur_px) | ((uint32_t)g_cur_dir << 8) | ((uint32_t)g_cur->key << 12);
    uint32_t seen_n = 0; for (int i = 0; i < 256; i++) seen_n += (g_cur->flags[i] != 0) * (uint32_t)(i + 1);
    for (int i = 0; i < 256; i++) seen_n += g_cur->cells[i] * 7u;
    sig ^= seen_n * 2654435761u ^ (uint32_t)(g_reveal_all * 3 + g_show_secrets) << 28;
    if (!g_shown) {
        SDL_Window *prev = SDL_GetKeyboardFocus();          /* don't steal focus from the game window */
        SDL_ShowWindow(g_win);
        if (prev && prev != g_win) SDL_RaiseWindow(prev);
        g_shown = true; g_last_sig = 0xFFFFFFFFu;
    }
    if (sig == g_last_sig) return;
    g_last_sig = sig;
    char title[96];
    snprintf(title, sizeof title, "Phantasy Star - Auto-Map  (floor %02X  x=%d y=%d  facing %s)",
             g_cur->index, g_cur_px, g_cur_py, k_face[g_cur_dir]);
    SDL_SetWindowTitle(g_win, title);
    SDL_SetRenderDrawColor(g_ren, 12, 12, 18, 255);
    SDL_RenderClear(g_ren);
    draw_map(wfill, MARGIN, MARGIN, CELL);
    SDL_RenderPresent(g_ren);
}
#else
static void window_hide(void) {}
static void window_update(void) {}
#endif

static bool g_window_on = false;
void ps_automap_enable(bool on)       /* the side window (the HUD page is always there) */
{
    g_window_on = on;
    if (!on) window_hide();
}

/* line of sight stops at anything that looks like a wall (walls, doors, secret walls) */
static bool blocks_view(uint8_t cell) { int t = cell & 7; return t == 1 || t >= 4; }

/* Stairs / doors: the game moves the party 2 cells (past the stairs or door), so the party never
   stands on them. Reveal the 3x3 around such a cell when the party arrives next to it. */
static bool is_passage(uint8_t cell) { int t = cell & 7; return t >= 2 && t <= 6; }
static void reveal_around(DungeonMemory *m, int x, int y)
{
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) reveal(m, x + dx, y + dy, SEEN);
}

/* Settling: on a floor change (stairs, pit) the game switches Dungeon_index and the layout at once,
   but moves the position only after the fade-out (~36 frames for stairs; a pit keeps the position).
   When a dungeon is entered, the layout is all zero for a few frames. Nothing is recorded until
   the new state is consistent. */
#define SETTLE_TIMEOUT 90
static bool g_was_in, g_pending;
static int  g_pend_frames, g_pend_pos, g_last_floor = -1, g_last_pos = -1;

void ps_automap_frame(uint64_t frame)
{
    (void)frame;
    uint8_t mode = sms_read8(RAM_GAME_MODE);
    track_saves(mode);
    g_in_dungeon = (mode == MODE_DUNGEON);
    if (!g_in_dungeon) { g_was_in = false; g_last_pos = -1; window_hide(); return; }

    uint8_t map[256]; bool any = false;
    for (int i = 0; i < 256; i++) { map[i] = sms_read8((uint16_t)(RAM_DNG_MAP + i)); any |= map[i] != 0; }
    uint8_t pos = sms_read8(RAM_DNG_POS);
    uint8_t floor = sms_read8(RAM_DNG_IDX);
    uint8_t dir = sms_read8(RAM_DNG_DIR) & 3;
    uint32_t hsh = 0; for (int i = 0; i < 256; i++) hsh = hsh * 31 + map[i];
    {   static int tr = -1; if (tr < 0) tr = getenv("PS_AUTOMAP_TRACE") != NULL;
        if (tr) fprintf(stderr, "[amtrace] f=%llu floor=%02X pos=%02X dir=%d map=%08X\n", (unsigned long long)frame, floor, pos, dir, hsh); }
    static uint32_t prev_hsh;
    bool stable = hsh == prev_hsh; prev_hsh = hsh;
    if (!any || !stable) return;                         /* layout not (fully) loaded yet: loads over 1-2 frames */

    bool arrived = false;
    if (!g_was_in) {                                     /* entered from outside: position is final */
        g_was_in = true; g_pending = false; arrived = true;
    } else if (floor != g_last_floor) {                  /* stairs / pit: wait for the position */
        g_pending = true; g_pend_frames = 0; g_pend_pos = g_last_pos;
    }
    g_last_floor = floor;
    if (g_pending) {
        if (pos == g_pend_pos && ++g_pend_frames < SETTLE_TIMEOUT) return;
        g_pending = false; arrived = true;
    }

    int px = pos & 15, py = pos >> 4;
    g_cur_dir = dir; g_cur_px = px; g_cur_py = py;
    g_cur = memory_for(&g_work, floor);
    memcpy(g_cur->cells, map, 256);                      /* doors / chests change: keep the latest */
    g_cur->index = floor;
    g_cur->flags[pos] |= VISITED | SEEN;
    reveal_around(g_cur, px, py);
    {   /* arrived past stairs (or walked through a door): also show around the stairs/door cell */
        int bx = px - DX[dir], by = py - DY[dir];
        bool jumped = !arrived && g_last_pos >= 0 && g_last_pos != pos
                      && px - 2 * DX[dir] == (g_last_pos & 15) && py - 2 * DY[dir] == (g_last_pos >> 4);
        if ((arrived || jumped) && bx >= 0 && bx < 16 && by >= 0 && by < 16 && is_passage(map[by * 16 + bx]))
            reveal_around(g_cur, bx, by);
    }
    g_last_pos = pos;
    {   /* line of sight straight ahead */
        int x = px, y = py;
        for (int i = 0; i < 16; i++) {
            x += DX[g_cur_dir]; y += DY[g_cur_dir];
            if (x < 0 || x > 15 || y < 0 || y > 15) break;
            reveal(g_cur, x, y, SEEN);
            if (blocks_view(map[y * 16 + x])) break;
        }
    }
    ascii_dump(g_cur_px, g_cur_py, g_cur_dir);
    if (g_window_on) window_update();
}
