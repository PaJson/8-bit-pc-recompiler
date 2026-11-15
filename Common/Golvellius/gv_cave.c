/*
 * gv_cave.c - Golvellius dungeon maps (side panel + HUD MAP tab), drawn from the ROM's own dungeon data.
 *
 * Found in the ROM (CLAUDE.md "Golvellius caves / dungeons"):
 *   entering a dungeon ($1D11): $CAD7 = its id 0..8 (0 = the New Game start area); odd ids = top-down
 *              (mode $C802 = 8), even = side-view (mode 7; also the New Game start area, id 0). Modes 8 / 7
 *              are stored only by $1D96 / $1DD3. (The hero object type $C100 is not usable: it changes
 *              while the hero is hit.)
 *   layout   : asset $10D3[id] = $50 + id (bank 5), 2 bytes per step, $FFFF = end. Top-down: a step is a
 *              row (bottom = entrance first), bits 15..6 = 10 cells left to right; side-view: a step is a
 *              column (left first), bits 15..7 = 9 cells top to bottom. 1 = wall; the low bits are a
 *              per-step marker (not used here). Borders: $1A / $1B before / after the cells.
 *   metatiles: asset $C1 + id (= RAM $CC00), 4 tile numbers each, tile = 256 + n, palette 0.
 *   walls    : ($1774 top-down, $1115 side-view) a wall cell gets the code left 8 | below 4 | right 2 |
 *              above 1 of its wall neighbours (borders and the steps beyond the ends count as wall) if
 *              metatile[code] is not empty, else the solid wall $10 + cell index.
 *   floor    : asset $CA -> per id a list of steps of 10 / 9 tiles ($17EF / $10DC), step mod count; then
 *              ($0E71 top-down, $0EB6 side-view) the shadow of walls left / above / above-left (code
 *              4 | 2 | 1, table $0EFB, 4 tiles, 0 = keep; wall = metatile < $30 top-down, < $20 side-view).
 *              Every tile checked against the game's name table (0 differ, both kinds).
 *   palette  : asset $60 + id ($5B for 8). Tiles: VRAM $2000 while inside (kept per dungeon, saved with the map).
 *   position : k = ($CADA - start) / 2 (the pointer runs 2 steps ahead). Top-down: row r is drawn at
 *              name-table line 16 * ((11 - r) mod 14), the ring holds rows k-15..k-2, screen line s
 *              shows line (reg9 + s) mod 224; the hero's centre = sprite y - 16 / x (never on a wall
 *              while walking / pushed). Side-view: column c is in name-table band c mod 16 (ring
 *              k-17..k-2), screen x shows name-table x (x - reg8) mod 256, cells from name row 2 (the
 *              status bar above); the hero's centre = picture y - 32 / x (fitted over 1352 positions of
 *              random play: no wall hit for x -6..+4, y -28..-4 around it).
 * Explored steps (on screen at least once) are kept per dungeon in <save>.gvcave. Option DUNGEON MAP:
 * explored only (default) / the whole dungeon.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hud.h"
#include "sms_runtime.h"
#include "video/sms_vdp.h"
#include "safe_file.h"

extern uint8_t *glue_rom_image(size_t *size);
extern const char *glue_save_path(void);

#define NCAVE 9
#define MAXST 768                                   /* steps per dungeon (the longest has 639) */
#define MAXCC 12                                    /* cells per step incl. the two borders */
#define GAP 24                                      /* between wrapped segments (picture pixels) */

int gv_cave_reveal;                                 /* option DUNGEON MAP: 0 explored, 1 whole */

static const uint8_t *s_rom; static size_t s_rom_n;
static uint8_t  s_seen[NCAVE][MAXST / 8];
static uint8_t  s_snap[NCAVE][0x2000];              /* VRAM $2000-$3FFF while inside */
static bool     s_has_snap[NCAVE];
static bool     s_loaded, s_dirty;
static char     s_names[NCAVE][12];

/* the dungeon being shown */
static int      s_id = -1, s_n, s_side, s_cc, s_ni; /* id, steps, side-view?, cells per step (incl. borders), inner cells */
static uint16_t s_steps[MAXST];
static uint8_t  s_mt[MAXST][MAXCC];                 /* metatile per cell */
static uint8_t  s_tl[MAXST][MAXCC][4];              /* its 4 tiles as drawn (floor shadows applied) */
static uint32_t *s_pic;                             /* PW x PH */
static int      s_pw, s_ph;
static bool     s_drawn[MAXST];
static uint32_t s_gen = 1, s_drawn_reveal = 2;
/* live state */
static bool     s_in;                               /* inside a dungeon now */
static int      s_hx = -1, s_hy = -1;               /* the hero in picture coordinates */
static int      s_vx, s_vy, s_vw, s_vh;             /* the game's view in picture coordinates */

static int asset(int code)
{
    int bank = (code >> 4) & 15; size_t o = (size_t)bank * 0x4000 + (size_t)(code & 15) * 2;
    if (o + 1 >= s_rom_n) return -1;
    return bank * 0x4000 + ((s_rom[o] | s_rom[o + 1] << 8) - 0x8000);
}
static bool rom_ok(void)
{
    if (!s_rom) s_rom = glue_rom_image(&s_rom_n);
    return s_rom && s_rom_n >= 0x40000;
}
static int pal_code(int id) { return id == 8 ? 0x5B : 0x60 + id; }      /* $1D26: $68 -> $5B */
static bool cave_log(void) { static int on = -1; if (on < 0) on = getenv("GV_CAVE_LOG") != NULL; return on; }

static void names_init(void)
{
    static const char *const fb[NCAVE] = { "START AREA", "DESPA", "ROLICK", "BACHULAR", "FOSBUS", "WARLIC", "CRAWKY", "HAIDEE", "GOLVELLIUS" };
    for (int i = 0; i < NCAVE; i++) snprintf(s_names[i], sizeof s_names[i], "%s", fb[i]);
    /* the game's own list (bank 7, file $1D33C): length-prefixed DESPA .. HAIDEE = dungeons 1..7 */
    size_t o = 0x1D33C;
    for (int i = 1; i <= 7; i++) {
        if (o >= s_rom_n) return;
        int n = s_rom[o];
        if (n < 3 || n > 10 || o + 1 + n > s_rom_n) return;
        for (int k = 0; k < n; k++) if (s_rom[o + 1 + k] < 'A' || s_rom[o + 1 + k] > 'Z') return;
        memcpy(s_names[i], s_rom + o + 1, (size_t)n); s_names[i][n] = 0;
        o += 1 + (size_t)n;
    }
}

/* ---- file: "GVC2", per dungeon: seen bits (MAXST / 8), has_snap, [snapshot]; then 182 bytes: the screens
   whose cave the hero has been in (gv_info.c: CAVE INFO = entered caves). "GVC1" = without them. ---- */
static uint8_t s_entered[182];
bool gv_cave_entered(int scr) { return scr >= 0 && scr < 182 && s_entered[scr]; }
const char *gv_boss_name(int id)                    /* the game's names, upper case */
{
    static bool init; if (!init && rom_ok()) { names_init(); init = true; }
    return id >= 0 && id < NCAVE ? s_names[id] : "";
}
static void cave_path(char *out, size_t n)
{
    const char *sp = glue_save_path(); out[0] = 0;
    if (!sp || !sp[0]) return;
    snprintf(out, n, "%s", sp);
    size_t l = strlen(out);
    if (l > 4 && !strcmp(out + l - 4, ".sav")) out[l - 4] = 0;
    strncat(out, ".gvcave", n - strlen(out) - 1);
}
void gv_cave_save(void)
{
    if (!s_dirty || getenv("SMS_NO_SAVE")) return;
    char p[1100]; cave_path(p, sizeof p); if (!p[0]) return;
    FILE *f = safe_fopen_w(p, "wb"); if (!f) return;
    fwrite("GVC2", 1, 4, f);
    for (int i = 0; i < NCAVE; i++) {
        fwrite(s_seen[i], 1, sizeof s_seen[i], f);
        uint8_t h = s_has_snap[i]; fwrite(&h, 1, 1, f);
        if (h) fwrite(s_snap[i], 1, sizeof s_snap[i], f);
    }
    fwrite(s_entered, 1, sizeof s_entered, f);
    if (safe_fclose(f, p)) s_dirty = false;
}
static void cave_load_file(void)
{
    if (s_loaded) return;
    s_loaded = true;
    if (rom_ok()) names_init();
    char p[1100]; cave_path(p, sizeof p);
    FILE *f = p[0] ? fopen(p, "rb") : NULL; if (!f) return;
    char mg[4];
    bool v2 = false, ok = true;
    if (fread(mg, 1, 4, f) == 4 && (!memcmp(mg, "GVC1", 4) || (v2 = !memcmp(mg, "GVC2", 4)))) {
        for (int i = 0; i < NCAVE && ok; i++) {
            uint8_t h = 0;
            if (fread(s_seen[i], 1, sizeof s_seen[i], f) != sizeof s_seen[i] || fread(&h, 1, 1, f) != 1) { ok = false; break; }
            if (h) { if (fread(s_snap[i], 1, sizeof s_snap[i], f) != sizeof s_snap[i]) { ok = false; break; } s_has_snap[i] = true; }
        }
        if (ok && v2 && fread(s_entered, 1, sizeof s_entered, f) != sizeof s_entered) memset(s_entered, 0, sizeof s_entered);
    }
    fclose(f);
}

/* ---- one dungeon: layout -> metatiles -> tiles -> picture ---- */
static uint16_t step_bits(int s)                    /* border bits added (before = bit 15, after = 14 - ni) */
{
    uint16_t v = s >= 0 && s < s_n ? s_steps[s] : 0xFFFF;
    return (uint16_t)(0x8000 | ((v >> 1) & 0x7FFF) | (1 << (14 - s_ni)));
}
static int wall_at(uint16_t w, int j) { return j < 0 || j >= s_ni ? 1 : (w >> (14 - j)) & 1; }   /* inner cell j */
static uint8_t mt_or(int s, int c, uint8_t outside) { return s >= 0 && s < s_n && c >= 0 && c < s_cc ? s_mt[s][c] : outside; }
static bool select_cave(int id)
{
    if (id == s_id) return s_n > 0;
    s_id = id; s_n = 0; s_drawn_reveal = 2;
    memset(s_drawn, 0, sizeof s_drawn);
    if (!rom_ok() || id < 0 || id >= NCAVE) return false;
    s_side = !(id & 1); s_ni = s_side ? 9 : 10; s_cc = s_ni + 2;
    int o = asset(0x50 + id), mt = asset(0xC1 + id), pl = asset(0xCA);
    if (o < 0 || mt < 0 || pl < 0) return false;
    while ((size_t)o + 1 < s_rom_n && s_n < MAXST) {
        uint16_t v = (uint16_t)(s_rom[o] << 8 | s_rom[o + 1]); o += 2;
        if (v == 0xFFFF) break;
        s_steps[s_n++] = v;
    }
    /* floor pattern: list of steps of ni tiles ending with $FF (pointer in bank 12) */
    uint8_t pat[256]; int np = 0;
    {   int q = pl + id * 2; int ptr = s_rom[q] | s_rom[q + 1] << 8; size_t po = (size_t)12 * 0x4000 + (size_t)(ptr - 0x8000);
        while (po < s_rom_n && s_rom[po] != 0xFF && np < 250) pat[np++] = s_rom[po++]; }
    int npg = np / s_ni; if (npg < 1) { npg = 1; memset(pat, 0x30, (size_t)s_ni); }
    for (int s = 0; s < s_n; s++) {
        uint16_t cur = step_bits(s), prev = step_bits(s - 1), next = step_bits(s + 1);
        s_mt[s][0] = 0x1A; s_mt[s][s_cc - 1] = 0x1B;
        for (int j = 0; j < s_ni; j++) {
            uint8_t m;
            if (!wall_at(cur, j)) m = pat[(s % npg) * s_ni + j];
            else {
                int l, b, r, a;                                     /* left, below, right, above */
                if (!s_side) { l = wall_at(cur, j - 1); b = wall_at(prev, j); r = wall_at(cur, j + 1); a = wall_at(next, j); }
                else         { l = wall_at(prev, j); b = wall_at(cur, j + 1); r = wall_at(next, j); a = wall_at(cur, j - 1); }
                int code = l << 3 | b << 2 | r << 1 | a;
                m = s_rom[mt + 4 * code] ? (uint8_t)code : (uint8_t)(0x10 + j);
            }
            s_mt[s][j + 1] = m;
        }
    }
    int thr = s_side ? 0x20 : 0x30;
    for (int s = 0; s < s_n; s++)
        for (int c = 0; c < s_cc; c++) {
            int m = s_mt[s][c];
            for (int q = 0; q < 4; q++) s_tl[s][c][q] = s_rom[mt + 4 * m + q];
            if (m < 0x30) continue;
            int l, a, al;
            if (!s_side) { l = mt_or(s, c - 1, 0) < thr; a = mt_or(s + 1, c, 0) < thr; al = mt_or(s + 1, c - 1, 0) < thr; }
            else         { l = mt_or(s - 1, c, 0xFF) < thr; a = mt_or(s, c - 1, 0) < thr; al = mt_or(s - 1, c - 1, 0xFF) < thr; }
            int code = l << 2 | a << 1 | al;
            for (int q = 0; q < 4; q++) if (s_rom[0x0EFB + code * 4 + q]) s_tl[s][c][q] = s_rom[0x0EFB + code * 4 + q];
        }
    s_pw = s_side ? s_n * 16 : s_cc * 16;
    s_ph = s_side ? s_cc * 16 : s_n * 16;
    free(s_pic); s_pic = malloc((size_t)s_pw * (size_t)s_ph * 4);
    if (!s_pic) { s_n = 0; return false; }
    for (size_t i = 0; i < (size_t)s_pw * (size_t)s_ph; i++) s_pic[i] = 0xFF000000u;
    s_gen++;
    if (cave_log()) fprintf(stderr, "[cave] dungeon %d (%s, %s): %d steps\n", id, s_names[id], s_side ? "side-view" : "top-down", s_n);
    return s_n > 0;
}
static bool seen_step(int id, int s) { return s >= 0 && s < MAXST && (s_seen[id][s >> 3] >> (s & 7) & 1); }
static void mark_seen(int id, int s) { if (s >= 0 && s < s_n && !seen_step(id, s)) { s_seen[id][s >> 3] |= (uint8_t)(1 << (s & 7)); s_dirty = true; } }
static uint32_t argb(uint8_t c)
{
    return 0xFF000000u | (uint32_t)((c & 3) * 85) << 16 | (uint32_t)(((c >> 2) & 3) * 85) << 8 | (uint32_t)(((c >> 4) & 3) * 85);
}
static void cell_origin(int s, int c, int *x, int *y)
{
    if (!s_side) { *x = c * 16; *y = (s_n - 1 - s) * 16; }
    else         { *x = s * 16; *y = c * 16; }
}
/* draw the steps that should show and are not drawn yet (all again when the option changes) */
static void update_pic(void)
{
    if (s_id < 0 || !s_n || !s_pic) return;
    if (s_drawn_reveal != (uint32_t)gv_cave_reveal) {
        memset(s_drawn, 0, sizeof s_drawn);
        for (size_t i = 0; i < (size_t)s_pw * (size_t)s_ph; i++) s_pic[i] = 0xFF000000u;
        s_drawn_reveal = (uint32_t)gv_cave_reveal; s_gen++;
    }
    if (!s_has_snap[s_id]) return;
    int pal = asset(pal_code(s_id)); if (pal < 0) return;
    uint32_t col[16]; for (int i = 0; i < 16; i++) col[i] = argb(s_rom[pal + i]);
    const uint8_t *tiles = s_snap[s_id];
    bool changed = false;
    for (int s = 0; s < s_n; s++) {
        if (s_drawn[s] || !(gv_cave_reveal || seen_step(s_id, s))) continue;
        s_drawn[s] = true; changed = true;
        for (int c = 0; c < s_cc; c++) {
            int ox, oy; cell_origin(s, c, &ox, &oy);
            for (int q = 0; q < 4; q++) {
                int t = s_tl[s][c][q], px = ox + (q & 1) * 8, py = oy + (q >> 1) * 8;
                for (int y = 0; y < 8; y++) {
                    const uint8_t *row = tiles + t * 32 + y * 4;
                    uint32_t *dst = s_pic + (size_t)(py + y) * s_pw + px;
                    for (int x = 0; x < 8; x++) {
                        int b = 7 - x;
                        dst[x] = col[((row[0] >> b) & 1) | ((row[1] >> b) & 1) << 1 | ((row[2] >> b) & 1) << 2 | ((row[3] >> b) & 1) << 3];
                    }
                }
            }
        }
    }
    if (changed) s_gen++;
}

/* ---- every frame: inside a dungeon? tiles, explored steps, the hero, the game's view ---- */
static int td_row(int line, int k)                  /* top-down: cave row at name-table line */
{
    int r = 11 - ((line % 224 + 224) % 224) / 16;
    while (r > k - 2) r -= 14;
    while (r < k - 15) r += 14;
    return r;
}
static int sv_col(int nx, int k)                    /* side-view: cave column at name-table x */
{
    int c = ((nx & 255) >> 4);
    while (c > k - 2) c -= 16;
    while (c < k - 17) c += 16;
    return c;
}
static int sv_px(int x, int k) { int nx = (x - g_vdp.reg[8]) & 255; return sv_col(nx, k) * 16 + (nx & 15); }
void gv_cave_frame(void)
{
    cave_load_file();
    s_in = false;
    if (!rom_ok()) return;
    {   /* the screen whose cave the hero is in: a cave room (mode $11) or a dungeon */
        int m = sms_read8(0xC802), scr = sms_read8(0xC80D);
        if (scr < 182 && !s_entered[scr] && (m == 0x11 || m == 8 || m == 0x0A || (m == 7 && sms_read8(0xCAD7)))) {
            s_entered[scr] = 1; s_dirty = true;
            if (cave_log()) fprintf(stderr, "[cave] entered the cave of screen %c%d (mode %02X)\n", 'A' + scr % 13, scr / 13 + 1, m);
        }
    }
    /* modes 8 / 7 are set only by $1D96 / $1DD3 (entry); mode $0A = the boss fight: set when the row /
       column after the next one is the $FFFF end ($18A0 top-down, $145D side-view) */
    int mode = sms_read8(0xC802), id = sms_read8(0xCAD7);
    bool boss = mode == 0x0A;
    bool td = (mode == 8 || boss) && (id & 1), sv = (mode == 7 || boss) && !(id & 1);
    if (!(td || sv) || id >= NCAVE) return;
    if (!select_cave(id)) return;
    s_in = true;
    if (!(g_vdp.reg[1] & 0x40)) return;                       /* display off: loading */
    int pal = asset(pal_code(id));
    bool steady = pal >= 0 && !memcmp(s_rom + pal, g_vdp.cram, 16);   /* the dungeon's palette, not mid-fade */
    if (steady && !s_has_snap[id]) { memcpy(s_snap[id], g_vdp.vram + 0x2000, 0x2000); s_has_snap[id] = true; s_dirty = true; }
    int start = asset(0x50 + id), cada = sms_read8(0xCADA) | sms_read8(0xCADB) << 8;
    int k = (cada - (0x8000 + (start & 0x3FFF))) / 2;
    if (k < 2 || k > s_n + 2) { s_hx = s_hy = -1; return; }
    int y = sms_read8(0xC101), x = sms_read8(0xC102);
    if (boss && steady)                     /* the end is reached: the rest (the boss room, partly never shown) */
        for (int s = k - (s_side ? 17 : 15); s < s_n; s++) mark_seen(id, s);
    static int trace = -1; if (trace < 0) trace = getenv("GV_CAVE_TRACE") != NULL;      /* test: positions */
    if (trace) fprintf(stderr, "[cavetrace] id %d k %d x %d y %d r8 %d r9 %d c100 %02X\n", id, k, x, y, g_vdp.reg[8], g_vdp.reg[9], sms_read8(0xC100));
    if (!s_side) {
        int v = g_vdp.reg[9];
        if (steady) for (int l = 0; l < 192; l += 8) mark_seen(id, td_row(v + l, k));
        int n = (v + y - 16 + 224) % 224, r = td_row(n, k);
        s_hy = (s_n - 1 - r) * 16 + n % 16; s_hx = x;
        s_vx = 0; s_vw = s_pw; s_vy = (s_n - 1 - td_row(v, k)) * 16 + v % 16; s_vh = 192;
    } else {
        if (steady) for (int l = 8; l < 256; l += 8) mark_seen(id, sv_px(l, k) / 16);
        s_hx = sv_px(x, k); s_hy = y - 32; if (s_hy < 0) s_hy = 0;
        s_vx = sv_px(8, k) - 8; s_vw = 256; s_vy = 0; s_vh = s_ph;
    }
    if (cave_log()) {
        int c = s_side ? s_hx / 16 : s_hx / 16, st = s_side ? s_hx / 16 : s_n - 1 - s_hy / 16, cell = s_side ? s_hy / 16 : c;
        static int last = -99; if (st != last) { fprintf(stderr, "[cave] dungeon %d step %d/%d (k %d)\n", id, st, s_n, k); last = st; }
        if (st >= 0 && st < s_n && cell >= 0 && cell < s_cc && s_mt[st][cell] < (s_side ? 0x20 : 0x30))
            fprintf(stderr, "[cave] hero on a wall: step %d cell %d (y %d x %d)\n", st, cell, y, x);
    }
}
bool gv_cave_inside(void) { return s_in; }

/* ---- the whole dungeon cut into segments: top-down = columns side by side (entrance bottom left, each
   continues at the bottom of the next), side-view = strips stacked (entrance top left) ---- */
typedef struct { uint32_t *buf; int w, h, nseg, len; uint32_t src, gen; } Wrap;
static float wrap_fit(int bw, int bh, int *nseg, int *len)
{
    float best = 0; *nseg = 1; *len = s_side ? s_pw : s_ph;
    for (int n = 1; n <= 24; n++) {
        int L = ((s_side ? s_pw : s_ph) + n - 1) / n; L = (L + 15) & ~15;
        int W = s_side ? L : n * s_pw + (n - 1) * GAP, H = s_side ? n * s_ph + (n - 1) * GAP : L;
        float sc = (float)bw / W < (float)bh / H ? (float)bw / W : (float)bh / H;
        if (sc > best) { best = sc; *nseg = n; *len = L; }
    }
    return best;
}
static void wrap_build(Wrap *w, int nseg, int len)
{
    int W = s_side ? len : nseg * s_pw + (nseg - 1) * GAP, H = s_side ? nseg * s_ph + (nseg - 1) * GAP : len;
    if (w->buf && w->w == W && w->h == H && w->nseg == nseg && w->len == len && w->src == s_gen) return;
    if (w->w * w->h < W * H || !w->buf) { free(w->buf); w->buf = malloc((size_t)W * H * 4); }
    if (!w->buf) { w->w = w->h = 0; return; }
    w->w = W; w->h = H; w->nseg = nseg; w->len = len; w->src = s_gen; w->gen++;
    for (int i = 0; i < W * H; i++) w->buf[i] = 0xFF0A1024u;
    for (int i = 0; i < nseg; i++) {
        if (!s_side) {                                    /* picture rows [ph-(i+1)len, ph-i*len) -> x = i*(pw+GAP) */
            for (int y = 0; y < len; y++) {
                int py = s_ph - (i + 1) * len + y; if (py < 0 || py >= s_ph) continue;
                memcpy(w->buf + (size_t)y * W + i * (s_pw + GAP), s_pic + (size_t)py * s_pw, (size_t)s_pw * 4);
            }
        } else {                                          /* picture columns [i*len, (i+1)*len) -> y = i*(ph+GAP) */
            int x0 = i * len, n = s_pw - x0 < len ? s_pw - x0 : len; if (n <= 0) continue;
            for (int y = 0; y < s_ph; y++)
                memcpy(w->buf + (size_t)(i * (s_ph + GAP) + y) * W, s_pic + (size_t)y * s_pw + x0, (size_t)n * 4);
        }
    }
}
static void wrap_xy(const Wrap *w, int px, int py, float *wx, float *wy)    /* picture -> wrap coordinates */
{
    if (!s_side) { int i = (s_ph - 1 - py) / w->len; *wx = (float)(i * (s_pw + GAP) + px); *wy = (float)(py - (s_ph - (i + 1) * w->len)); }
    else         { int i = px / w->len; *wx = (float)(px - i * w->len); *wy = (float)(i * (s_ph + GAP) + py); }
}
static void wrap_draw(Wrap *w, int space, uint32_t *fb, int fw, int fh, float bx, float by, float bw, float bh)
{
    int nseg, len; float sc = wrap_fit((int)bw, (int)bh, &nseg, &len);
    wrap_build(w, nseg, len);
    if (!w->buf || sc <= 0) return;
    float dw = w->w * sc, dh = w->h * sc, dx = bx + (bw - dw) / 2, dy = by + (bh - dh) / 2;
    if (hud_hires()) hud_image(space, dx, dy, dw, dh, w->buf, w->w, w->h, 0, 0, w->w, w->h, false, w->gen);
    else
        for (int y = 0; y < (int)dh; y++)
            for (int x = 0; x < (int)dw; x++) {
                int X = (int)dx + x, Y = (int)dy + y;
                if (X >= 0 && Y >= 0 && X < fw && Y < fh) fb[(size_t)Y * fw + X] = w->buf[(size_t)(int)(y / sc) * w->w + (int)(x / sc)];
            }
    if (s_hx < 0 || !hud_hires()) return;
    float vx, vy; wrap_xy(w, s_side ? s_vx < 0 ? 0 : s_vx : s_vx, s_side ? s_vy : s_vy + s_vh - 1, &vx, &vy);   /* the game's view */
    float fx = dx + vx * sc, fy = dy + vy * sc, fwid = (s_side ? s_vw : s_vw) * sc, fhei = s_vh * sc;
    if (!s_side) fy -= (s_vh - 1) * sc;
    hud_rect(space, fx, fy, fwid, 1, 0xC060D0FFu); hud_rect(space, fx, fy + fhei - 1, fwid, 1, 0xC060D0FFu);
    hud_rect(space, fx, fy, 1, fhei, 0xC060D0FFu); hud_rect(space, fx + fwid - 1, fy, 1, fhei, 0xC060D0FFu);
    static unsigned blink; blink++;
    if ((blink / 18) % 3) {
        float hx, hy; wrap_xy(w, s_hx, s_hy, &hx, &hy);
        float r = 16 * sc; if (r < 2.f) r = 2.f; if (r > 4.f) r = 4.f;
        hud_marker(space, dx + hx * sc, dy + hy * sc, r, 1.f, 0xFFFFE040u, 0xFF000000u);
    }
}

/* ---- side panel (the whole panel while inside) ---- */
static int explored(void)
{
    int e = 0; for (int s = 0; s < s_n; s++) e += seen_step(s_id, s);
    return s_n ? e * 100 / s_n : 0;
}
static void box(uint32_t *b, int w, int h, int x0, int y0, int x1, int y1)
{
    hud_fill(b, w, h, x0, y0, x1, y1, 0xFF3C6EA0u, false);
    hud_fill(b, w, h, x0 + 2, y0 + 2, x1 - 2, y1 - 2, 0xFF141E3Cu, false);
}
static void soft_copy(uint32_t *b, int w, int h, int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh)
{
    for (int y = 0; y < dh; y++) {
        int Y = dy + y, py = sy + y * sh / dh; if (Y < 0 || Y >= h || py < 0 || py >= s_ph) continue;
        for (int x = 0; x < dw; x++) { int X = dx + x, px = sx + x * sw / dw; if (X >= 0 && X < w && px >= 0 && px < s_pw) b[(size_t)Y * w + X] = s_pic[(size_t)py * s_pw + px]; }
    }
}
static Wrap s_wpanel, s_whud;
bool gv_cave_panel(uint32_t *b, int w, int h)
{
    if (!s_in || s_id < 0 || !s_n) return false;
    update_pic();
    for (int i = 0; i < w * h; i++) b[i] = 0xFF0A1024u;
    char t[48];
    box(b, w, h, 8, 8, w - 8, 40);
    hud_text_scaled(b, w, h, 16, 16, s_names[s_id], 0xFF60D0FFu, 2);
    snprintf(t, sizeof t, "%d%%", explored());
    hud_text(b, w, h, w - 16 - 6 * (int)strlen(t), 20, t, 0xFF8C96AAu);
    int y0 = 48, y1 = h - 8;
    box(b, w, h, 8, y0, w - 8, y1);
    if (!s_has_snap[s_id]) { hud_text(b, w, h, w / 2 - 21, (y0 + y1) / 2, "LOADING", 0xFF8C96AAu); return true; }
    if (s_side) { wrap_draw(&s_wpanel, HUD_SPACE_PANEL, b, w, h, 12.f, (float)y0 + 4, (float)(w - 24), (float)(y1 - y0 - 8)); return true; }
    /* top-down: a 1:1 window around the hero + the whole dungeon as a strip beside it */
    int mh = y1 - y0 - 8, mx = 12, mw = s_pw;
    int cy = s_hy >= 0 ? s_hy : s_ph - 1;
    int sy = cy - mh / 2; if (sy > s_ph - mh) sy = s_ph - mh; if (sy < 0) sy = 0;
    int vh = mh < s_ph ? mh : s_ph;
    if (hud_hires()) hud_image(HUD_SPACE_PANEL, (float)mx, (float)y0 + 4, (float)mw, (float)vh, s_pic, s_pw, s_ph, 0, sy, s_pw, vh, false, s_gen);
    else soft_copy(b, w, h, mx, y0 + 4, mw, vh, 0, sy, s_pw, vh);
    int ox0 = mx + mw + 6, ow_max = w - 12 - ox0;
    float sc = (float)ow_max / s_pw; if (s_ph * sc > mh) sc = (float)mh / s_ph;
    float ow = s_pw * sc, oh = s_ph * sc, ox = ox0 + (ow_max - ow) / 2, oy = y0 + 4 + (mh - oh);
    if (hud_hires()) hud_image(HUD_SPACE_PANEL, ox, oy, ow, oh, s_pic, s_pw, s_ph, 0, 0, s_pw, s_ph, false, s_gen);
    else soft_copy(b, w, h, (int)ox, (int)oy, (int)ow < 1 ? 1 : (int)ow, (int)oh < 1 ? 1 : (int)oh, 0, 0, s_pw, s_ph);
    if (hud_hires()) {
        float fy = oy + sy * sc, fh = vh * sc;                /* the window on the strip */
        hud_rect(HUD_SPACE_PANEL, ox - 1, fy, ow + 2, 1, 0xFF60D0FFu); hud_rect(HUD_SPACE_PANEL, ox - 1, fy + fh - 1, ow + 2, 1, 0xFF60D0FFu);
        hud_rect(HUD_SPACE_PANEL, ox - 1, fy, 1, fh, 0xFF60D0FFu); hud_rect(HUD_SPACE_PANEL, ox + ow, fy, 1, fh, 0xFF60D0FFu);
        static unsigned blink; blink++;
        if (s_hy >= 0 && (blink / 20) % 3) {
            hud_marker(HUD_SPACE_PANEL, (float)mx + s_hx, (float)y0 + 4 + (s_hy - sy), 4.f, 1.f, 0xFFFFE040u, 0xFF000000u);
            hud_marker(HUD_SPACE_PANEL, ox + s_hx * sc, oy + s_hy * sc, 2.f, 1.f, 0xFFFFE040u, 0xFF000000u);
        }
    }
    return true;
}

/* ---- HUD MAP tab inside a dungeon ---- */
bool gv_cave_hud(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1)
{
    if (!s_in || s_id < 0 || !s_n) return false;
    update_pic();
    char t[64];
    snprintf(t, sizeof t, "DUNGEON %d: %s", s_id, s_names[s_id]);
    if (s_id == 0) snprintf(t, sizeof t, "%s", s_names[0]);
    hud_text(fb, w, h, x0 + 4, y0, t, 0xFF60D0FFu);
    snprintf(t, sizeof t, "%d%% EXPLORED", explored());
    hud_text(fb, w, h, x1 - 4 - 6 * (int)strlen(t), y0, t, 0xFFA0A0B0u);
    hud_text(fb, w, h, x0 + 4, y1 - 9, gv_cave_reveal ? "WHOLE DUNGEON (OPTION DUNGEON MAP)" : "EXPLORED PART (OPTION DUNGEON MAP)", 0xFFA0A0B0u);
    int by = y0 + 11, bh = y1 - 11 - by;
    if (!s_has_snap[s_id]) { hud_text(fb, w, h, (x0 + x1) / 2 - 21, by + bh / 2, "LOADING", 0xFFA0A0B0u); return true; }
    wrap_draw(&s_whud, HUD_SPACE_GAME, fb, w, h, (float)x0 + 4, (float)by, (float)(x1 - x0 - 8), (float)bh);
    return true;
}

/* test: GV_CAVE_PPM=<file> writes the dungeon picture at frame GV_CAVE_FRAME and compares the tiles
   with the game's name table (the rows / columns of the ring) */
void gv_cave_test(uint64_t frame)
{
    const char *p = getenv("GV_CAVE_PPM"), *f = getenv("GV_CAVE_FRAME");
    if (!p || !f || frame != (uint64_t)atoll(f) || s_id < 0 || !s_pic) return;
    update_pic();
    if (s_in) {
        int start = asset(0x50 + s_id), cada = sms_read8(0xCADA) | sms_read8(0xCADB) << 8;
        int k = (cada - (0x8000 + (start & 0x3FFF))) / 2, bad = 0, n = 0;
        for (int s = k - (s_side ? 17 : 15); s <= k - 2; s++) {
            if (s < 0 || s >= s_n) continue;
            for (int c = 0; c < s_cc; c++)
                for (int q = 0; q < 4; q++) {
                    int nrow, ncol;
                    if (!s_side) { nrow = 2 * (((11 - s) % 14 + 14) % 14) + (q >> 1); ncol = c * 2 + (q & 1); }
                    else         { nrow = 2 + c * 2 + (q >> 1); ncol = (s % 16) * 2 + (q & 1); }
                    int a = 0x3800 + (nrow % 28) * 64 + ncol * 2;
                    int e = (g_vdp.vram[a] | g_vdp.vram[a + 1] << 8) & 0x1FF;
                    n++;
                    if (e != (0x100 | s_tl[s][c][q])) { if (bad < 4) fprintf(stderr, "[cave] step %d cell %d tile %d: game %03X mine %03X\n", s, c, q, e, 0x100 | s_tl[s][c][q]); bad++; }
                }
        }
        fprintf(stderr, "[cave] name table check: %d of %d tiles differ\n", bad, n);
    }
    FILE *o = fopen(p, "wb"); if (!o) return;
    fprintf(o, "P6\n%d %d\n255\n", s_pw, s_ph);
    for (size_t i = 0; i < (size_t)s_pw * (size_t)s_ph; i++) { uint8_t c[3] = { (uint8_t)(s_pic[i] >> 16), (uint8_t)(s_pic[i] >> 8), (uint8_t)s_pic[i] }; fwrite(c, 1, 3, o); }
    fclose(o);
    fprintf(stderr, "[cave] dungeon %d picture %dx%d hero %d,%d view %d,%d explored %d%% -> %s\n", s_id, s_pw, s_ph, s_hx, s_hy, s_vx, s_vy, explored(), p);
}
