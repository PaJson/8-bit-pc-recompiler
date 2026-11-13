/*
 * ps_qol.c - Phantasy Star (SMS) quality-of-life options, SEGA-AGES style.
 *
 * Everything is OFF by default (the game then runs bit-identically to the ROM). Options come from
 * "ps_qol.ini" in the working directory (or the file named by $PS_QOL_INI) and can be changed
 * while playing in the settings HUD (F1 / gamepad Back; runner/hud.c). Closing the HUD writes the
 * file back.
 *
 *     exp_multiplier     = 1       ; battle EXP reward x N (1..100)
 *     meseta_multiplier  = 1       ; battle Meseta reward x N (1..100)
 *     encounter_percent  = 100     ; random-encounter chance, 100 = original, 50 = half, 0 = none
 *     automap            = 1       ; 0 = no dungeon auto-map window
 *
 * No game logic is rewritten: rewards are scaled in RAM right after the game computes them, the
 * overworld encounter-rate table is scaled in the in-memory ROM copy (from a saved original, so it
 * can change at any time), and the dungeon encounter chance ($C2E3) is scaled while the dungeon
 * engine is running.
 *
 * RAM (ps1disasm-master): $C2D0 EXP reward (16-bit, set at battle setup), $C2DD Meseta reward
 * (16-bit, same setup; $C2DF is the item in the treasure chest - NOT part of the reward), $C2E3 dungeon encounter chance (x/256 per step), $C202 Game_mode ($0B
 * dungeon), party stats $C400+$10*n (status, HP, MP, EXP word, level, max HP, max MP...), money
 * $C4E0. Overworld chance: ROM file offset $C000 (bank 3, CPU $8000) = 3 maps x 64 areas x (rate,
 * zone) - the same offset in every PS ROM we have (Rev A, USA, Brazil, Japan, Japan Rev 1, Korea,
 * both AGES, the 2.5 retranslations); a 16-byte signature is checked before patching.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hud.h"

extern uint8_t  sms_read8(uint16_t addr);
extern void     sms_write8(uint16_t addr, uint8_t val);
extern uint8_t *glue_rom_image(size_t *size);
extern void     ps_automap_frame(uint64_t frame);
extern void     ps_automap_enable(bool on);
extern void     ps_automap_init(void);
extern void     ps_automap_hud_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1);
extern int      ps_automap_in_dungeon(void);
extern void     ps_automap_set_view(int reveal_all, int show_secrets);
extern void     ps_worldmap_init(void);
extern void     ps_worldmap_frame(void);
extern bool     ps_worldmap_valid(void);
extern void     ps_worldmap_hud_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1);

#define RAM_GAME_MODE 0xC202
#define RAM_EXP       0xC2D0
#define RAM_MESETA    0xC2DD
#define RAM_DNG_RATE  0xC2E3
#define RAM_STATS     0xC400
#define RAM_MONEY     0xC4E0
#define MODE_DUNGEON  0x0B

#define OVERWORLD_RATE_TABLE_ROM_OFFSET (3u * 0x4000u)   /* bank 3, CPU $8000 */
#define OVERWORLD_RATE_TABLE_SIZE       0x180u           /* 3 maps * 64 * 2 bytes */
static const uint8_t k_rate_sig[16] = {0x10,0x0C,0x10,0x0C,0x10,0x0C,0x10,0x0B,0x10,0x0B,0x10,0x05,0x10,0x05,0x10,0x0F};

/* ---- option values (indices into the HUD choice lists) ---- */
#define N_MULT 100                              /* multipliers x1 .. x100 (index + 1) */
static char        s_mult_buf[N_MULT][16];
static const char *k_mult_names[N_MULT];        /* filled by init_names() */
static const char *const k_enc_names[]  = { "NONE", "25%", "50%", "75%", "100% (ORIGINAL)", "150%", "200%" };
static const int         k_enc_vals[]   = { 0, 25, 50, 75, 100, 150, 200 };
static const char *const k_onoff[]      = { "OFF", "ON" };
#define N_ENC  7

static int s_exp_i = 0, s_mes_i = 0, s_enc_i = 4, s_map_i = 0;   /* map side window off: the map is a HUD page */
static int s_walk_i = 0;                       /* walk speed: 0 = x1 (original), 1 = x2, 2 = x4 */
static int s_reveal_i = 0, s_secrets_i = 1;    /* dungeon map: explored / whole floor; secrets hidden / shown */
static const char *s_ini_path = "ps_qol.ini";

static unsigned exp_mult(void)  { return (unsigned)s_exp_i + 1; }
static unsigned mes_mult(void)  { return (unsigned)s_mes_i + 1; }
static unsigned walk_mult(void) { return 1u << s_walk_i; }      /* 1, 2, 4 */
static int    enc_pct(void)  { return k_enc_vals[s_enc_i]; }

static int nearest_mult(double v){ int m = (int)(v + 0.5); return (m < 1 ? 1 : m > N_MULT ? N_MULT : m) - 1; }
static void init_names(void)
{
    for (int i = 0; i < N_MULT; i++) {
        snprintf(s_mult_buf[i], sizeof s_mult_buf[i], i ? "X%d" : "X1 (ORIGINAL)", i + 1);
        k_mult_names[i] = s_mult_buf[i];
    }
}
static int nearest_enc(int v)    { int b = 0; for (int i = 1; i < N_ENC; i++) if (abs(k_enc_vals[i] - v) < abs(k_enc_vals[b] - v)) b = i; return b; }

static void load_config(void)
{
    const char *path = getenv("PS_QOL_INI"); if (path) s_ini_path = path;
    FILE *f = fopen(s_ini_path, "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof line, f)) {
        char *p = line; while (*p == ' ' || *p == '\t') p++;
        if (*p == ';' || *p == '#' || *p == '\n' || *p == 0) continue;
        char *eq = strchr(p, '='); if (!eq) continue;
        *eq = 0; char *key = p, *val = eq + 1;
        for (char *q = key + strlen(key); q > key && (q[-1] == ' ' || q[-1] == '\t'); ) *--q = 0;
        char *sc = strchr(val, ';'); if (sc) *sc = 0;
        double v = atof(val);
        if (!strcmp(key, "exp_multiplier") && v >= 0) s_exp_i = nearest_mult(v);
        else if (!strcmp(key, "meseta_multiplier") && v >= 0) s_mes_i = nearest_mult(v);
        else if (!strcmp(key, "automap")) s_map_i = v != 0;
        else if (!strcmp(key, "walk_speed")) s_walk_i = v >= 3 ? 2 : v >= 1.5 ? 1 : 0;
        else if (!strcmp(key, "map_reveal")) s_reveal_i = v != 0;
        else if (!strcmp(key, "map_secrets")) s_secrets_i = v != 0;
        else if (!strcmp(key, "encounter_percent") && v >= 0) s_enc_i = nearest_enc((int)(v + 0.5));
    }
    fclose(f);
    fprintf(stderr, "[ps_qol] exp x%u  meseta x%u  encounters %d%%  automap %d  walk x%u (from %s)\n",
            exp_mult(), mes_mult(), enc_pct(), s_map_i, walk_mult(), s_ini_path);
}

static void save_config(void)
{
    FILE *f = fopen(s_ini_path, "w");
    if (!f) { fprintf(stderr, "[ps_qol] cannot write %s\n", s_ini_path); return; }
    fprintf(f, "; Phantasy Star QoL options (also editable in game: F1 / gamepad Back). Original game = 1.0 / 100.\n");
    fprintf(f, "exp_multiplier     = %-6u ; battle EXP reward multiplier (1..100)\n", exp_mult());
    fprintf(f, "meseta_multiplier  = %-6u ; battle Meseta reward multiplier (1..100)\n", mes_mult());
    fprintf(f, "encounter_percent  = %d      ; random encounter chance: 100 = original, 50 = half, 0 = none\n", enc_pct());
    fprintf(f, "automap            = %d      ; extra auto-map side window (the map is always on the HUD's MAP page)\n", s_map_i);
    fprintf(f, "walk_speed         = %-6u ; 1 = original, 2 or 4 = faster walking on maps (vehicles x2 max)\n", walk_mult());
    fprintf(f, "map_reveal         = %d      ; dungeon map: 0 = explored cells only, 1 = the whole floor\n", s_reveal_i);
    fprintf(f, "map_secrets        = %d      ; dungeon map: 1 = mark secret walls, pits, chests, events; 0 = only where you have stood\n", s_secrets_i);
    fclose(f);
}

/* ---- overworld encounter table: scaled from a saved copy of the original ---- */
static uint8_t s_rate_orig[OVERWORLD_RATE_TABLE_SIZE];
static bool    s_rate_ok;

static void apply_overworld_rate(void)
{
    if (!s_rate_ok) return;
    size_t n = 0; uint8_t *rom = glue_rom_image(&n);
    uint8_t *t = rom + OVERWORLD_RATE_TABLE_ROM_OFFSET;
    for (unsigned i = 0; i < OVERWORLD_RATE_TABLE_SIZE; i += 2) {      /* even bytes = rate, odd = zone */
        unsigned v = (s_rate_orig[i] * (unsigned)enc_pct() + 50) / 100;
        t[i] = (uint8_t)(v > 255 ? 255 : v);
    }
}

/* ---- walking / vehicle speed x2 / x4 ([insn_hooks] in game.toml; idea from the PS retranslation 2.5) ----
 * The map engine moves 16 px per step: walking 1 px x 16 frames, vehicles 2 px x 8 frames
 * (frame counter $C265 counts down from 15 / 7, vehicle type $C30E). xM = M px x 16/M frames on
 * foot, 2M px x 8/M frames in a vehicle (vehicles capped at x2 = 4 px/frame, see veh_mult).
 * The followers' sprite animation keys on counter value $0F and an animation timer of 7 (8 frames), and followers move
 * +/-1 px per frame - all divided / multiplied by M to match. Rev A addresses (other ROMs: shifted):
 *   ps_walk_frames  @$7166 -> $7173  `ld a,($C30E) ; or a ; ld a,$0F ; jr z,+ ; ld a,$07 ; ld ($C265),a`
 *   ps_walk_pixels  @$7173 -> $717D  `ld de,1 ; ld a,($C30E) ; or a ; jr z,+ ; inc e`
 *   ps_walk_animcp  @$5AA4 -> $5AA6  `cp $0F`
 *   ps_walk_animtmr @$5B3F -> $5B43  `ld (iy+$0E),7`
 *   ps_walk_follow_x @$5B70 -> $5B7A `or a ; jr nz,+ ; dec a ; add a,(iy+2) ; ld (iy+2),a`
 *   ps_walk_follow_y @$5B7B -> $5B86 `sub 2 ; jr nz,+ ; dec a ; add a,(iy+4) ; ld (iy+4),a`
 * With walk speed x1 every hook returns 0 and the original instructions run. */
#include "sms_runtime.h"
#include "z80_ops.h"
#define RAM_VEHICLE   0xC30E
#define RAM_MOVE_CNT  0xC265
/* Vehicles: at most x2 (4 px/frame). The map engine draws a newly exposed tile column in two halves
 * on consecutive frames (rows 0-11, then 12-23), so at 8 px/frame (vehicle x4) the top half of the
 * edge column stayed stale - verified 2026-11-13: Land Rover on Motavia, x4 vs x1 at the same
 * position, name-table column 0 rows 0-11 differed. On foot x4 is 4 px/frame = safe. */
static unsigned veh_mult(void) { return walk_mult() > 2 ? 2 : walk_mult(); }
int ps_walk_frames(void)
{
    if (!s_walk_i) return 0;
    uint8_t v = sms_read8(RAM_VEHICLE) ? (uint8_t)(8 / veh_mult() - 1) : (uint8_t)(16 / walk_mult() - 1);
    g_z80.a = v; sms_write8(RAM_MOVE_CNT, v);
    return 1;
}
int ps_walk_pixels(void)
{
    if (!s_walk_i) return 0;
    uint8_t veh = sms_read8(RAM_VEHICLE);
    g_z80.d = 0; g_z80.e = (uint8_t)(veh ? 2 * veh_mult() : walk_mult()); g_z80.a = veh;
    return 1;
}
int ps_walk_animcp(void)
{
    if (!s_walk_i) return 0;
    z80_cp8(&g_z80, g_z80.a, (uint8_t)(16 / walk_mult() - 1));
    return 1;
}
int ps_walk_animtmr(void)
{
    if (!s_walk_i) return 0;
    sms_write8((uint16_t)(g_z80.iy + 0x0E), (uint8_t)(8 / walk_mult() - 1));
    return 1;
}
static int follow(uint8_t a_is_positive, uint8_t off)
{
    int8_t d = (int8_t)(a_is_positive ? (int)walk_mult() : -(int)walk_mult());
    uint16_t p = (uint16_t)(g_z80.iy + off);
    uint8_t v = (uint8_t)(sms_read8(p) + d);
    sms_write8(p, v); g_z80.a = v;
    return 1;
}
int ps_walk_follow_x(void) { return s_walk_i ? follow(g_z80.a != 0, 2) : 0; }      /* a: 0 = left, 1 = right */
int ps_walk_follow_y(void) { return s_walk_i ? follow((uint8_t)(g_z80.a - 2) != 0, 4) : 0; }   /* a: 2 = up, 3 = down */

/* ---- HUD ---- */
static void on_enc_changed(void) { apply_overworld_rate(); }
static void on_map_changed(void) { ps_automap_enable(s_map_i != 0); }
static void on_view_changed(void) { ps_automap_set_view(s_reveal_i, s_secrets_i); }
static const char *const k_reveal_names[]  = { "EXPLORED", "WHOLE FLOOR" };
static const char *const k_secrets_names[] = { "HIDDEN", "SHOWN" };

static const char *const k_walk_names[] = { "X1 (ORIGINAL)", "X2", "X4" };
static const HudOption k_opts[] = {
    { "EXP MULTIPLIER",    k_mult_names, N_MULT, &s_exp_i, NULL },
    { "MESETA MULTIPLIER", k_mult_names, N_MULT, &s_mes_i, NULL },
    { "ENCOUNTERS",        k_enc_names,  N_ENC,  &s_enc_i, on_enc_changed },
    { "MAP SIDE WINDOW",   k_onoff,      2,      &s_map_i, on_map_changed },
    { "WALK SPEED",        k_walk_names, 3,      &s_walk_i, NULL },
    { "MAP REVEAL",        k_reveal_names, 2,    &s_reveal_i, on_view_changed },
    { "MAP SECRETS",       k_secrets_names, 2,   &s_secrets_i, on_view_changed },
};

static int hud_info(char lines[][41], int max)
{
    static const char *const names[4] = { "ALIS", "MYAU", "ODIN", "NOAH" };
    int n = 0;
    for (int c = 0; c < 4 && n < max - 1; c++) {
        uint16_t b = (uint16_t)(RAM_STATS + 0x10 * c);
        if (!(sms_read8(b) & 1)) continue;                       /* not in the party / dead */
        snprintf(lines[n++], 41, "%-5s LV %2u  HP %3u/%-3u  MP %3u/%-3u", names[c], sms_read8(b + 5),
                 sms_read8(b + 1), sms_read8(b + 6), sms_read8(b + 2), sms_read8(b + 7));
    }
    unsigned money = sms_read8(RAM_MONEY) | (sms_read8(RAM_MONEY + 1) << 8);
    if (n < max) snprintf(lines[n++], 41, "MESETA %u", money);
    return n;
}

/* MAP page: the dungeon map in / after a dungeon, the surroundings map on a planet or in a town
   (whichever of the two the party was in last) */
static int s_map_ctx;                  /* 0 none yet, 1 dungeon, 2 planet / town */
static void map_page_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1)
{
    if (s_map_ctx == 2 && ps_worldmap_valid()) ps_worldmap_hud_draw(fb, w, h, x0, y0, x1, y1);
    else ps_automap_hud_draw(fb, w, h, x0, y0, x1, y1);
}

static int hud_open_page(void)   /* in a dungeon: the map (test: PS_HUD_PAGE=n forces a page) */
{
    const char *e = getenv("PS_HUD_PAGE");
    if (e) return atoi(e);
    return ps_automap_in_dungeon() ? 1 : 0;
}

void ps_on_post_reset(void)
{
    static bool pages_added;
    init_names();
    load_config();
    ps_automap_init();
    ps_worldmap_init();
    s_map_ctx = 0;
    if (!pages_added) { hud_add_page("MAP", map_page_draw, NULL); pages_added = true; }
    hud_set_open_page(hud_open_page);
    size_t n = 0; uint8_t *rom = glue_rom_image(&n);
    if (rom && n >= OVERWORLD_RATE_TABLE_ROM_OFFSET + OVERWORLD_RATE_TABLE_SIZE
        && !memcmp(rom + OVERWORLD_RATE_TABLE_ROM_OFFSET, k_rate_sig, sizeof k_rate_sig)) {
        memcpy(s_rate_orig, rom + OVERWORLD_RATE_TABLE_ROM_OFFSET, OVERWORLD_RATE_TABLE_SIZE);
        s_rate_ok = true;
    } else fprintf(stderr, "[ps_qol] overworld encounter table not found: overworld encounter option disabled\n");
    if (enc_pct() != 100) apply_overworld_rate();
    ps_automap_enable(s_map_i != 0);
    ps_automap_set_view(s_reveal_i, s_secrets_i);
    hud_set_title("PHANTASY STAR - OPTIONS");
    hud_set_options(k_opts, (int)(sizeof k_opts / sizeof k_opts[0]));
    hud_set_info(hud_info);
    hud_set_on_close(save_config);
}

void ps_on_frame_post(uint64_t frame)
{
    ps_automap_frame(frame);
    ps_worldmap_frame();
    {   uint8_t mode = sms_read8(0xC202);
        if (mode == 0x0B) s_map_ctx = 1; else if (mode == 0x09) s_map_ctx = 2; }

    static uint16_t last_exp;           /* last EXP reward value we produced (0 = none yet) */
    static int      enc_base = -1, enc_written = -1;

    /* Battle rewards: scale once, right after the battle setup stored a new EXP value.
     * EXP: CalculateExp adds the reward to each living member's 16-bit EXP; on overflow it stores
     * 65535 and returns BEFORE the level-up check (`ret c`), so a character pushed to 65535 would
     * never level up again. The scaled reward is therefore capped at what every living member can
     * still take (never below the original reward = original behaviour). The game levels a
     * character up at most once per battle, also with huge rewards (original behaviour, kept).
     * Meseta: 16-bit; the game's add (LABEL_297A) clamps the money at 65535 itself. */
    if (exp_mult() != 1 || mes_mult() != 1) {
        uint16_t exp = (uint16_t)(sms_read8(RAM_EXP) | (sms_read8(RAM_EXP + 1) << 8));
        if (exp == 0) last_exp = 0;
        else if (exp != last_exp) {
            uint32_t ne = (uint32_t)exp * exp_mult();
            uint32_t room = 0xFFFF;
            for (int c = 0; c < 4; c++) {
                uint16_t sb = (uint16_t)(RAM_STATS + 0x10 * c);
                if (!(sms_read8(sb) & 1)) continue;                  /* not alive / not in the party */
                uint32_t have = sms_read8((uint16_t)(sb + 3)) | (sms_read8((uint16_t)(sb + 4)) << 8);
                if (0xFFFFu - have < room) room = 0xFFFFu - have;
            }
            if (ne > room) ne = room > exp ? room : exp;
            sms_write8(RAM_EXP, (uint8_t)ne); sms_write8(RAM_EXP + 1, (uint8_t)(ne >> 8));
            last_exp = (uint16_t)ne;
            if (mes_mult() != 1) {
                uint32_t m = (uint32_t)(sms_read8(RAM_MESETA) | (sms_read8(RAM_MESETA + 1) << 8)) * mes_mult();
                if (m > 0xFFFF) m = 0xFFFF;
                sms_write8(RAM_MESETA, (uint8_t)m); sms_write8(RAM_MESETA + 1, (uint8_t)(m >> 8));
            }
        }
    }

    /* Dungeon encounter chance: scale the per-step chance while the dungeon engine runs. */
    if (enc_pct() != 100 || enc_base >= 0) {
        if (sms_read8(RAM_GAME_MODE) == MODE_DUNGEON) {
            int cur = sms_read8(RAM_DNG_RATE);
            if (cur != enc_written) enc_base = cur;               /* the game stored a fresh value */
            unsigned w = ((unsigned)enc_base * (unsigned)enc_pct() + 50) / 100; if (w > 255) w = 255;
            if ((int)w != cur) sms_write8(RAM_DNG_RATE, (uint8_t)w);
            enc_written = (int)w;
        } else if (enc_base >= 0) {                                /* left the dungeon: put the original back */
            if (sms_read8(RAM_DNG_RATE) == (uint8_t)enc_written) sms_write8(RAM_DNG_RATE, (uint8_t)enc_base);
            enc_base = enc_written = -1;
        }
    }
}
