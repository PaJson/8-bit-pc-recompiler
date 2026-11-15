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

enum { SEEN = 1, VISITED = 2, AVAIL = 4 };   /* AVAIL: a hidden-at-start chest was seen available here */
#define MAX_DUNGEONS 96

/* key = floor number (Dungeon_index); `index` kept for the file format */
typedef struct { uint32_t key; uint8_t index; uint8_t cells[256]; uint8_t flags[256]; } DungeonMemory;
#define N_MAPS 32                       /* places (ps_worldmap_current_place): 0..3 planets, 4.. towns */
typedef struct {
    DungeonMemory d[MAX_DUNGEONS]; int n;
    uint8_t map_seen[N_MAPS];            /* planets / towns visited (HUD MAP page list) */
    int16_t map_px[N_MAPS], map_py[N_MAPS];   /* the party's last position there (world pixels) */
} MapMemory;

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
typedef struct { uint8_t floor, pos, kind; bool hidden; uint16_t flag; } MapObject;
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
        m->hidden = false;
    }
    /* Chests that only appear after a script enables them: the new-game routine (LABEL_5E8) clears the
       save block and then sets their flags to $FF (= "no chest"); a script later writes $00 (Rev A:
       $C600 Odin joining, $C604 Dungeon Key NPC LABEL_4B9D). So $FF on such a flag means "not yet
       available" OR "taken". Find them in the ROM:
       ld hl,$C300 ; ld de,$C301 ; ld bc,$03FF ; ld (hl),l (Rev A) or ld (hl),0 (Japan) ; ldir
       then   ld hl,flag ; ld (hl),$FF */
    static const uint8_t clr[] = { 0x21, 0x00, 0xC3, 0x11, 0x01, 0xC3, 0x01, 0xFF, 0x03 };
    int nhid = 0;
    for (size_t i = 0; i + sizeof clr + 80 <= 0x8000; i++) {
        if (memcmp(rom + i, clr, sizeof clr)) continue;
        const uint8_t *t = rom + i + sizeof clr;
        size_t body = (t[0] == 0x75 && t[1] == 0xED && t[2] == 0xB0) ? 3
                    : (t[0] == 0x36 && t[1] == 0x00 && t[2] == 0xED && t[3] == 0xB0) ? 4 : 0;
        if (!body) continue;
        for (size_t j = i + sizeof clr + body; j < i + sizeof clr + body + 64; j++)
            if (rom[j] == 0x21 && rom[j + 3] == 0x36 && rom[j + 4] == 0xFF) {
                uint16_t f = (uint16_t)(rom[j + 1] | (rom[j + 2] << 8));
                for (int k = 0; k < g_nobj; k++) if (g_obj[k].flag == f) { g_obj[k].hidden = true; nhid++; }
            }
        break;
    }
    if (getenv("PS_AUTOMAP_ASCII")) fprintf(stderr, "[automap] %d objects, %d hidden at start\n", g_nobj, nhid);
}

static const MapObject *object_at(uint8_t floor, int pos)
{
    for (int i = 0; i < g_nobj; i++) if (g_obj[i].floor == floor && g_obj[i].pos == pos) return &g_obj[i];
    return NULL;
}

/* ---- cell classification ---- */
enum { K_FLOOR, K_WALL, K_UP, K_DOWN, K_DOOR, K_LOCKED, K_MAGIC, K_SECRET, K_PIT, K_CHEST, K_CHEST_OPEN,
       K_EVENT, K_EVENT_DONE, K_N };
/* where object flags are read: live RAM, or a save slot's copy of $C300-$C6FF (slot preview) */
static const uint8_t *g_flag_blk;
static uint8_t flag_read(uint16_t a)
{
    if (g_flag_blk && a >= RAM_SAVE_BLK && a < RAM_SAVE_BLK + 0x400) return g_flag_blk[a - RAM_SAVE_BLK];
    return sms_read8(a);
}
static int classify(uint8_t floor, int pos, uint8_t cell, uint8_t mflags, bool *event)
{
    int v = cell & 0x0F, t = v & 7;
    *event = false;
    if (v == 8) {
        const MapObject *o = object_at(floor, pos);
        if (!o) return K_PIT;
        bool done = o->flag >= 0xC000 && flag_read(o->flag) == 0xFF;
        if (done && o->hidden && !(mflags & AVAIL)) return K_FLOOR;   /* not enabled yet (or taken unseen) */
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
#define C_EXIT    0x40E070u   /* corner mark: stairs/door that leads out (B03_DungeonTransitionData) */

/* Cells are cs px with a gap of g_u px (1 for the small pixel-art sizes; cs / 8 for the big
   picture the hi-res layer shows, so the shapes keep their proportions). Sizes below 16 keep the
   original hand-tuned pixel shapes. */
static int g_u = 1;
static int parity_fit(int wd, int e) { if (wd < 1) wd = 1; if ((e - wd) & 1) wd++; return wd > e ? e : wd; }

static void draw_glyph(FillFn f, int k, bool visited, int sx, int sy, int cs)
{
    int u = g_u, e = cs - u, q = cs / 4 > 0 ? cs / 4 : 1;
    bool big = cs >= 16;
    uint32_t floor = visited ? C_FLOOR_V : C_FLOOR;
    switch (k) {
    case K_WALL:   f(sx, sy, sx + e, sy + e, C_WALL); break;
    case K_SECRET: f(sx, sy, sx + e, sy + e, C_SECRET);                  /* wall with a hole pattern */
                   for (int y = 0; y < e; y += 2 * u)
                       for (int x = ((y / (2 * u)) & 1) * u; x < e; x += 2 * u)
                           f(sx + x, sy + y, sx + (x + u < e ? x + u : e), sy + (y + u < e ? y + u : e), C_WALL);
                   break;
    case K_DOOR: case K_LOCKED: case K_MAGIC:
        f(sx, sy, sx + e, sy + e, C_WALL);
        f(sx + q, sy + q, sx + e - q, sy + e - q, k == K_DOOR ? C_DOOR : k == K_LOCKED ? C_LOCKED : C_MAGIC);
        break;
    case K_UP: case K_DOWN:
        f(sx, sy, sx + e, sy + e, floor);
        if (big) {                                  /* centred triangle, 9/16 of the cell tall */
            int th = e * 9 / 16, wmax = e - 4 * u, top = sy + (e - th) / 2;
            for (int i = 0; i < th; i++) {
                int wd = parity_fit(wmax * (i + 1) / th, e), r = k == K_UP ? i : th - 1 - i, x0 = sx + (e - wd) / 2;
                f(x0, top + r, x0 + wd, top + r + 1, C_STAIRS);
            }
        } else {   /* centred triangle, 2 px wider every 2 rows, 1 px margin; pointing up or down */
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
        {   int p = big ? e / 6 : 1;
            f(sx, sy, sx + e, sy + e, floor);
            f(sx + p, sy + p, sx + e - p, sy + e - p, C_PIT_RIM);
            f(sx + p + u, sy + p + u, sx + e - p - u, sy + e - p - u, C_PIT);
        }
        break;
    case K_CHEST: case K_CHEST_OPEN:
        f(sx, sy, sx + e, sy + e, floor);
        f(sx + q, sy + q, sx + e - q, sy + e - q, k == K_CHEST ? C_CHEST : C_OPENED);
        break;
    case K_EVENT: case K_EVENT_DONE:
        f(sx, sy, sx + e, sy + e, floor);           /* "!" */
        {   uint32_t c = k == K_EVENT ? C_EVENT : C_OPENED;
            if (big) {
                int bw = parity_fit(e / 6, e), x0 = sx + (e - bw) / 2, d0 = sy + e * 3 / 4;
                f(x0, sy + e / 8, x0 + bw, sy + e * 5 / 8, c); f(x0, d0, x0 + bw, d0 + bw, c);
            } else {
                int bw = e % 2 ? 1 : 2, x0 = sx + (e - bw) / 2;   /* centred for odd and even sizes */
                if (e >= 5) { f(x0, sy + 1, x0 + bw, sy + e - 3, c); f(x0, sy + e - 2, x0 + bw, sy + e - 1, c); }
                else f(x0, sy, x0 + bw, sy + e, c);
            }
        }
        break;
    default:       f(sx, sy, sx + e, sy + e, floor); break;
    }
}

/* Draws the 16x16 map at (mx,my) with cell size cs; returns the number of explored cells. */
static int draw_map(FillFn f, int mx, int my, int cs)
{
    g_u = cs >= 16 ? cs / 8 : 1;
    int u = g_u;
    f(mx - u, my - u, mx + 16 * cs + u, my + 16 * cs + u, C_BG);
    int seen = 0;
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        int p = y * 16 + x;
        uint8_t fl = g_cur->flags[p];
        if (fl & (SEEN | VISITED)) seen++;
        if (!(fl & (SEEN | VISITED)) && !g_reveal_all) continue;
        bool ev; int k = classify(g_cur->index, p, g_cur->cells[p], fl, &ev);
        if (is_secret(k) && !g_show_secrets && !(fl & VISITED)) {   /* looks like what it pretends to be */
            k = (k == K_SECRET) ? K_WALL : K_FLOOR; ev = false;
        }
        int sx = mx + x * cs, sy = my + y * cs;
        draw_glyph(f, k, (fl & VISITED) != 0, sx, sy, cs);
        if (ev && (g_show_secrets || (fl & VISITED))) f(sx, sy, sx + 2 * u, sy + 2 * u, C_EXIT);    /* exit / warp corner */
    }
    if (g_in_dungeon) {                                  /* player: arrow (head + shaft) pointing the facing way */
        int sx = mx + g_cur_px * cs, sy = my + g_cur_py * cs, e = cs - u, head = (e + 1) / 2;
        int shaft = cs >= 16 ? parity_fit(e * 3 / 7, e) : (e % 2 ? 3 : 4);
        for (int i = 0; i < e; i++) {                    /* row i, counted from the tip */
            int wd = i < head ? (e % 2 ? 2 * i + 1 : 2 * i + 2) : shaft; if (wd > e) wd = e;
            int a = (e - wd) / 2;                        /* span [a, a+wd) across the arrow */
            uint32_t c = C_YOU;
            switch (g_cur_dir) {
            case 0: f(sx + a, sy + i, sx + a + wd, sy + i + 1, c); break;                   /* north: tip at top */
            case 2: f(sx + a, sy + e - 1 - i, sx + a + wd, sy + e - i, c); break;           /* south */
            case 1: f(sx + e - 1 - i, sy + a, sx + e - i, sy + a + wd, c); break;           /* east: tip right */
            default: f(sx + i, sy + a, sx + i + 1, sy + a + wd, c); break;                  /* west */
            }
        }
    }
    return seen;
}

/* ---- the map as a big picture for the engine's hi-res layer (sharp at any window size) ----
   48 px cells (+ a 6 px border) = 780 x 780; the host shrinks it (area average) or enlarges it to
   the real window pixels. Redrawn only when what the small map would draw changes (signature =
   hash of the draw calls at 8 px cells). One MapImg per place it is shown in the same frame. */
#define HI_CS 48
#define HI_U  (HI_CS / 8)
#define HI_SZ (16 * HI_CS + 2 * HI_U)
typedef struct { uint32_t *img; uint32_t sig, gen; } MapImg;
static uint32_t g_sig, *g_ifb;
static void sig_fill(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    uint32_t v[5] = { (uint32_t)x0, (uint32_t)y0, (uint32_t)x1, (uint32_t)y1, rgb };
    const uint8_t *p = (const uint8_t *)v;
    for (size_t i = 0; i < sizeof v; i++) { g_sig ^= p[i]; g_sig *= 16777619u; }
}
static void img_fill(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > HI_SZ) x1 = HI_SZ; if (y1 > HI_SZ) y1 = HI_SZ;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) g_ifb[y * HI_SZ + x] = 0xFF000000u | rgb;
}
/* draws the current map state (g_cur ...) into m when it changed; returns the explored cell count */
static int map_image(MapImg *m)
{
    g_sig = 2166136261u;
    int seen = draw_map(sig_fill, 1, 1, 8);
    if (!m->img && !(m->img = malloc((size_t)HI_SZ * HI_SZ * 4))) return seen;
    if (m->gen == 0 || g_sig != m->sig) {
        g_ifb = m->img;
        draw_map(img_fill, HI_U, HI_U, HI_CS);
        m->sig = g_sig; m->gen++;
    }
    return seen;
}
/* shows it in `space` with the 16 x 16 cells at (x, y), cs units per cell (border around it) */
static void map_image_show(MapImg *m, int space, float x, float y, float cs)
{
    if (!m->img) return;
    float b = cs * HI_U / HI_CS;
    hud_image(space, x - b, y - b, 16 * cs + 2 * b, 16 * cs + 2 * b, m->img, HI_SZ, HI_SZ, 0, 0, HI_SZ, HI_SZ, false, m->gen);
}

/* ---- HUD page ---- */
static uint32_t *g_hfb; static int g_hw, g_hh;
static void hud_fill_rgb(int x0, int y0, int x1, int y1, uint32_t rgb) { hud_fill(g_hfb, g_hw, g_hh, x0, y0, x1, y1, 0xFF000000u | rgb, false); }

/* Floor names (ps1.constants.asm DungeonID_*). One layout is used by several places in the game
   ('|' separates them; e.g. Medusa Tower 5F-7F are Baya Malay Tower B3F-B1F). */
static const char *const k_floor_name[] = {
    "Medusa Cave|Gothic Tunnel|Baya Malay Pass", "Triada Prison", "Camineet Warehouse|Abion Warehouse",
    "Medusa Tower B1F", "Medusa Tower 1F", "Medusa Tower 2F", "Medusa Tower 3F", "Medusa Tower 4F",
    "Medusa Tower 5F|Baya Malay Tower B3F", "Medusa Tower 6F|Baya Malay Tower B2F", "Medusa Tower 7F|Baya Malay Tower B1F",
    "Baya Malay Tower 1F", "Baya Malay Tower 2F", "Baya Malay Tower 3F", "Baya Malay Tower 4F", "Baya Malay Tower 5F",
    "Iala Cave B3F|Naula Cave B3F", "Iala Cave B2F|Naula Cave B2F", "Iala Cave B1F|Naula Cave B1F", "Iala Cave 1F|Naula Cave 1F",
    "Baya Malay Prison", "Drasgow Warehouse|Dr. Mad's Lab", "Bortevo Pass",
    "Island Tower 1F", "Island Tower 2F", "Island Tower 3F", "Island Tower 4F", "Island Tower 5F",
    "Maharu Cave B2F", "Maharu Cave B1F", "Maharu Cave 1F",
    "Governor's Palace B3F", "Governor's Palace B2F", "Governor's Palace B1F|Path to the Governor",
    "Tarzimal Cave B2F", "Tarzimal Cave B1F", "Tarzimal Cave 1F", "Casba Cave B2F", "Casba Cave B1F", "Casba Cave 1F",
    "Skure Tunnel|Twintown Tunnel", "Fort Guaron B1F", "Fort Guaron 1F",
    "Corona Tower 1F", "Corona Tower 2F", "Corona Tower 3F", "Corona Tower 4F",
    "Dezoris Path 1|Dezoris Path 5 B1F|Laconian Shield Cave B5F", "Dezoris Path 2|Dezoris Path 5 1F|Laconian Shield Cave B4F",
    "Dezoris Path 4|Laconian Shield Cave B3F", "Dezoris Path 3|Laconian Shield Cave B2F",
    "Laconian Shield Cave B1F", "Laconian Shield Cave 1F",
    "Air Castle B4F", "Air Castle B3F", "Air Castle B2F", "Air Castle B1F", "Air Castle 1F", "Aeroprism Cave",
    "Credits Dungeon B1F", "Credits Dungeon 1F",                 /* $3B/$3C: the ending walks here */
};
const char *ps_automap_floor_name(int floor)
{
    return floor >= 0 && floor < (int)(sizeof k_floor_name / sizeof k_floor_name[0]) ? k_floor_name[floor] : NULL;
}

/* word-wrap s[0..len) into lines of <= maxc chars at (x, y); returns the number of lines drawn */
static int text_wrap(uint32_t *fb, int w, int h, int x, int y, const char *s, size_t len, int maxc, uint32_t c, int maxl)
{
    int nl = 0;
    if (maxc < 4) maxc = 4;
    while (len && nl < maxl) {
        size_t n = len <= (size_t)maxc ? len : (size_t)maxc;
        if (n < len) { size_t k = n; while (k > 0 && s[k] != ' ') k--; if (k > 0) n = k; }
        char buf[48]; size_t m = n < sizeof buf - 1 ? n : sizeof buf - 1; memcpy(buf, s, m); buf[m] = 0;
        hud_text(fb, w, h, x, y + nl * 9, buf, c); nl++;
        s += n; len -= n; while (len && *s == ' ') { s++; len--; }
    }
    return nl;
}

/* HUD MAP page: floor `floor` (-1 = the current / last one) with its name(s), position (when the
   party is on it), explored %, the caller's `lines` (nav hints) and the legend while it fits */
void ps_automap_hud_draw_floor(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, int floor,
                               const char *const *lines, int nlines)
{
    const uint32_t WHITE = 0xFFFFFFFF, GREY = 0xFFA0A0B0, CYAN = 0xFF60D0FF;
    DungeonMemory *d = g_cur;
    if (floor >= 0) { d = NULL; for (int i = 0; i < g_work.n; i++) if (g_work.d[i].key == (uint32_t)floor) d = &g_work.d[i]; }
    if (!d) {
        hud_text(fb, w, h, x0 + 8, y0 + 4, "NO DUNGEON EXPLORED YET", GREY);
        hud_text(fb, w, h, x0 + 8, y0 + 18, "THE MAP FILLS IN AS YOU WALK", GREY);
        hud_text(fb, w, h, x0 + 8, y0 + 28, "THROUGH A DUNGEON.", GREY);
        for (int i = 0; i < nlines; i++) hud_text(fb, w, h, x0 + 8, y0 + 46 + 9 * i, lines[i], GREY);
        return;
    }
    DungeonMemory *cur = g_cur; bool in = g_in_dungeon;
    bool here = g_in_dungeon && d == g_cur;
    g_cur = d; g_in_dungeon = here;
    g_hfb = fb; g_hw = w; g_hh = h;
    int avail = (y1 - y0) < (x1 - x0) ? (y1 - y0) : (x1 - x0);
    int cs = avail / 16; if (cs > 8) cs = 8; if (cs < 3) cs = 3;
    int mx = x0 + 2, my = y0 + 1, seen;
    if (hud_hires()) {                                   /* sharp at the window's resolution */
        static MapImg hud_img;
        seen = map_image(&hud_img);
        hud_fill_rgb(mx - 1, my - 1, mx + 16 * cs + 1, my + 16 * cs + 1, C_BG);
        map_image_show(&hud_img, HUD_SPACE_GAME, (float)mx, (float)my, (float)cs);
    } else seen = draw_map(hud_fill_rgb, mx, my, cs);
    g_cur = cur; g_in_dungeon = in;
    /* info + legend to the right of the map */
    int tx = mx + 16 * cs + 7, ty = my, maxc = (x1 - tx) / 6;
    char buf[48];
    const char *nm = ps_automap_floor_name(d->index);
    if (nm) {
        for (const char *p = nm; *p && ty + 8 <= y1; ) {
            const char *e = strchr(p, '|'); size_t len = e ? (size_t)(e - p) : strlen(p);
            ty += 9 * text_wrap(fb, w, h, tx, ty, p, len, maxc, CYAN, 2);
            p += len; if (*p == '|') p++;
        }
    } else { snprintf(buf, sizeof buf, "Floor %02X", d->index); hud_text(fb, w, h, tx, ty, buf, CYAN); ty += 9; }
    ty += 3;
    if (here) { snprintf(buf, sizeof buf, "X%d Y%d %s", g_cur_px, g_cur_py, k_face[g_cur_dir]); hud_text(fb, w, h, tx, ty, buf, WHITE); ty += 9; }
    else if (d == g_cur) { hud_text(fb, w, h, tx, ty, "LAST FLOOR", GREY); ty += 9; }
    snprintf(buf, sizeof buf, "EXPLORED %d%%", seen * 100 / 256); hud_text(fb, w, h, tx, ty, buf, GREY); ty += 9;
    for (int i = 0; i < nlines; i++, ty += 9) hud_text(fb, w, h, tx, ty, lines[i], WHITE);
    ty += 4;
    /* legend: a second glyph = the other state (stairs down, opened chest, event done) */
    static const struct { int k, k2; const char *t; } leg[] = {
        { K_UP, K_DOWN, "STAIRS UP/DN" }, { K_DOOR, -1, "DOOR" }, { K_LOCKED, -1, "LOCKED DOOR" },
        { K_MAGIC, -1, "MAGIC DOOR" }, { K_SECRET, -1, "SECRET WALL" }, { K_PIT, -1, "PIT" },
        { K_CHEST, K_CHEST_OPEN, "CHEST/OPEN" }, { K_EVENT, K_EVENT_DONE, "EVENT/DONE" }, { K_UP, -1, "EXIT/WARP" } };
    int ls = cs < 8 ? cs : 8;                            /* legend glyphs = the map's cell size */
    g_u = 1;
    for (unsigned i = 0; i < sizeof leg / sizeof leg[0] && ty + 7 <= y1; i++, ty += 8) {
        draw_glyph(hud_fill_rgb, leg[i].k, true, tx, ty, ls);
        if (leg[i].k2 >= 0) draw_glyph(hud_fill_rgb, leg[i].k2, true, tx + ls + 1, ty, ls);
        if (i == sizeof leg / sizeof leg[0] - 1) hud_fill_rgb(tx, ty, tx + 2, ty + 2, C_EXIT);
        hud_text(fb, w, h, tx + 2 * ls + 5, ty, leg[i].t, GREY);
    }
}

void ps_automap_hud_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1)
{
    ps_automap_hud_draw_floor(fb, w, h, x0, y0, x1, y1, -1, NULL, 0);
}

/* ---- HUD MAP page navigation: visited maps and explored floors (kept per save slot) ---- */
void ps_automap_note_map(int map, int px, int py)
{
    if (map < 0 || map >= N_MAPS) return;
    g_work.map_seen[map] = 1; g_work.map_px[map] = (int16_t)px; g_work.map_py[map] = (int16_t)py;
}
bool ps_automap_map_known(int map, int *px, int *py)
{
    if (map < 0 || map >= N_MAPS || !g_work.map_seen[map]) return false;
    *px = g_work.map_px[map]; *py = g_work.map_py[map];
    return true;
}
/* the party has stood on cell `pos` of floor `floor` (this save's memory; FLY page: visited places) */
bool ps_automap_cell_visited(int floor, int pos)
{
    if (pos < 0 || pos > 255) return false;
    for (int i = 0; i < g_work.n; i++)
        if (g_work.d[i].key == (uint32_t)floor) return (g_work.d[i].flags[pos] & VISITED) != 0;
    return false;
}
static const DungeonMemory *explored(int floor)
{
    for (int i = 0; i < g_work.n; i++) if (g_work.d[i].key == (uint32_t)floor) return &g_work.d[i];
    return NULL;
}
static bool has_cell(const DungeonMemory *d, uint8_t v)
{
    for (int c = 0; c < 256; c++) if ((d->cells[c] & 0x0F) == v) return true;
    return false;
}
/* Explored floors linked by plain stairs (cell 2 = up to floor+1, 3 = down to floor-1; stairs with
   the event bit lead elsewhere) form one dungeon. */
static bool linked(int f)
{
    const DungeonMemory *a = explored(f), *b = explored(f + 1);
    return a && b && (has_cell(a, 2) || has_cell(b, 3));
}
/* the floors of the dungeon containing `floor`, ascending; 0 if that floor is not explored */
int ps_automap_group(int floor, int *out, int max)
{
    if (!explored(floor)) return 0;
    int lo = floor, hi = floor;
    while (lo > 0 && linked(lo - 1)) lo--;
    while (hi < 255 && linked(hi)) hi++;
    int n = 0;
    for (int f = lo; f <= hi && n < max; f++) out[n++] = f;
    return n;
}
/* the lowest floor of every dungeon explored, ascending */
int ps_automap_groups(int *out, int max)
{
    int n = 0;
    for (int f = 0; f < 256 && n < max; f++) if (explored(f) && !(f > 0 && linked(f - 1))) out[n++] = f;
    return n;
}

/* Draw the current / last dungeon floor into any ARGB buffer (side panel): 16 x 16 cells of cs px
   at (x, y). Returns false when no dungeon was explored yet. */
static uint32_t *g_bfb; static int g_bw, g_bh;
static void buf_fill(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > g_bw) x1 = g_bw; if (y1 > g_bh) y1 = g_bh;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) g_bfb[y * g_bw + x] = 0xFF000000u | rgb;
}
bool ps_automap_draw(uint32_t *fb, int w, int h, int x, int y, int cs)
{
    if (!g_cur) return false;
    if (hud_hires()) {                                   /* the panel: sharp at the window's resolution */
        static MapImg panel_img;
        map_image(&panel_img);
        map_image_show(&panel_img, HUD_SPACE_PANEL, (float)x, (float)y, (float)cs);
        return true;
    }
    g_bfb = fb; g_bw = w; g_bh = h;
    draw_map(buf_fill, x, y, cs);
    return true;
}
/* Save-slot preview: the explored map stored with save slot `slot` (1..5) for the floor in `blk`
   (the slot's copy of RAM $C300-$C6FF: floor $C30D, position $C30C, direction $C30A), chest /
   event states from that copy. Returns false when that slot has no explored map of the floor. */
bool ps_automap_draw_slot(uint32_t *fb, int w, int h, int x, int y, int cs, int slot, const uint8_t *blk)
{
    if (slot < 1 || slot >= N_SLOTS || !blk) return false;
    uint8_t floor = blk[RAM_DNG_IDX - RAM_SAVE_BLK], pos = blk[RAM_DNG_POS - RAM_SAVE_BLK];
    DungeonMemory *d = NULL;
    for (int i = 0; i < g_slot[slot].n; i++) if (g_slot[slot].d[i].key == floor) d = &g_slot[slot].d[i];
    if (!d) return false;
    DungeonMemory *cur = g_cur; int px = g_cur_px, py = g_cur_py, dir = g_cur_dir; bool in = g_in_dungeon;
    g_cur = d; g_cur_px = pos & 15; g_cur_py = pos >> 4; g_cur_dir = blk[RAM_DNG_DIR - RAM_SAVE_BLK] & 3;
    g_in_dungeon = true; g_flag_blk = blk;
    if (hud_hires()) {
        static MapImg slot_img;
        map_image(&slot_img);
        map_image_show(&slot_img, HUD_SPACE_PANEL, (float)x, (float)y, (float)cs);
    } else {
        g_bfb = fb; g_bw = w; g_bh = h;
        draw_map(buf_fill, x, y, cs);
    }
    g_cur = cur; g_cur_px = px; g_cur_py = py; g_cur_dir = dir; g_in_dungeon = in; g_flag_blk = NULL;
    return true;
}
int  ps_automap_floor(void) { return g_cur ? g_cur->index : -1; }
bool ps_automap_here(void)  { return g_in_dungeon; }

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
    fwrite("PSAM\3", 1, 5, f);                           /* v2: key = floor number; v3: + visited maps */
    for (int s = 1; s < N_SLOTS; s++) {
        uint8_t c[2] = { (uint8_t)g_slot[s].n, (uint8_t)(g_slot[s].n >> 8) };
        fwrite(c, 1, 2, f);
        for (int i = 0; i < g_slot[s].n; i++) {
            const DungeonMemory *d = &g_slot[s].d[i];
            uint8_t k[5] = { (uint8_t)d->key, (uint8_t)(d->key >> 8), (uint8_t)(d->key >> 16), (uint8_t)(d->key >> 24), d->index };
            fwrite(k, 1, 5, f); fwrite(d->cells, 1, 256, f); fwrite(d->flags, 1, 256, f);
        }
        fwrite(g_slot[s].map_seen, 1, N_MAPS, f);
        for (int m = 0; m < N_MAPS; m++) {
            uint8_t q[4] = { (uint8_t)g_slot[s].map_px[m], (uint8_t)(g_slot[s].map_px[m] >> 8),
                             (uint8_t)g_slot[s].map_py[m], (uint8_t)(g_slot[s].map_py[m] >> 8) };
            fwrite(q, 1, 4, f);
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
    if (fread(hdr, 1, 5, f) != 5 || memcmp(hdr, "PSAM", 4) || hdr[4] < 1 || hdr[4] > 3) { fclose(f); return; }
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
        if (hdr[4] >= 3) {
            if (fread(g_slot[s].map_seen, 1, N_MAPS, f) != N_MAPS) break;
            for (int m = 0; m < N_MAPS; m++) {
                uint8_t q[4]; if (fread(q, 1, 4, f) != 4) break;
                g_slot[s].map_px[m] = (int16_t)(q[0] | (q[1] << 8)); g_slot[s].map_py[m] = (int16_t)(q[2] | (q[3] << 8));
            }
        }
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
            bool ev; int k = classify(g_cur->index, y * 16 + x, g_cur->cells[y * 16 + x], g_cur->flags[y * 16 + x], &ev);
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
    for (int i = 0; i < g_nobj; i++)                     /* hidden-at-start chests: remember once available */
        if (g_obj[i].hidden && g_obj[i].floor == floor && sms_read8(g_obj[i].flag) != 0xFF) g_cur->flags[g_obj[i].pos] |= AVAIL;
    reveal_around(g_cur, px, py);
    {   /* arrived past stairs (or walked through a door): also show around the stairs/door cell */
        int bx = px - DX[dir], by = py - DY[dir];
        bool jumped = !arrived && g_last_pos >= 0 && g_last_pos != pos
                      && px - 2 * DX[dir] == (g_last_pos & 15) && py - 2 * DY[dir] == (g_last_pos >> 4);
        if ((arrived || jumped) && bx >= 0 && bx < 16 && by >= 0 && by < 16 && is_passage(map[by * 16 + bx]))
            reveal_around(g_cur, bx, by);
    }
    g_last_pos = pos;
    {   /* what the 3D view shows (LABEL_6AED / LABEL_6C46, cell offsets LABEL_6C06): cells 1..3 ahead,
           each with its left/right neighbours while the way is open; the first non-empty cell ahead
           is drawn as a front face and ends the view; the 4th cell ahead only shows if it is not empty */
        int lx = DY[dir], ly = -DX[dir];                 /* left of the facing direction */
        for (int d = 1; d <= 4; d++) {
            int x = px + DX[dir] * d, y = py + DY[dir] * d;
            if (x < 0 || x > 15 || y < 0 || y > 15) break;
            bool open = (map[y * 16 + x] & 7) == 0;
            if (d == 4) { if (!open) reveal(g_cur, x, y, SEEN); break; }
            reveal(g_cur, x, y, SEEN);
            if (!open) break;
            reveal(g_cur, x + lx, y + ly, SEEN);
            reveal(g_cur, x - lx, y - ly, SEEN);
        }
    }
    ascii_dump(g_cur_px, g_cur_py, g_cur_dir);
    if (g_window_on) window_update();
}
