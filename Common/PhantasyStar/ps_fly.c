/*
 * ps_fly.c - Phantasy Star (SMS) HUD "FLY" page: Alis's Fly spell to any town visited.
 *
 * The game's Fly (MapMagicList: Alis's 2nd map spell, MPCostData[$12] = 8 MP; handler LABEL_2140)
 * takes the party to the last church visited: the church sets $C317 (LABEL_2A85), Fly refuses
 * if Interaction_Type = 0, else sets $C2D8 = 8 and the player menu exit (LABEL_1C15) does
 * Game_mode = 8 (LoadMap), $C30E = 0 (vehicle), sound $BF and LABEL_787B with the 3-byte record
 * LABEL_1C7C[$C317] = (map, y, x) (9 church towns). LABEL_787B: $C308 = $C309 = map, camera
 * from the party cell (y: high nibble = screen row, low nibble = cell 0..11; x: cell 0..127),
 * then LABEL_7908 clears the object RAM ($C800-$C8FF) and $C26F-$C286 and copies $C810 to $C2D7.
 *
 * This page does the same for any town in this save's visited list (ps_automap.c), with the same
 * conditions and cost (Alis alive, Fly learned = map_magic_num >= 2, MP >= cost, not refused
 * here). Landing: the game's own Fly spot for the 9 church towns, else the town gate (the planet
 * entrance tables, LABEL_DF1 + 12 in bank 3: 6-byte records y, x, type, and for type 8 (LoadMap)
 * a LABEL_787B record), else the party's last position there (map = the only map of that
 * tileset). Planet groups come from the entrance tables; towns without a planet gate (Air
 * Castle) are "Special". The warp itself is done by the [insn_hooks] hook ps_fly_go at the end
 * of GameMode_Map (`ld a,(Ctrl_1_held) ; and $30 ; ret z`, Rev A $0B35) = where the game opens
 * the player menu (standing still, after the battle check): the same RAM writes as the game,
 * then `ret z` returns to the main loop, which runs GameMode_LoadMap.
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

#define RAM_GAME_MODE 0xC202
#define RAM_MOVE_CNT  0xC265
#define RAM_ALIS      0xC400                 /* +0 status (bit 0 alive), +2 MP, +7 max MP, +$F map spells */
#define MODE_MAP      0x09
#define MAX_DEST      32

typedef struct {
    int     place, group;                    /* group: 0 Palma, 1 Motavia, 2 Dezoris, 3 Special */
    int     map;                             /* -1 = no landing known */
    uint8_t y, x;                            /* LABEL_787B record */
    int     kind;                            /* 0 church (Fly), 1 gate, 2 last position */
} Dest;

static const uint8_t *g_rom; static size_t g_rom_n;
static long     s_fly_tbl = -1;              /* LABEL_1C7C */
static int      s_cost = 8;
static uint16_t s_it_addr;                   /* Interaction_Type */
static Dest     s_dest[MAX_DEST]; static int s_ndest;
static int      s_pending = -1, s_pending_age; /* index into s_dest */
static int      s_col, s_row;                /* page selection */
static char     s_msg[48];                   /* last refusal, shown on the page */
static long     s_abf = -1;                  /* LABEL_ABF: spaceship routes */

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

static Dest *dest_of(int place)
{
    for (int i = 0; i < s_ndest; i++) if (s_dest[i].place == place) return &s_dest[i];
    return NULL;
}
static int place_of_record(const uint8_t *r)  /* (map, y, x) -> town place, -1 */
{
    int ts = ps_worldmap_tileset_of(r[0]);
    if (ts < 4) return -1;
    return ps_worldmap_town_place(ts, (r[2] & 0x7F) >> 4, (r[1] >> 4) & 7);
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
    for (int p = 4; p < ps_worldmap_num_places() && s_ndest < MAX_DEST; p++)
        s_dest[s_ndest++] = (Dest){ p, 3, -1, 0, 0, 2 };
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
            if (s_dest[j].group != 3 && !strcmp(ps_worldmap_place_name(s_dest[i].place), ps_worldmap_place_name(s_dest[j].place)))
                s_dest[i].group = s_dest[j].group;
    if (getenv("PS_FLY_DEBUG"))
        for (int i = 0; i < s_ndest; i++)
            fprintf(stderr, "[fly] %-18s group %d map %2d y %02X x %02X kind %d\n", ps_worldmap_place_name(s_dest[i].place),
                    s_dest[i].group, s_dest[i].map, s_dest[i].y, s_dest[i].x, s_dest[i].kind);
}

/* the landing record of a destination: (map, y, x); false if none */
static bool landing(const Dest *d, uint8_t r[3])
{
    if (d->map >= 0) { r[0] = (uint8_t)d->map; r[1] = d->y; r[2] = d->x; return true; }
    int px, py;                              /* last position there; map = the tileset's only map */
    if (!ps_automap_map_known(d->place, &px, &py)) return false;
    int ts = ps_worldmap_place_tileset(d->place), map = -1;
    for (int k = 4; k < 32; k++) if (ps_worldmap_tileset_of(k) == ts) { if (map >= 0) return false; map = k; }
    if (map < 0 || px < 0 || py < 0) return false;
    r[0] = (uint8_t)map; r[2] = (uint8_t)((px >> 4) & 0x7F);
    r[1] = (uint8_t)(((py / 192) & 7) << 4 | ((py % 192) >> 4));
    return true;
}

static bool visited(const Dest *d) { int px, py; return ps_automap_map_known(d->place, &px, &py); }

/* why Fly is not possible now (NULL = possible) */
static const char *refusal(void)
{
    if (s_fly_tbl < 0) return "NOT AVAILABLE IN THIS VERSION";
    if (sms_read8(RAM_GAME_MODE) != MODE_MAP) return "ONLY WHILE WALKING ON A MAP";
    if (!(sms_read8(RAM_ALIS) & 1)) return "ALIS CANNOT CAST NOW";
    if (sms_read8(RAM_ALIS + 0x0F) < 2) return "ALIS HAS NOT LEARNED FLY";
    if (sms_read8(RAM_ALIS + 2) < s_cost) return "NOT ENOUGH MP";
    if (!sms_read8(s_it_addr)) return "CANNOT FLY FROM HERE";
    return NULL;
}

/* [insn_hooks] ps_fly_go @ GameMode_Map `ld a,(Ctrl_1_held)` -> resume at its `ret z` */
int ps_fly_go(void)
{
    if (s_pending < 0 || sms_read8(RAM_MOVE_CNT)) return 0;   /* mid-step: next frame */
    Dest *d = &s_dest[s_pending];
    s_pending = -1;
    uint8_t r[3];
    if (refusal() || !landing(d, r)) return 0;
    sms_write8(RAM_ALIS + 2, (uint8_t)(sms_read8(RAM_ALIS + 2) - s_cost));
    /* LABEL_787B */
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
    sms_write8(0xC004, 0xBF);                  /* the Fly departure sound */
    sms_write8(RAM_GAME_MODE, 0x08);           /* GameMode_LoadMap */
    z80f_set(&g_z80, Z80_FLAG_Z, true);        /* -> ret z */
    if (getenv("PS_FLY_DEBUG")) fprintf(stderr, "[fly] to %s: map %d y %02X x %02X\n", ps_worldmap_place_name(d->place), r[0], r[1], r[2]);
    return 1;
}

/* drop a pending flight that could not start (e.g. a battle came first) */
void ps_fly_frame(void)
{
    if (s_pending >= 0 && ++s_pending_age > 30) s_pending = -1;
}

/* ---- HUD page ---- */
static const char *const k_group[4] = { "Palma", "Motavia", "Dezoris", "Special" };
/* the visited destinations of one column, alphabetical, one per name: col 0 = Palma, 1 = the rest */
static int column(int col, int *out, int max, int *grp_start)
{
    int n = 0;
    for (int g = col ? 1 : 0; g <= (col ? 3 : 0); g++) {
        if (grp_start) grp_start[g] = n;
        int start = n;
        for (int i = 0; i < s_ndest && n < max; i++) {
            const Dest *d = &s_dest[i];
            uint8_t r[3];
            if (d->group != g || !visited(d) || !landing(d, r)) continue;
            const char *nm = ps_worldmap_place_name(d->place);
            bool dup = false;              /* same name twice (Dezoris): keep the lower kind (church spot) */
            for (int k = start; k < n; k++)
                if (!strcmp(ps_worldmap_place_name(s_dest[out[k]].place), nm)) {
                    dup = true;
                    if (d->kind < s_dest[out[k]].kind) out[k] = i;
                }
            if (dup) continue;
            int k = n++;                       /* insertion sort by name */
            while (k > start && strcmp(ps_worldmap_place_name(s_dest[out[k - 1]].place), nm) > 0) { out[k] = out[k - 1]; k--; }
            out[k] = i;
        }
    }
    return n;
}

void ps_fly_hud_key(int key)
{
    int c0[MAX_DEST], c1[MAX_DEST], n0 = column(0, c0, MAX_DEST, NULL), n1 = column(1, c1, MAX_DEST, NULL);
    int n = s_col ? n1 : n0;
    if (key == HUD_KEY_LEFT || key == HUD_KEY_RIGHT) {
        int nc = s_col ^ 1, nn = nc ? n1 : n0;
        if (nn) { s_col = nc; if (s_row >= nn) s_row = nn - 1; }
        return;
    }
    if (!n) return;
    if (key == HUD_KEY_UP) s_row = (s_row + n - 1) % n;
    if (key == HUD_KEY_DOWN) s_row = (s_row + 1) % n;
    if (key == HUD_KEY_SELECT) {
        const char *why = refusal();
        if (why) { snprintf(s_msg, sizeof s_msg, "%s", why); return; }
        s_pending = (s_col ? c1 : c0)[s_row < n ? s_row : 0];
        s_pending_age = 0; s_msg[0] = 0;
        hud_key(HUD_KEY_CLOSE);
    }
}

void ps_fly_hud_opened(void) { s_msg[0] = 0; }

void ps_fly_hud_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1)
{
    const uint32_t GREY = 0xFFA0A0B0, CYAN = 0xFF60D0FF, YEL = 0xFFFFE040, WHITE = 0xFFFFFFFF, DIM = 0xFF606878;
    char buf[64];
    snprintf(buf, sizeof buf, "ALIS MP %u/%u   FLY COSTS %d MP", sms_read8(RAM_ALIS + 2), sms_read8(RAM_ALIS + 7), s_cost);
    hud_text(fb, w, h, x0 + 4, y0, buf, CYAN);
    const char *why = s_msg[0] ? s_msg : refusal();
    hud_text(fb, w, h, x0 + 4, y0 + 10, why ? why : "CONFIRM = FLY THERE", why ? YEL : GREY);
    int c0[MAX_DEST], c1[MAX_DEST], g0[4], g1[4];
    int n0 = column(0, c0, MAX_DEST, g0), n1 = column(1, c1, MAX_DEST, g1);
    if (!n0 && !n1) { hud_text(fb, w, h, x0 + 4, y0 + 30, "NO TOWNS VISITED YET", GREY); return; }
    if (s_col == 0 && !n0) s_col = 1;
    if (s_col == 1 && !n1) s_col = 0;
    int nsel = s_col ? n1 : n0; if (s_row >= nsel) s_row = nsel ? nsel - 1 : 0;
    int cw = (x1 - x0 - 8) / 2, ty0 = y0 + 24;
    for (int col = 0; col < 2; col++) {
        const int *c = col ? c1 : c0, *gs = col ? g1 : g0; int n = col ? n1 : n0;
        int x = x0 + 4 + col * cw, y = ty0;
        for (int g = col ? 1 : 0; g <= (col ? 3 : 0); g++) {
            int end = g < 3 && col ? gs[g + 1] : n;
            if (gs[g] == end) continue;                      /* no visited town in this group */
            hud_text(fb, w, h, x, y, k_group[g], DIM); y += 10;
            for (int k = gs[g]; k < end; k++, y += 9) {
                bool sel = col == s_col && k == s_row;
                if (sel) hud_fill(fb, w, h, x + 4, y - 1, x + cw - 6, y + 8, 0xFF304060u, false);
                hud_text(fb, w, h, x + 8, y, ps_worldmap_place_name(s_dest[c[k]].place), sel ? (why ? WHITE : YEL) : WHITE);
            }
            y += 3;
        }
    }
    (void)y1;
}
