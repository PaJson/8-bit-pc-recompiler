/*
 * ps_fly.c - Phantasy Star (SMS) HUD "FLY" page: Alis's Fly spell to any town or dungeon.
 *
 * The game's Fly (MapMagicList: Alis's 2nd map spell, MPCostData[$12] = 8 MP; handler LABEL_2140)
 * takes the party to the last church visited: the church sets $C317 (LABEL_2A85), Fly refuses
 * if Interaction_Type = 0, else sets $C2D8 = 8 and the player menu exit (LABEL_1C15) does
 * Game_mode = 8 (LoadMap), $C30E = 0 (vehicle), sound $BF and LABEL_787B with the 3-byte record
 * LABEL_1C7C[$C317] = (map, y, x) (9 church towns). LABEL_787B: $C308 = $C309 = map, camera
 * from the party cell (y: high nibble = screen row, low nibble = cell row 0..11; x: cell 0..127),
 * then LABEL_7908 clears the object RAM ($C800-$C8FF) and $C26F-$C286 and copies $C810 to $C2D7.
 *
 * Towns: the same for any town (landing: the game's Fly spot for the 9 church towns, else the town
 * gate from the planet entrance tables (LABEL_DF1 + 12 in bank 3: 6-byte records y, x, type, a, b,
 * c; type 8 = LoadMap with a LABEL_787B record), else the party's last position there).
 * Dungeons: the entrance tables' type $0A records (a = floor, b = cell, c = facing): entering one
 * on foot (LABEL_78BD) does Game_mode = $0A (LoadDungeon), Dungeon_position/index/direction =
 * b/a/c, and the map camera = the camera from before the step onto the entrance ($C311/$C313),
 * which is where you come out again. This page writes the same: floor, cell, facing, and the
 * "return point" = the map cell one step outside the entrance (against the facing). Four places
 * are only entered by scripts (no entrance record): the Governor's Palace basement (LABEL_4C70:
 * floor $22 cell $E6, then the pit "fall" = $21), the credits dungeon (ending: $3D/$F7, fall =
 * $3C), the jail of the fake-Roadpass robot (LABEL_534B: $15/$9C) and the Spaceport side of the
 * Gothic Tunnel (LABEL_792A: $00/$DE; the tunnel is also entered from Gothic). Several floors are
 * shared by different places (e.g. layout $00 = Medusa Cave, Gothic Tunnel and Baya Malay Pass as
 * three separate parts of one 16 x 16 floor); the names below are per entrance (Tools/ps_dungeons.py
 * splits every floor into its walkable parts and lists which entrance / stairs / pit reaches each).
 *
 * Options (ps_qol.c, OPTIONS tab): FLY LIST = VISITED (towns and dungeons this save has been to:
 * ps_automap's visited maps / visited cells) or ALL; FLY COST = SPELL (the game's rules: on a map,
 * Alis alive, Fly learned, 8 MP, not where the game refuses) or FREE (no spell, no MP, also from
 * inside a dungeon). The warp runs in [insn_hooks] hooks: ps_fly_go at the end of GameMode_Map
 * (`ld a,(Ctrl_1_held) ; and $30 ; ret z`, Rev A $0B35 = where the game opens the player menu,
 * standing still after the battle check) and ps_fly_dungeon_go at GameMode_Dungeon's
 * `call LABEL_65EE` (after its WaitForVInt; resume = its `ret z`): the RAM writes of the game's own
 * paths, then `ret z` returns to the main loop, which runs GameMode_LoadMap / LoadDungeon.
 * Test env: PS_FLY_DEBUG=1 (destination table + flights).
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hud.h"
#include "sms_runtime.h"
#include "z80_ops.h"

extern uint8_t *glue_rom_image(size_t *size);
extern int         ps_worldmap_num_places(void);
extern const char *ps_worldmap_place_name(int place);
extern int         ps_worldmap_place_tileset(int place);
extern int         ps_worldmap_town_place(int tileset, int sx, int sy);
extern int         ps_worldmap_tileset_of(int map);
extern long        ps_worldmap_df1(void);
extern bool        ps_automap_map_known(int map, int *px, int *py);
extern bool        ps_automap_cell_visited(int floor, int pos);
extern const char *ps_automap_floor_name(int floor);
extern int         ps_fly_opt_all(void);     /* ps_qol.c: FLY LIST = ALL */
extern int         ps_fly_opt_free(void);    /* ps_qol.c: FLY COST = FREE */

#define RAM_GAME_MODE 0xC202
#define RAM_MOVE_CNT  0xC265
#define RAM_ALIS      0xC400                 /* +0 status (bit 0 alive), +2 MP, +7 max MP, +$F map spells */
#define MODE_MAP      0x09
#define MODE_DUNGEON  0x0B
#define MAX_DEST      96

typedef struct {
    const char *name, *type;
    int     place, group;                    /* town place (-1 = dungeon); group 0 Palma, 1 Motavia, 2 Dezoris, 3 Special */
    int     map;                             /* -1 = no landing known */
    uint8_t y, x;                            /* LABEL_787B record: town landing, or a dungeon's return point */
    int     kind;                            /* towns: 0 church (Fly), 1 gate, 2 last position; dungeons: 3 entrance, 4 script */
    int     floor, pos, dir;                 /* dungeons (floor >= 0) */
    const char *ret_town;                    /* script dungeons: the return point = this town's landing */
} Dest;

static const uint8_t *g_rom; static size_t g_rom_n;
static long     s_fly_tbl = -1;              /* LABEL_1C7C */
static int      s_cost = 8;
static uint16_t s_it_addr;                   /* Interaction_Type */
static Dest     s_dest[MAX_DEST]; static int s_ndest;
static int      s_pending = -1, s_pending_age; /* index into s_dest */
static char     s_msg[48];                   /* last refusal, shown on the page */
static long     s_abf = -1;                  /* LABEL_ABF: spaceship routes */
static unsigned s_frame, s_dng_hook_frame;   /* the dungeon hook exists in this build if it ran lately */

/* dungeon names per entrance (floor, cell): layouts are shared by different places (ps_dungeons.py).
   The pairs below are guesses from the entrance positions where two caves share a floor
   (Naula = north of Scion, Iala = south; Dezoris Paths 1-5 from their mouths / stairs). */
static const struct { uint8_t floor, pos; const char *name, *type; } k_dng[] = {
    { 0x00, 0x15, "Baya Malay Pass", "pass" }, { 0x00, 0x5A, "Baya Malay Pass", "pass" },
    { 0x00, 0xDC, "Medusa Cave", "cave" }, { 0x00, 0x2E, "Gothic Tunnel", "tunnel" },
    { 0x01, 0xD2, "Triada Prison", "prison" },
    { 0x02, 0x5E, "Camineet Warehouse", "warehouse" }, { 0x02, 0x12, "Abion Warehouse", "warehouse" }, { 0x02, 0xDE, "Abion Warehouse", "warehouse" },
    { 0x04, 0xD8, "Medusa Tower", "tower" }, { 0x0B, 0xB9, "Baya Malay Tower", "tower" },
    { 0x13, 0x13, "Naula Cave", "cave" }, { 0x13, 0xDE, "Iala Cave", "cave" },
    { 0x14, 0xCB, "Baya Malay Prison", "prison" }, { 0x14, 0x21, "Baya Malay Prison", "prison" },
    { 0x15, 0xD1, "Drasgow Warehouse", "warehouse" }, { 0x15, 0xD8, "Dr. Mad's Lab", "lab" },
    { 0x16, 0x21, "Bortevo Pass", "pass" }, { 0x16, 0xDE, "Bortevo Pass", "pass" },
    { 0x17, 0xD7, "Island Tower", "tower" },
    { 0x1E, 0x69, "Maharu Cave", "cave" }, { 0x24, 0xD9, "Tarzimal Cave", "cave" },
    { 0x27, 0xD3, "Casba Cave", "cave" }, { 0x27, 0xDC, "Casba Cave", "cave" },
    { 0x21, 0x61, "Path to the Governor", "passage" }, { 0x21, 0xD4, "Path to the Governor", "passage" },
    { 0x28, 0x23, "Skure Tunnel", "tunnel" }, { 0x28, 0x6B, "Skure Tunnel", "tunnel" },
    { 0x28, 0xDD, "Twintown Tunnel", "tunnel" }, { 0x28, 0x87, "Twintown Tunnel", "tunnel" },
    { 0x2A, 0xDD, "Fort Guaron", "fort" }, { 0x2B, 0xD1, "Corona Tower", "tower" },
    { 0x34, 0xD3, "Laconian Shield Cave", "cave" },
    { 0x2F, 0xD2, "Dezoris Path 1", "path" }, { 0x2F, 0xDD, "Dezoris Path 1", "path" },
    { 0x30, 0x87, "Dezoris Path 2", "path" }, { 0x30, 0xED, "Dezoris Path 2", "path" },
    { 0x32, 0x59, "Dezoris Path 3", "path" }, { 0x32, 0xDB, "Dezoris Path 3", "path" },
    { 0x31, 0x1C, "Dezoris Path 4", "path" }, { 0x31, 0xE2, "Dezoris Path 4", "path" },
    { 0x30, 0x2E, "Dezoris Path 5", "path" }, { 0x30, 0x79, "Dezoris Path 5", "path" },
    { 0x3A, 0xDD, "Aeroprism Cave", "cave" }, { 0x39, 0xD6, "Air Castle", "castle" },
};
/* script-only places: floor, cell, facing, group, name, type, return town */
static const struct { uint8_t floor, pos, dir, group; const char *name, *type, *ret_town; } k_script[] = {
    { 0x21, 0xE6, 0, 1, "Governor's Palace", "basement", "Paseo" },
    { 0x15, 0x9C, 1, 0, "Roadpass Jail", "jail", "Camineet" },
    { 0x3C, 0xF7, 0, 3, "Credits Dungeon", "ending", "Camineet" },
};

/* During a spaceship flight (GameMode_LoadShip / GameMode_Ship, route $C2E9 & $7F = 1-based entry
   of LABEL_ABF: departure planet record, arrival planet record, town record): the take-off /
   landing point on `planet` in world pixels (centre of the party cell). False if unknown. */
bool ps_fly_ship_pos(int planet, int *px, int *py)
{
    int k = sms_read8(0xC2E9) & 0x7F;
    if (s_abf < 0 || k < 1 || k > 8 || !g_rom) return false;
    const uint8_t *e = g_rom + s_abf + 9 * (k - 1);
    const uint8_t *r = e[0] == planet ? e : e[3] == planet ? e + 3 : NULL;
    if (!r) return false;
    *px = (r[2] & 0x7F) * 16 + 8;
    *py = ((r[1] >> 4) & 7) * 192 + (r[1] & 15) * 16 + 8;
    return true;
}

static long find_pat_n(const char *hex, bool first)
{
    int pat[64], np = 0;
    for (const char *p = hex; *p && np < 64; ) {
        while (*p == ' ') p++;
        if (!*p) break;
        pat[np++] = p[0] == '.' ? -1 : (int)strtol((char[3]){ p[0], p[1], 0 }, NULL, 16);
        p += 2;
    }
    long hit = -1;
    for (size_t i = 0; g_rom && i + (size_t)np <= g_rom_n && i < 0x8000; i++) {
        int k = 0; while (k < np && (pat[k] < 0 || g_rom[i + k] == pat[k])) k++;
        if (k == np) { if (first) return (long)i; if (hit >= 0) return -1; hit = (long)i; }
    }
    return hit;
}
static long find_pat(const char *hex) { return find_pat_n(hex, false); }        /* must be unique */
static long find_pat_first(const char *hex) { return find_pat_n(hex, true); }

static void areas_init(void);

static Dest *dest_of(int place)
{
    for (int i = 0; i < s_ndest; i++) if (s_dest[i].place == place) return &s_dest[i];
    return NULL;
}
static Dest *town_named(const char *name)
{
    for (int i = 0; i < s_ndest; i++) if (s_dest[i].place >= 0 && !strcmp(s_dest[i].name, name)) return &s_dest[i];
    return NULL;
}
static int place_of_record(const uint8_t *r)  /* (map, y, x) -> town place, -1 */
{
    int ts = ps_worldmap_tileset_of(r[0]);
    if (ts < 4) return -1;
    return ps_worldmap_town_place(ts, (r[2] & 0x7F) >> 4, (r[1] >> 4) & 7);
}
static const char *town_type(const char *name)
{
    return strstr(name, "Spaceport") ? "spaceport" : !strcmp(name, "Air Castle") ? "grounds" : "town";
}

/* the map cell one step from (yb, xb) against facing `dir` (0 north, 1 east, 2 south, 3 west):
   where the party stood before stepping onto a dungeon entrance = where it comes out again */
static void step_back(uint8_t *yb, uint8_t *xb, int dir)
{
    int row = ((*yb >> 4) & 7) * 12 + (*yb & 15), col = *xb & 0x7F;
    if (dir == 0) row++; else if (dir == 2) row--; else if (dir == 1) col--; else col++;
    row = (row + 96) % 96; col &= 0x7F;
    *yb = (uint8_t)((row / 12) << 4 | (row % 12)); *xb = (uint8_t)col;
}

void ps_fly_init(void)
{
    g_rom = glue_rom_image(&g_rom_n);
    s_ndest = 0; s_pending = -1; s_msg[0] = 0; s_fly_tbl = -1; s_it_addr = 0; s_abf = -1;
    if (!g_rom || g_rom_n < 0x10000) return;
    /* player menu exit, Fly: ld a,($C317) ; ld l,a ; add a,a ; add a,l ; ld h,0 ; ld l,a ; ld de,LABEL_1C7C ; add hl,de ; jp LABEL_787B */
    long f = find_pat("3a 17 c3 6f 87 85 26 00 6f 11 .. .. 19 c3");
    if (f >= 0) s_fly_tbl = g_rom[f + 10] | (g_rom[f + 11] << 8);
    long m = find_pat("00 02 06 06 0a 04 10 0c 04 02 0a 02 02 04 04 0c 02 04 08");   /* MPCostData */
    if (m >= 0) s_cost = g_rom[m + 0x12];
    long b = find_pat("3a 08 c3 fe 04 21 .. .. 30 .. 3a .. .. b7 28 .. c5 d5 cd .. .. d1 c1 21 .. .. 20 .. 3e 08 32 0e c3");   /* ItemUse_Hovercraft */
    if (b >= 0) s_it_addr = (uint16_t)(g_rom[b + 11] | (g_rom[b + 12] << 8));
    long df1 = ps_worldmap_df1();
    if (s_fly_tbl < 0 || !s_it_addr || df1 < 0) {
        fprintf(stderr, "[fly] tables not found (fly %ld, it %04X, df1 %ld): no FLY page\n", s_fly_tbl, s_it_addr, df1);
        return;
    }
    for (int p = 4; p < ps_worldmap_num_places() && s_ndest < MAX_DEST; p++) {
        const char *nm = ps_worldmap_place_name(p);
        s_dest[s_ndest++] = (Dest){ nm, town_type(nm), p, 3, -1, 0, 0, 2, -1, 0, 0, NULL };
    }
    /* 1. the game's Fly spots (church towns) */
    for (int k = 0; k < 9; k++) {
        const uint8_t *r = g_rom + s_fly_tbl + 3 * k;
        Dest *d = dest_of(place_of_record(r));
        if (d && d->map < 0) { d->map = r[0]; d->y = r[1]; d->x = r[2]; d->kind = 0; }
    }
    /* 2. planet entrance tables: the planet of each town, and its gate */
    for (int t = 0; t < 4; t++) {
        const uint8_t *e = g_rom + df1 + t * 14;
        long o = 3L * 0x4000 + (long)((e[12] | (e[13] << 8)) - 0x8000);
        for (int n = 0; n < 256 && o + 6 <= (long)g_rom_n && g_rom[o] != 0xFF; n++, o += 6) {
            const uint8_t *r = g_rom + o;
            if (r[2] != 0x08) continue;
            Dest *d = dest_of(place_of_record(r + 3));
            if (!d) continue;
            d->group = t < 2 ? t : 2;
            if (d->map < 0) { d->map = r[3]; d->y = r[4]; d->x = r[5]; d->kind = 1; }
        }
    }
    /* 3. the spaceship table (LABEL_ABF, 8 x 9 bytes: departure planet, arrival planet, town record):
       the spaceports' landing spots and planets. GameMode_LoadShip: ld a,($C2E9) ; and $7F ; ld l,a ;
       add a,a x3 ; add a,l ; ld l,a ; ld h,0 ; ld de,LABEL_ABF-9 ; add hl,de ; ld de,LABEL_A8C ;
       push de ; call LABEL_787B (the first of two such sites; the other uses LABEL_ABF-6) */
    long s = find_pat_first("3a e9 c2 e6 7f 6f 87 87 87 85 6f 26 00 11 .. .. 19 11 .. .. d5 cd");
    if (s >= 0) {
        long abf = (long)(g_rom[s + 14] | (g_rom[s + 15] << 8)) + 9;
        s_abf = abf;
        for (int k = 0; k < 8 && abf + 9 * k + 9 <= 0x8000; k++) {
            const uint8_t *e = g_rom + abf + 9 * k;
            Dest *d = dest_of(place_of_record(e + 6));
            if (!d) continue;
            if (d->group == 3 && e[3] < 4) d->group = e[3] < 2 ? e[3] : 2;
            if (d->map < 0) { d->map = e[6]; d->y = e[7]; d->x = e[8]; d->kind = 1; }
        }
    }
    /* towns that exist twice (Dezoris: maps 19/20 with gates, 21/22 with the church spots) share the planet */
    for (int i = 0; i < s_ndest; i++) if (s_dest[i].group == 3)
        for (int j = 0; j < s_ndest; j++)
            if (s_dest[j].group != 3 && !strcmp(s_dest[i].name, s_dest[j].name)) s_dest[i].group = s_dest[j].group;
    /* 4. dungeon entrances (type $0A) of every tileset's list: planets 0-3 directly, towns via the
       town the entrance is in (its planet group and map number). The lists overlap (Palma's runs on
       through Motavia's, Dezoris's and the towns' up to one $FF): a record belongs to the list that
       starts closest before it. */
    long lst[11];
    for (int t = 0; t < 11; t++) {
        const uint8_t *e = g_rom + df1 + t * 14;
        lst[t] = 3L * 0x4000 + (long)((e[12] | (e[13] << 8)) - 0x8000);
        if (lst[t] < 3L * 0x4000 || lst[t] >= 4L * 0x4000 || lst[t] >= (long)g_rom_n) lst[t] = -1;
    }
    for (int t = 0; t < 11 && s_ndest < MAX_DEST; t++) {
        long o = lst[t];
        if (o < 0) continue;
        for (int n = 0; n < 256 && o + 6 <= (long)g_rom_n && g_rom[o] != 0xFF && s_ndest < MAX_DEST; n++, o += 6) {
            const uint8_t *r = g_rom + o;
            if (r[2] != 0x0A) continue;
            int own = t;
            for (int u = 0; u < 11; u++) if (lst[u] >= 0 && lst[u] <= o && (lst[u] > lst[own] || (lst[u] == lst[own] && u < own))) own = u;
            if (own != t) continue;
            Dest d = { NULL, "dungeon", -1, 3, -1, r[0], r[1], 3, r[3], r[4], r[5] & 3, NULL };
            if (t < 4) { d.group = t < 2 ? t : 2; d.map = t; }
            else {
                Dest *town = dest_of(ps_worldmap_town_place(t, (r[1] & 0x7F) >> 4, (r[0] >> 4) & 7));
                if (town) { d.group = town->group; d.map = town->map; }
                if (d.map < 0) for (int k = 4; k < 32; k++) if (ps_worldmap_tileset_of(k) == t) { d.map = k; break; }
            }
            step_back(&d.y, &d.x, d.dir);
            for (unsigned k = 0; k < sizeof k_dng / sizeof k_dng[0]; k++)
                if (k_dng[k].floor == r[3] && k_dng[k].pos == r[4]) { d.name = k_dng[k].name; d.type = k_dng[k].type; }
            if (!d.name) {                          /* unknown entrance: the floor's first name */
                static char buf[8][32]; static int nb;
                const char *fn = ps_automap_floor_name(r[3]), *bar = fn ? strchr(fn, '|') : NULL;
                char *o2 = buf[nb++ & 7];
                snprintf(o2, 32, "%.*s", fn ? (int)(bar ? bar - fn : (long)strlen(fn)) : 0, fn ? fn : "");
                if (!o2[0]) snprintf(o2, 32, "Floor %02X", r[3]);
                d.name = o2;
            }
            s_dest[s_ndest++] = d;
        }
    }
    /* 5. script-only places */
    for (unsigned k = 0; k < sizeof k_script / sizeof k_script[0] && s_ndest < MAX_DEST; k++)
        s_dest[s_ndest++] = (Dest){ k_script[k].name, k_script[k].type, -1, k_script[k].group, -1, 0, 0, 4,
                                    k_script[k].floor, k_script[k].pos, k_script[k].dir, k_script[k].ret_town };
    areas_init();
    if (getenv("PS_FLY_DEBUG"))
        for (int i = 0; i < s_ndest; i++)
            fprintf(stderr, "[fly] %-22s %-9s group %d map %2d y %02X x %02X kind %d floor %02X pos %02X dir %d\n",
                    s_dest[i].name, s_dest[i].type, s_dest[i].group, s_dest[i].map, s_dest[i].y, s_dest[i].x,
                    s_dest[i].kind, s_dest[i].floor & 0xFF, s_dest[i].pos & 0xFF, s_dest[i].dir);
}

/* ---- the name of the place a dungeon cell belongs to ----
   Many layouts are shared (floor $00 = Medusa Cave + Gothic Tunnel + Baya Malay Pass as separate
   parts of one 16 x 16 floor, Medusa Tower 5F-7F = Baya Malay Tower B3F-B1F ...). Every floor of
   B15_DungeonLayouts (128 bytes, a nibble per cell, found by the first floor's bytes) is split into
   4-connected walkable parts; each dungeon destination (entrance / script) names the part it lands
   in, and the names spread through stairs (cell 2: floor + 1, 3: floor - 1, same cell) and pits
   (cell 8 without an object: floor - 1). The shown name = the floor-name variant that starts with the
   dungeon's name ("Laconian Shield Cave" on $2F -> "Laconian Shield Cave B5F"), else the dungeon's
   name itself (Roadpass Jail). Same method as Tools/ps_dungeons.py. */
#define NFLOORS 64
extern bool ps_automap_has_object(int floor, int pos);
static uint8_t     s_part[NFLOORS][256];    /* part + 1 per cell, 0 = wall / no data */
static const char *s_area[NFLOORS][128];    /* (floor, part) -> shown name */
static char        s_area_pool[4096]; static size_t s_area_used;

static const char *area_string(int floor, const char *base)
{
    const char *fn = ps_automap_floor_name(floor), *pick = NULL; size_t plen = 0, bl = strlen(base);
    for (const char *p = fn; p && *p; ) {
        size_t len = strcspn(p, "|");
        if (len >= bl && !strncmp(p, base, bl) && (len == bl || p[bl] == ' ')) { pick = p; plen = len; break; }
        p += len; if (*p == '|') p++;
    }
    if (!pick) { pick = base; plen = bl; }
    if (s_area_used + plen + 1 > sizeof s_area_pool) return base;
    char *o = s_area_pool + s_area_used;
    memcpy(o, pick, plen); o[plen] = 0; s_area_used += plen + 1;
    return o;
}
static void areas_init(void)
{
    static const uint8_t sig[] = { 0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,0xA0,0x00,0x01,0x01,0x11,0xA1 };
    memset(s_part, 0, sizeof s_part); memset(s_area, 0, sizeof s_area); s_area_used = 0;
    long lo = -1;
    for (size_t i = 0; i + sizeof sig <= g_rom_n; i++) if (!memcmp(g_rom + i, sig, sizeof sig)) { lo = (long)i; break; }
    if (lo < 0) return;
    int nf = 0;
    for (int f = 0; f < NFLOORS && lo + 128L * (f + 1) <= (long)g_rom_n && ps_automap_floor_name(f); f++, nf++) {
        uint8_t c[256], st[256];
        for (int p = 0; p < 256; p++) { uint8_t b = g_rom[lo + 128 * f + p / 2]; c[p] = p & 1 ? b & 15 : b >> 4; }
        int n = 0;
        for (int p = 0; p < 256 && n < 127; p++) {
            if (c[p] == 1 || s_part[f][p]) continue;
            int sp = 0; st[sp++] = (uint8_t)p; s_part[f][p] = (uint8_t)(n + 1);
            while (sp) {
                int q = st[--sp], y = q >> 4, x = q & 15;
                int nb[4] = { y > 0 ? q - 16 : -1, y < 15 ? q + 16 : -1, x > 0 ? q - 1 : -1, x < 15 ? q + 1 : -1 };
                for (int k = 0; k < 4; k++)
                    if (nb[k] >= 0 && c[nb[k]] != 1 && !s_part[f][nb[k]]) { s_part[f][nb[k]] = (uint8_t)(n + 1); st[sp++] = (uint8_t)nb[k]; }
            }
            n++;
        }
    }
    /* seeds: the dungeon destinations in table order (entrances, then scripts); then spread */
    static const char *base[NFLOORS][128];
    memset(base, 0, sizeof base);
    int qf[NFLOORS * 128], qp[NFLOORS * 128], qn = 0;
    for (int i = 0; i < s_ndest; i++) {
        const Dest *d = &s_dest[i];
        if (d->floor < 0 || d->floor >= nf || !s_part[d->floor][d->pos]) continue;
        int pt = s_part[d->floor][d->pos] - 1;
        if (!base[d->floor][pt]) { base[d->floor][pt] = d->name; qf[qn] = d->floor; qp[qn] = pt; qn++; }
    }
    for (int k = 0; k < qn; k++) {
        int f = qf[k], pt = qp[k];
        for (int p = 0; p < 256; p++) {
            if (s_part[f][p] != pt + 1) continue;
            uint8_t b = g_rom[lo + 128 * f + p / 2], t = p & 1 ? b & 15 : b >> 4;
            int tf = t == 2 ? f + 1 : t == 3 ? f - 1 : (t == 8 && !ps_automap_has_object(f, p)) ? (f > 0 ? f - 1 : 0) : -1;
            if (tf < 0 || tf >= nf || tf == f || !s_part[tf][p]) continue;
            int tp = s_part[tf][p] - 1;
            if (!base[tf][tp] && qn < NFLOORS * 128) { base[tf][tp] = base[f][pt]; qf[qn] = tf; qp[qn] = tp; qn++; }
        }
    }
    for (int f = 0; f < nf; f++) for (int pt = 0; pt < 128; pt++) if (base[f][pt]) s_area[f][pt] = area_string(f, base[f][pt]);
    if (getenv("PS_FLY_DEBUG"))
        for (int f = 0; f < nf; f++) for (int pt = 0; pt < 128; pt++) if (s_area[f][pt]) fprintf(stderr, "[fly] area %02X part %d: %s\n", f, pt, s_area[f][pt]);
}
/* the place cell `pos` of floor `floor` belongs to ("Gothic Tunnel", "Medusa Tower 6F"); NULL = unknown */
const char *ps_fly_area_name(int floor, int pos)
{
    if (floor < 0 || floor >= NFLOORS || pos < 0 || pos > 255 || !s_part[floor][pos]) return NULL;
    return s_area[floor][s_part[floor][pos] - 1];
}

/* the map record of a destination: (map, y, x) = a town's landing / a dungeon's return point */
static bool landing(const Dest *d, uint8_t r[3])
{
    if (d->ret_town) { const Dest *t = town_named(d->ret_town); return t && t != d && landing(t, r); }
    if (d->map >= 0) { r[0] = (uint8_t)d->map; r[1] = d->y; r[2] = d->x; return true; }
    if (d->place < 0) return false;
    int px, py;                              /* last position there; map = the tileset's only map */
    if (!ps_automap_map_known(d->place, &px, &py)) return false;
    int ts = ps_worldmap_place_tileset(d->place), map = -1;
    for (int k = 4; k < 32; k++) if (ps_worldmap_tileset_of(k) == ts) { if (map >= 0) return false; map = k; }
    if (map < 0 || px < 0 || py < 0) return false;
    r[0] = (uint8_t)map; r[2] = (uint8_t)((px >> 4) & 0x7F);
    r[1] = (uint8_t)(((py / 192) & 7) << 4 | ((py % 192) >> 4));
    return true;
}

static bool visited(const Dest *d)
{
    int px, py;
    if (d->floor >= 0) return ps_automap_cell_visited(d->floor, d->pos);
    return ps_automap_map_known(d->place, &px, &py);
}

/* why Fly is not possible now (NULL = possible) */
static const char *refusal(void)
{
    uint8_t mode = sms_read8(RAM_GAME_MODE);
    if (s_fly_tbl < 0) return "NOT AVAILABLE IN THIS VERSION";
    if (ps_fly_opt_free()) {
        if (mode == MODE_MAP) return NULL;
        if (mode != MODE_DUNGEON) return "ONLY ON A MAP OR IN A DUNGEON";
        if (s_frame - s_dng_hook_frame > 120) return "NOT FROM A DUNGEON IN THIS VERSION";
        return NULL;
    }
    if (mode != MODE_MAP) return "ONLY WHILE WALKING ON A MAP";
    if (!(sms_read8(RAM_ALIS) & 1)) return "ALIS CANNOT CAST NOW";
    if (sms_read8(RAM_ALIS + 0x0F) < 2) return "ALIS HAS NOT LEARNED FLY";
    if (sms_read8(RAM_ALIS + 2) < s_cost) return "NOT ENOUGH MP";
    if (!sms_read8(s_it_addr)) return "CANNOT FLY FROM HERE";
    return NULL;
}

/* LABEL_787B's camera writes for a party cell record (map, y, x), + LABEL_7908 */
static void land_map_record(const uint8_t r[3])
{
    sms_write8(0xC308, r[0]); sms_write8(0xC309, r[0]);
    unsigned hl = (unsigned)r[1] * 16u;
    uint8_t l = (uint8_t)hl, h = (uint8_t)(hl >> 8), a = (uint8_t)(l - 0x60);
    if (l < 0x60 || a >= 0xC0) { a = (uint8_t)(a - 0x40); h--; }
    h &= 7;
    sms_write8(0xC305, a); sms_write8(0xC306, h); sms_write8(0xC311, a); sms_write8(0xC312, h);
    unsigned x = (unsigned)((r[2] - 8) & 0x7F) * 16u;
    sms_write8(0xC301, (uint8_t)x); sms_write8(0xC302, (uint8_t)(x >> 8));
    sms_write8(0xC313, (uint8_t)x); sms_write8(0xC314, (uint8_t)(x >> 8));
    sms_write8(0xC30E, 0);
    /* LABEL_7908 */
    sms_write8(0xC2D7, sms_read8(0xC810));
    for (uint16_t p = 0xC26F; p <= 0xC286; p++) sms_write8(p, 0);
    for (uint16_t p = 0xC800; p <= 0xC8FF; p++) sms_write8(p, 0);
}

/* the pending flight, if it can start now; true = RAM written, the caller returns to the main loop */
static bool do_pending(void)
{
    if (s_pending < 0) return false;
    Dest *d = &s_dest[s_pending];
    s_pending = -1;
    uint8_t r[3];
    if (refusal() || !landing(d, r)) return false;
    if (!ps_fly_opt_free()) sms_write8(RAM_ALIS + 2, (uint8_t)(sms_read8(RAM_ALIS + 2) - s_cost));
    land_map_record(r);
    if (d->floor >= 0) {                       /* LABEL_78BD: into the dungeon */
        sms_write8(0xC30C, (uint8_t)d->pos); sms_write8(0xC30D, (uint8_t)d->floor); sms_write8(0xC30A, (uint8_t)d->dir);
        sms_write8(RAM_GAME_MODE, 0x0A);       /* GameMode_LoadDungeon */
    } else sms_write8(RAM_GAME_MODE, 0x08);    /* GameMode_LoadMap */
    sms_write8(0xC004, 0xBF);                  /* the Fly departure sound */
    if (getenv("PS_FLY_DEBUG")) fprintf(stderr, "[fly] to %s (%s): map %d y %02X x %02X floor %d pos %02X dir %d\n",
                                        d->name, d->type, r[0], r[1], r[2], d->floor, d->pos & 0xFF, d->dir);
    return true;
}

/* [insn_hooks] ps_fly_go @ GameMode_Map `ld a,(Ctrl_1_held)` -> resume at its `ret z` */
extern int ps_vehicle_go(void);              /* ps_qol.c: quick vehicles (LB / RB) share this hook */
int ps_fly_go(void)
{
    if (ps_vehicle_go()) return 1;
    if (s_pending < 0 || sms_read8(RAM_MOVE_CNT)) return 0;   /* mid-step: next frame */
    if (!do_pending()) return 0;
    z80f_set(&g_z80, Z80_FLAG_Z, true);        /* -> ret z */
    return 1;
}
/* [insn_hooks] ps_fly_dungeon_go @ GameMode_Dungeon `call LABEL_65EE` -> resume at its `ret z` */
int ps_fly_dungeon_go(void)
{
    s_dng_hook_frame = s_frame;
    if (s_pending < 0 || !do_pending()) return 0;
    z80f_set(&g_z80, Z80_FLAG_Z, true);
    return 1;
}

/* per frame: drop a pending flight that could not start (e.g. a battle came first) */
void ps_fly_frame(void)
{
    s_frame++;
    if (s_pending >= 0 && ++s_pending_age > 30) s_pending = -1;
}

/* ---- HUD page: one scrolling list with planet headers ---- */
static const char *const k_group[4] = { "PALMA", "MOTAVIA", "DEZORIS", "SPECIAL" };
typedef struct { int dest, group; bool header; } Row;
#define MAX_ROWS (MAX_DEST + 4)
static int s_sel, s_top;                     /* selected row, first row shown */

static int build_rows(Row *rows)
{
    int n = 0;
    bool all = ps_fly_opt_all();
    for (int g = 0; g < 4; g++) {
        int start = n + 1;
        for (int i = 0; i < s_ndest && n < MAX_ROWS - 1; i++) {
            const Dest *d = &s_dest[i];
            uint8_t r[3];
            if (d->group != g || !landing(d, r) || (!all && !visited(d))) continue;
            if (n < start) rows[n++] = (Row){ -1, g, true };
            bool dup = false;                  /* same name + type (two entrances, two Dezoris maps) */
            for (int k = start; k < n; k++) {
                const Dest *o = &s_dest[rows[k].dest];
                if (!strcmp(o->name, d->name) && !strcmp(o->type, d->type)) {
                    dup = true;
                    if (d->kind < o->kind || (d->place < 0 && visited(d) && !visited(o))) rows[k].dest = i;
                }
            }
            if (dup) continue;
            int k = n++;                       /* insertion sort by name */
            while (k > start && strcmp(s_dest[rows[k - 1].dest].name, d->name) > 0) { rows[k] = rows[k - 1]; k--; }
            rows[k] = (Row){ i, g, false };
        }
    }
    return n;
}
static int first_entry(const Row *rows, int n, int from, int step)
{
    for (int k = from; k >= 0 && k < n; k += step) if (!rows[k].header) return k;
    return -1;
}

void ps_fly_hud_key(int key)
{
    Row rows[MAX_ROWS]; int n = build_rows(rows);
    if (!n) return;
    if (s_sel >= n || rows[s_sel].header) s_sel = first_entry(rows, n, 0, 1) < 0 ? 0 : first_entry(rows, n, 0, 1);
    if (key == HUD_KEY_UP || key == HUD_KEY_DOWN) {
        int k = first_entry(rows, n, s_sel + (key == HUD_KEY_DOWN ? 1 : -1), key == HUD_KEY_DOWN ? 1 : -1);
        if (k < 0) k = first_entry(rows, n, key == HUD_KEY_DOWN ? 0 : n - 1, key == HUD_KEY_DOWN ? 1 : -1);
        if (k >= 0) s_sel = k;
    }
    else if (key == HUD_KEY_LEFT || key == HUD_KEY_RIGHT) {   /* the previous / next planet's first entry */
        int g = rows[s_sel].group, k = -1;
        if (key == HUD_KEY_RIGHT) { for (int i = s_sel; i < n; i++) if (rows[i].header && rows[i].group > g) { k = i + 1; break; } }
        else { for (int i = s_sel; i >= 0; i--) if (rows[i].header && rows[i].group < g) { while (i > 0 && !rows[i - 1].header) i--; k = i + 1; break; } }
        if (k < 0) k = first_entry(rows, n, key == HUD_KEY_RIGHT ? 0 : n - 1, key == HUD_KEY_RIGHT ? 1 : -1);
        if (key == HUD_KEY_LEFT && k >= 0 && rows[k].header) k = first_entry(rows, n, k, 1);
        if (k >= 0 && k < n && !rows[k].header) s_sel = k;
    }
    else if (key == HUD_KEY_SELECT) {
        const char *why = refusal();
        if (why) { snprintf(s_msg, sizeof s_msg, "%s", why); return; }
        s_pending = rows[s_sel].dest;
        s_pending_age = 0; s_msg[0] = 0;
        hud_key(HUD_KEY_CLOSE);
    }
}

void ps_fly_hud_opened(void) { s_msg[0] = 0; }

void ps_fly_hud_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1)
{
    const uint32_t GREY = 0xFFA0A0B0, CYAN = 0xFF60D0FF, YEL = 0xFFFFE040, WHITE = 0xFFFFFFFF, DIM = 0xFF606878;
    char buf[64];
    if (ps_fly_opt_free()) snprintf(buf, sizeof buf, "FLY: FREE (OPTIONS)");
    else snprintf(buf, sizeof buf, "ALIS MP %u/%u   FLY COSTS %d MP", sms_read8(RAM_ALIS + 2), sms_read8(RAM_ALIS + 7), s_cost);
    hud_text(fb, w, h, x0 + 4, y0, buf, CYAN);
    const char *why = s_msg[0] ? s_msg : refusal();
    hud_text(fb, w, h, x0 + 4, y0 + 10, why ? why : "CONFIRM = FLY   LEFT/RIGHT = PLANET", why ? YEL : GREY);
    Row rows[MAX_ROWS]; int n = build_rows(rows);
    if (!n) { hud_text(fb, w, h, x0 + 4, y0 + 30, ps_fly_opt_all() ? "NO PLACES KNOWN" : "NO PLACES VISITED YET", GREY); return; }
    if (s_sel >= n || rows[s_sel].header) { int k = first_entry(rows, n, 0, 1); s_sel = k < 0 ? 0 : k; }
    int ty0 = y0 + 22, vis = (y1 - ty0) / 9;
    if (vis < 1) vis = 1;
    if (s_sel < s_top) s_top = s_sel;
    if (s_sel >= s_top + vis) s_top = s_sel - vis + 1;
    if (s_top > 0 && rows[s_top].header == false && rows[s_top - 1].header && s_sel > s_top) s_top--;   /* keep a header with its first entry */
    if (s_top + vis > n) s_top = n - vis > 0 ? n - vis : 0;
    for (int k = s_top, y = ty0; k < n && k < s_top + vis; k++, y += 9) {
        if (rows[k].header) { hud_text(fb, w, h, x0 + 4, y, k_group[rows[k].group], DIM); continue; }
        const Dest *d = &s_dest[rows[k].dest];
        bool sel = k == s_sel, vst = visited(d);
        if (sel) hud_fill(fb, w, h, x0 + 8, y - 1, x1 - 10, y + 8, 0xFF304060u, false);
        snprintf(buf, sizeof buf, "%s (%s)", d->name, d->type);
        hud_text(fb, w, h, x0 + 12, y, buf, sel ? (why ? WHITE : YEL) : vst ? WHITE : GREY);
    }
    if (s_top > 0) hud_text(fb, w, h, x1 - 8, ty0, "^", CYAN);
    if (s_top + vis < n) hud_text(fb, w, h, x1 - 8, ty0 + (vis - 1) * 9, "v", CYAN);
}
