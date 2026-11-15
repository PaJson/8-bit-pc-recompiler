/*
 * ps_qol.c - Phantasy Star (SMS) quality-of-life options, SEGA-AGES style.
 *
 * Everything is OFF by default (the game then runs bit-identically to the ROM). Options come from
 * "ps_qol.ini" in the working directory (or the file named by $PS_QOL_INI) and can be changed
 * while playing in the settings HUD (F1 / gamepad Back; runner/hud.c). Closing the HUD writes the
 * file back.
 *
 *     exp_multiplier     = 1       ; battle EXP reward x N (0..100, 0 = none)
 *     meseta_multiplier  = 1       ; battle Meseta reward x N (0..100, 0 = none)
 *     damage_dealt       = 1       ; damage the party deals x N (0, 0.25 .. 10)
 *     damage_received    = 1       ; damage the party takes x N (0 = invincible, 0.25 .. 10)
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
#include <math.h>
#include "hud.h"
#include "safe_file.h"

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
extern void     ps_panel_draw(uint32_t *b, int w, int h);
extern void     ps_panel_frame(uint64_t frame);
extern void     ps_worldmap_frame(void);
extern bool     ps_worldmap_valid(void);
extern void     ps_worldmap_hud_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1);
extern void     ps_worldmap_hud_key(int key);
extern void     ps_worldmap_hud_draw_place(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, int map,
                                           int px, int py, bool here, const char *title, const char *const *lines, int nlines);
extern int      ps_worldmap_current_place(void);
extern int      ps_worldmap_place_tileset(int place);
extern int      ps_worldmap_num_places(void);
extern void     ps_worldmap_current_pos(int *px, int *py);
extern void     ps_automap_hud_draw_floor(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, int floor,
                                          const char *const *lines, int nlines);
extern void     ps_automap_note_map(int map, int px, int py);
extern bool     ps_automap_map_known(int map, int *px, int *py);
extern int      ps_automap_group(int floor, int *out, int max);
extern int      ps_automap_groups(int *out, int max);
extern int      ps_automap_floor(void);
extern void     ps_items_init(void);
extern void     ps_fly_init(void);
extern void     ps_fly_frame(void);
extern void     ps_fly_hud_opened(void);
extern void     ps_fly_hud_key(int key);
extern void     ps_fly_hud_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1);
extern void     ps_items_hud_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1);
extern void     ps_items_hud_key(int key);
extern void     ps_items_hud_opened(void);
extern int      g_ps_item_custom_names;

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
#define N_MULT 101                              /* multipliers x0 .. x100 (= index) */
static char        s_mult_buf[N_MULT][16];
static const char *k_mult_names[N_MULT];        /* filled by init_names() */
static char        s_pct_buf[101][8];
static const char *k_pct_names[101];           /* 0%..100%, filled by init_names() */
static const char *const k_enc_names[]  = { "NONE", "25%", "50%", "75%", "100% (ORIGINAL)", "150%", "200%" };
static const int         k_enc_vals[]   = { 0, 25, 50, 75, 100, 150, 200 };
static const char *const k_onoff[]      = { "OFF", "ON" };
#define N_ENC  7
/* damage multipliers (percent) */
static const char *const k_dmg_names[] = { "X0 (NO DAMAGE)", "X0.25", "X0.5", "X0.75", "X1 (ORIGINAL)", "X1.5", "X2", "X3", "X4", "X5", "X10" };
static const int         k_dmg_vals[]  = { 0, 25, 50, 75, 100, 150, 200, 300, 400, 500, 1000 };
#define N_DMG  11
#define DMG_X1 4
static int s_dealt_i = DMG_X1, s_recv_i = DMG_X1;
static int nearest_dmg(double v) { int b = 0; for (int i = 1; i < N_DMG; i++) if (abs(k_dmg_vals[i] - (int)(v * 100 + 0.5)) < abs(k_dmg_vals[b] - (int)(v * 100 + 0.5))) b = i; return b; }

static int s_exp_i = 1, s_mes_i = 1, s_enc_i = 4, s_map_i = 0;   /* map side window off: the map is a HUD page */
static int s_walk_i = 0;                       /* walk speed: 0 = x1 (original), 1 = x2, 2 = x4 */
static int s_reveal_i = 0, s_secrets_i = 1;    /* dungeon map: explored / whole floor; secrets hidden / shown */
static int s_panel_i = 0;                      /* widescreen side panel (party, meseta, map) off / on */
static int s_hover_i = 0;                      /* hovercraft also drives on land: off / on */
static int s_flyall_i = 0, s_flyfree_i = 0;    /* FLY page: visited places / all; the spell's rules / free */
static const char *const k_flylist_names[] = { "VISITED", "ALL" };
static const char *const k_flycost_names[] = { "SPELL (8 MP)", "FREE" };
int ps_fly_opt_all(void)  { return s_flyall_i; }
int ps_fly_opt_free(void) { return s_flyfree_i; }
static int s_bgm_i = 100, s_sfx_i = 100;       /* music / sound effects volume, percent */
/* SOUND CHIP (ps_sound.c): ini key sound_chip = fm | psg | psg_western (absent = the ROM's own) */
extern void        ps_sound_init(void);
extern int         ps_sound_mode_from_key(const char *k);
extern const char *ps_sound_mode_key(int m);
extern const char *ps_sound_mode_name(int m);
extern bool        ps_sound_mode_available(int m);
extern int         ps_sound_native_mode(void);
extern int         ps_sound_mode(void);
extern void        ps_sound_select(int m);
static int s_chip_pref = -1;                   /* mode from the ini, -1 = native */
static const double k_ff_vals[] = { 0.5, 1, 2, 4 };   /* fast-forward speed while TAB / pad RT is held */
static const char *const k_ff_names[] = { "X0.5 (SLOW)", "X1", "X2", "X4 (DEFAULT)" };
#define N_FF 4
static int s_ff_i = 3;
static const char *s_ini_path = "ps_qol.ini";

static unsigned exp_mult(void)  { return (unsigned)s_exp_i; }
static unsigned mes_mult(void)  { return (unsigned)s_mes_i; }
static unsigned walk_mult(void) { return 1u << s_walk_i; }      /* 1, 2, 4 */
static int    enc_pct(void)  { return k_enc_vals[s_enc_i]; }

static int nearest_mult(double v){ int m = (int)(v + 0.5); return m < 0 ? 0 : m > N_MULT - 1 ? N_MULT - 1 : m; }
static void init_names(void)
{
    for (int i = 0; i < N_MULT; i++) {
        snprintf(s_mult_buf[i], sizeof s_mult_buf[i], i == 0 ? "X0 (NONE)" : i == 1 ? "X1 (ORIGINAL)" : "X%d", i);
        k_mult_names[i] = s_mult_buf[i];
    }
    for (int i = 0; i <= 100; i++) { snprintf(s_pct_buf[i], sizeof s_pct_buf[i], "%d%%", i); k_pct_names[i] = s_pct_buf[i]; }
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
        else if (!strcmp(key, "damage_dealt") && v >= 0) s_dealt_i = nearest_dmg(v);
        else if (!strcmp(key, "damage_received") && v >= 0) s_recv_i = nearest_dmg(v);
        else if (!strcmp(key, "automap")) s_map_i = v != 0;
        else if (!strcmp(key, "item_names")) g_ps_item_custom_names = v != 0;
        else if (!strcmp(key, "walk_speed")) s_walk_i = v >= 3 ? 2 : v >= 1.5 ? 1 : 0;
        else if (!strcmp(key, "map_reveal")) s_reveal_i = v != 0;
        else if (!strcmp(key, "map_secrets")) s_secrets_i = v != 0;
        else if (!strcmp(key, "side_panel")) s_panel_i = v != 0;
        else if (!strcmp(key, "hovercraft_land")) s_hover_i = v != 0;
        else if (!strcmp(key, "fly_list_all")) s_flyall_i = v != 0;
        else if (!strcmp(key, "fly_free")) s_flyfree_i = v != 0;
        else if (!strcmp(key, "music_volume")) s_bgm_i = v < 0 ? 0 : v > 100 ? 100 : (int)(v + 0.5);
        else if (!strcmp(key, "sfx_volume")) s_sfx_i = v < 0 ? 0 : v > 100 ? 100 : (int)(v + 0.5);
        else if (!strcmp(key, "sound_chip")) {
            char w[32] = ""; sscanf(val, " %31s", w);
            s_chip_pref = ps_sound_mode_from_key(w);
        }
        else if (!strcmp(key, "encounter_percent") && v >= 0) s_enc_i = nearest_enc((int)(v + 0.5));
        else if (!strcmp(key, "fast_forward") && v > 0) {
            s_ff_i = 0;
            for (int i = 1; i < N_FF; i++) if (fabs(k_ff_vals[i] - v) < fabs(k_ff_vals[s_ff_i] - v)) s_ff_i = i;
        }
    }
    fclose(f);
    fprintf(stderr, "[ps_qol] exp x%u  meseta x%u  damage dealt %d%% received %d%%  encounters %d%%  automap %d  walk x%u (from %s)\n",
            exp_mult(), mes_mult(), k_dmg_vals[s_dealt_i], k_dmg_vals[s_recv_i], enc_pct(), s_map_i, walk_mult(), s_ini_path);
}

static void save_config(void)
{
    FILE *f = safe_fopen_w(s_ini_path, "w");      /* "<ini>.tmp", swapped in when complete */
    if (!f) { fprintf(stderr, "[ps_qol] cannot write %s\n", s_ini_path); return; }
    fprintf(f, "; Phantasy Star QoL options (also editable in game: F1 / gamepad Back). Original game = 1.0 / 100.\n");
    fprintf(f, "exp_multiplier     = %-6u ; battle EXP reward multiplier (0..100, 0 = none)\n", exp_mult());
    fprintf(f, "meseta_multiplier  = %-6u ; battle Meseta reward multiplier (0..100, 0 = none)\n", mes_mult());
    fprintf(f, "damage_dealt       = %-6g ; damage the party deals: 1 = original (0, 0.25, 0.5, 0.75, 1, 1.5, 2, 3, 4, 5, 10)\n", k_dmg_vals[s_dealt_i] / 100.0);
    fprintf(f, "damage_received    = %-6g ; damage the party takes: 1 = original, 0 = invincible\n", k_dmg_vals[s_recv_i] / 100.0);
    fprintf(f, "encounter_percent  = %d      ; random encounter chance: 100 = original, 50 = half, 0 = none\n", enc_pct());
    fprintf(f, "automap            = %d      ; extra auto-map side window (the map is always on the HUD's MAP page)\n", s_map_i);
    fprintf(f, "walk_speed         = %-6u ; 1 = original, 2 or 4 = faster walking on maps (vehicles x2 max)\n", walk_mult());
    fprintf(f, "map_reveal         = %d      ; dungeon map: 0 = explored cells only, 1 = the whole floor\n", s_reveal_i);
    fprintf(f, "map_secrets        = %d      ; dungeon map: 1 = mark secret walls, pits, chests, events; 0 = only where you have stood\n", s_secrets_i);
    fprintf(f, "item_names         = %d      ; HUD ITEMS page: 0 = the game's item names, 1 = ps_item_names.txt\n", g_ps_item_custom_names);
    fprintf(f, "side_panel         = %d      ; widescreen window with a side panel: party, meseta, map (like SEGA AGES)\n", s_panel_i);
    fprintf(f, "hovercraft_land    = %d      ; 1 = the hovercraft also drives on land like the Land Rover (board it anywhere)\n", s_hover_i);
    fprintf(f, "fly_list_all       = %d      ; FLY page: 0 = towns and dungeons visited in this save, 1 = every place (also script-only ones)\n", s_flyall_i);
    fprintf(f, "fly_free           = %d      ; FLY page: 0 = the Fly spell's rules (8 MP, on a map), 1 = free, also from inside a dungeon\n", s_flyfree_i);
    fprintf(f, "music_volume       = %-6d ; music volume in percent (0..100)\n", s_bgm_i);
    {   int m = ps_sound_mode() >= 0 ? ps_sound_mode() : s_chip_pref;
        if (m >= 0) fprintf(f, "sound_chip         = %-6s ; fm, psg or psg_western (a sound bank from another PS ROM in the ROM folder is used when needed)\n", ps_sound_mode_key(m)); }
    fprintf(f, "sfx_volume         = %-6d ; sound effects volume in percent (0..100)\n", s_sfx_i);
    fprintf(f, "fast_forward       = %-6g ; speed while TAB / pad RT is held (LT = x0.5): 0.5, 1, 2 or 4 (default 4)\n", k_ff_vals[s_ff_i]);
    if (!safe_fclose(f, s_ini_path)) fprintf(stderr, "[ps_qol] cannot write %s (old file kept)\n", s_ini_path);
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
#include "audio/sn76489.h"
#include "audio/ym2413.h"
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

/* ---- battle damage x N ([insn_hooks] "ps_damage", Tools/ps_find_hooks.py) ----
 * Both places where battle damage is applied, `add a,(ix+1)` with A = 256 - damage and ix = the
 * target's stats ($C400 + $10 * n: party, $C440 + $10 * n: enemies):
 *   LABEL_125E (Rev A $1268): party attacks (LABEL_121D) and party attack magic on enemies
 *   LABEL_15C2 (Rev A $15C2): enemy attacks (LABEL_1579 / LABEL_15B2) and enemy magic (LABEL_1443,
 *                             LABEL_1505) on the party
 * The hook does the add with the scaled damage (same flags), and the game continues at the
 * following `jr c` (0 HP = defeated, exactly as with the original damage). A scaled damage of 0
 * leaves the HP unchanged (carry set, Z only if the HP already was 0). A non-zero original damage
 * scaled by > 0 stays at least 1. */
int ps_damage(void)
{
    {   static int log = -1; if (log < 0) log = getenv("PS_DAMAGE_LOG") != NULL;   /* test */
        if (log) fprintf(stderr, "[dmg] target %04X damage %u hp %u\n", g_z80.ix, (256u - g_z80.a) & 0xFF, sms_read8((uint16_t)(g_z80.ix + 1))); }
    if (s_dealt_i == DMG_X1 && s_recv_i == DMG_X1) return 0;
    uint16_t ix = g_z80.ix;
    bool party = ix >= RAM_STATS && ix < RAM_STATS + 0x40, enemy = ix >= RAM_STATS + 0x40 && ix < RAM_STATS + 0xC0;
    if ((!party && !enemy) || g_z80.a == 0) return 0;
    int pct = k_dmg_vals[party ? s_recv_i : s_dealt_i];
    if (pct == 100) return 0;
    unsigned d = 256u - g_z80.a;
    unsigned nd = (d * (unsigned)pct + 50) / 100;
    if (pct > 0 && nd == 0) nd = 1;
    if (nd > 255) nd = 255;
    uint8_t hp = sms_read8((uint16_t)(ix + 1));
    if (nd == 0) {                                      /* no damage: A = HP, carry set */
        g_z80.a = z80_add8(&g_z80, hp, 0, 0);
        z80f_set(&g_z80, Z80_FLAG_C, true);
    } else g_z80.a = z80_add8(&g_z80, (uint8_t)(256u - nd), hp, 0);
    return 1;
}

/* ---- hovercraft also drives on land ([insn_hooks] "ps_hover_*", Tools/ps_find_hooks.py) ----
 * LABEL_74E4 decides whether the party / vehicle can move: it reads a tile attribute byte
 * (LABEL_7764: metatile under a probe point -> attribute table in bank 3) at two probe points.
 * On foot and in the Land Rover a tile blocks if bit 0 is set (water, mountains, walls ...); the
 * hovercraft is blocked by bits 1 | 3 (land, town tiles). With the option the hovercraft is
 * blocked only where both are: it drives on water and on land (over towns like the Land Rover;
 * get out to enter them, as with the Land Rover). Encounters, the vehicle sprite etc. are the
 * game's own (vehicles: a quarter of the encounter rate, for all vehicles).
 *   ps_hover_pass  @ both `and $0A` of the hovercraft branch (Rev A $7535, $753F)
 *   ps_hover_board @ ItemUse_Hovercraft `ld a,(Interaction_Type)` (Rev A $22F5): the game refuses if
 *     Interaction_Type = 0, else LABEL_76C1 looks for water next to the party (8 probe sets) and
 *     moves the party onto it, or fails ("will do no good here"). The hook runs that same search
 *     (call_by_address); if it finds no water it runs the Land Rover's boarding search (LABEL_7656:
 *     4 probe sets, mask $0D, shifts the party by up to one cell like the Land Rover) -> resume at
 *     `ld a,$08 ; ld ($C30E),a`; if both fail the original code runs (and fails as before). */

static uint16_t s_attr_fn, s_it_addr, s_water_fn, s_rover_fn;   /* LABEL_7764, Interaction_Type, LABEL_76C1, LABEL_7656 */
static long find_rom(const char *hex)
{
    size_t n = 0; const uint8_t *rom = glue_rom_image(&n);
    int pat[64], np = 0;
    for (const char *p = hex; *p && np < 64; ) {
        while (*p == ' ') p++;
        if (!*p) break;
        pat[np++] = p[0] == '.' ? -1 : (int)strtol((char[3]){ p[0], p[1], 0 }, NULL, 16);
        p += 2;
    }
    long hit = -1;
    for (size_t i = 0; rom && i + (size_t)np <= n && i < 0x8000; i++) {
        int k = 0; while (k < np && (pat[k] < 0 || rom[i + k] == pat[k])) k++;
        if (k == np) { if (hit >= 0) return -1; hit = (long)i; }      /* must be unique */
    }
    return hit;
}
static void hover_init(void)
{
    size_t n = 0; const uint8_t *rom = glue_rom_image(&n);
    s_attr_fn = s_it_addr = 0;
    long d = find_rom("fe 08 20 .. d5 06 00 21 .. .. 09 eb 1a cd .. .. e6 0a 20 .. 13 1a cd .. .. e6 0a");
    long b = find_rom("3a 08 c3 fe 04 21 .. .. 30 .. 3a .. .. b7 28 .. c5 d5 cd .. .. d1 c1 21 .. .. 20 .. 3e 08 32 0e c3");
    /* LABEL_7656: ld bc,$0400 ; - push bc ; ld b,0 ; ld hl,LABEL_7A35 ; add hl,bc ; ex de,hl ; ld a,(de) ; call LABEL_7764 ; and $0D */
    long r = find_rom("01 00 04 c5 06 00 21 .. .. 09 eb 1a cd .. .. e6 0d");
    if (d < 0 || b < 0 || r < 0) return;
    unsigned t = rom[r + 7] | (rom[r + 8] << 8);
    if (t >= 0x8000 || rom[d + 14] != rom[r + 13] || rom[d + 15] != rom[r + 14]) return;
    s_rover_fn = (uint16_t)r;
    s_attr_fn = (uint16_t)(rom[d + 14] | (rom[d + 15] << 8));
    s_it_addr = (uint16_t)(rom[b + 11] | (rom[b + 12] << 8));
    s_water_fn = (uint16_t)(rom[b + 19] | (rom[b + 20] << 8));
}
int ps_hover_pass(void)
{
    if (!s_hover_i) return 0;
    {   static int log = -1; if (log < 0) log = getenv("PS_HOVER_LOG") != NULL;   /* test */
        if (log) fprintf(stderr, "[hover] pass: attr %02X\n", g_z80.a); }
    g_z80.a = z80_and8(&g_z80, g_z80.a, (g_z80.a & 1) ? 0x0A : 0x00);
    return 1;
}
/* run a game routine that returns Z / A = 0 on success (LABEL_76C1, LABEL_7656), as its callers do */
static bool game_call(uint16_t fn)
{
    g_z80.sp = (uint16_t)(g_z80.sp - 2); sms_write16(g_z80.sp, 0x0000);   /* return address, popped by its ret */
    call_by_address(fn);
    return g_z80.a == 0;
}
int ps_hover_board(void)
{
    if (!s_hover_i || !s_attr_fn || !sms_read8(s_it_addr)) return 0;     /* the game refuses anyway */
    uint16_t bc = z80_bc(&g_z80), de = z80_de(&g_z80), hl = z80_hl(&g_z80);
    int how = game_call(s_water_fn) ? 1 : game_call(s_rover_fn) ? 2 : 0;   /* water next to the party / land */
    {   static int log = -1; if (log < 0) log = getenv("PS_HOVER_LOG") != NULL;   /* test */
        if (log) fprintf(stderr, "[hover] board: %s\n", how == 1 ? "water" : how == 2 ? "land" : "no"); }
    z80_set_bc(&g_z80, bc); z80_set_de(&g_z80, de); z80_set_hl(&g_z80, hl);
    return how ? 1 : 0;                        /* 1: board (ld a,$08 ; ld ($C30E),a ...) */
}

/* ---- quick vehicles: LB / key C = Land Rover (Ice Digger on Dezoris), RB / key V = hovercraft ----
 * The game's own rules, without the menu and the landscape screen. Using a vehicle item
 * (ItemUse_LandRover / _Hovercraft / _IceDigger, PlayerMenu_Item):
 *   - getting in: only on a planet ($C308 < 4; Ice Digger: $C308 = 2) and with the item in the
 *     inventory; the search for a spot (LABEL_7656 Land Rover / Ice Digger: walkable cells, shifts
 *     the party by up to one cell; LABEL_76C1 hovercraft: water next to the party; with HOVERCRAFT
 *     ON LAND also LABEL_7656, as ps_hover_board) must succeed -> $C30E = 4 / 12 / 8. Using another
 *     vehicle's item while driving is the same boarding (the game switches vehicles directly).
 *   - getting out: the item of the vehicle you are in -> LABEL_7656 (Land Rover / Ice Digger) or
 *     LABEL_7732 (hovercraft: land next to it, by the facing direction $C2D7) -> $C30E = 0.
 *   The menu then goes back through GameMode_LoadInteraction / _Interaction to GameMode_LoadMap,
 *   which builds the party / vehicle object for $C30E. We do the RAM writes of that path (LABEL_B41,
 *   the Interaction exit; not LoadInteraction's scroll reset, LoadMap does that) and set GameMode_LoadMap: one fade
 *   out / in like Fly. The Interaction_Type check of the item code always passes there (LoadInteraction
 *   makes it non-zero), so it is not repeated. Runs in the ps_fly_go hook (GameMode_Map, standing
 *   still, after the battle check); a refused press does nothing. Test env PS_VEH_PRESS=frame:n,...
 *   (n = 1 Land Rover, 2 hovercraft), PS_VEH_LOG=1. */
#define RAM_INV   0xC4C0
#define RAM_INV_N 0xC4E2
static uint16_t s_exit_fn;                     /* LABEL_7732 */
static int s_veh_pending, s_veh_age;           /* 1 = LB action, 2 = RB action */
static void vehicle_init(void)
{
    long x = find_rom("3a d7 c2 e6 03 4f 87 81 87 4f 06 08");
    s_exit_fn = x > 0 ? (uint16_t)x : 0;
    if (!s_exit_fn || !s_water_fn || !s_rover_fn || !s_it_addr) s_exit_fn = 0;
    if (!s_exit_fn)
        fprintf(stderr, "[ps_qol] vehicle routines not found: quick vehicles disabled\n");
}
static bool has_item(uint8_t id)
{
    int n = sms_read8(RAM_INV_N); if (n > 24) n = 24;
    for (int i = 0; i < n; i++) if (sms_read8((uint16_t)(RAM_INV + i)) == id) return true;
    return false;
}

/* ---- EXP add per character ([insn_hooks] ps_exp_add, Tools/ps_find_hooks.py group "exp") ----
 * CalculateExp: `ld hl,(CurrentBattle_EXPReward) ; add hl,de ; jr nc,+ ; ld hl,$FFFF ; + ld (iy+exp),l ;
 * ld (iy+exp+1),h ; ret c ; ...level-up check`. On overflow the game stores 65535 and skips that
 * character's level-up check (carry). With an EXP multiplier the hook adds with saturation at 65535
 * and no carry, so every character gets the full reward (up to 65535) and its level-up check runs;
 * an earlier version capped the whole party's reward at the fullest member's headroom instead (user
 * report 2026-11-14: Alis at 65535 -> everybody got x1). At x1 the original code runs. */
int ps_exp_add(void)
{
    if (exp_mult() == 1) return 0;
    uint32_t sum = (uint32_t)z80_hl(&g_z80) + z80_de(&g_z80);
    z80_set_hl(&g_z80, (uint16_t)(sum > 0xFFFF ? 0xFFFF : sum));
    z80f_set(&g_z80, Z80_FLAG_C, false);
    return 1;                                  /* -> ld (iy+exp),l */
}

/* ---- battle auto attack: RB / key V = on, LB / key C = off (only during a battle) ----
 * LABEL_100F runs a battle; each round it walks the party (LABEL_1074, $C267 = character) and for
 * every living member without a command opens the command menu (LABEL_108A: draws the enemy-name,
 * command and character windows, CheckOptionSelect at cursor $7882, BattleMenu_OptionTbl).
 * BattleMenu_Attack takes no target: LABEL_1BB9 stores {c=1, b=0, a=0} at $C2AC + 4*$C267, then
 * $C267++. With auto attack on, the ps_battle_cmd hook (at the first window call) does exactly that
 * and resumes at LABEL_10B7 (next character): no menu is drawn. If a battle command menu is already
 * open when RB is pressed, ps_battle_select (CheckOptionSelect's button read) answers it with ATTACK:
 * cursor $C26B = 0 and Ctrl_1_pressed = button 2; that menu is recognised by the return address of
 * its `call CheckOptionSelect` on the stack (the map's player menu uses the same cursor / count).
 * "In battle" = from LABEL_100F (ps_battle_begin) to the next walking step (ps_items_walk_mark):
 * there LB / RB switch auto attack instead of the vehicles. Auto attack is off at every battle start.
 * Test env PS_VEH_PRESS (1 = LB, 2 = RB), PS_BATTLE_LOG=1. */
static bool s_in_battle, s_auto, s_battle_won;   /* won: all enemies down, victory texts follow */
static uint16_t s_cmd_ret;                     /* return address of the battle menu's CheckOptionSelect */
static void battle_init(void)
{
    long m = find_rom("11 0c 00 19 7e b7 20 .. cd .. .. cd .. .. cd .. .. 21 82 78 22 69 c2 3e 04 32 6e c2 cd .. .. cb 61 c2 .. .. 21 .. .. cd .. .. cd .. .. 3a 67 c2 fe 04");
    s_cmd_ret = m > 0 ? (uint16_t)(m + 31) : 0;
    s_in_battle = s_auto = s_battle_won = false;
}
static int battle_log(void) { static int on = -1; if (on < 0) on = getenv("PS_BATTLE_LOG") != NULL; return on; }
int ps_battle_begin(void)
{
    s_in_battle = true; s_auto = s_battle_won = false;
    if (battle_log()) fprintf(stderr, "[battle] begin\n");
    return 0;
}
void ps_battle_walking(void)                   /* ps_items_walk_mark: walking again = no battle */
{
    if (s_in_battle && battle_log()) fprintf(stderr, "[battle] end (walking)\n");
    s_in_battle = s_auto = s_battle_won = false;
}
int ps_battle_cmd(void)
{
    if (!s_auto) return 0;
    uint8_t ch = sms_read8(0xC267);
    uint16_t p = (uint16_t)(0xC2AC + 4 * ch);
    sms_write8(p, 1); sms_write8((uint16_t)(p + 1), 0); sms_write8((uint16_t)(p + 2), 0);   /* LABEL_1BB9: attack */
    sms_write8(0xC267, (uint8_t)(ch + 1));
    if (battle_log()) fprintf(stderr, "[battle] auto attack: character %u\n", ch);
    return 1;                                  /* -> LABEL_10B7 */
}
int ps_battle_select(void)
{
    if (s_auto && s_cmd_ret && sms_read16(g_z80.sp) == s_cmd_ret) {
        sms_write8(0xC26B, 0);                 /* ATTACK */
        sms_write8(0xC205, 0x20);              /* Ctrl_1_pressed = button 2 (confirm) */
        if (battle_log()) fprintf(stderr, "[battle] auto attack: open menu answered\n");
    }
    return 0;
}
/* auto attack ends with the battle: as soon as no enemy has HP left (the battle loop's own victory
   test after each action, LABEL_10E7: HP at $C441 + 16*n, 8 slots) - before the victory texts */
static void battle_frame(void)
{
    if (!s_in_battle || s_battle_won) return;
    for (int n = 0; n < 8; n++) if (sms_read8((uint16_t)(0xC441 + 16 * n))) return;
    s_battle_won = true;
    if (!s_auto) return;
    s_auto = false;
    if (battle_log()) fprintf(stderr, "[battle] all enemies down: auto attack off\n");
}
static void battle_overlay(uint32_t *fb, int w, int h)
{
    if (!(s_in_battle && s_auto)) return;
    const char *t = "AUTO ATTACK: ON";
    int tw = 6 * (int)strlen(t);
    hud_fill(fb, w, h, 4, 4, 4 + tw + 7, 4 + 13, 0xFF101830u, true);
    hud_fill(fb, w, h, 4, 4, 4 + tw + 7, 5, 0xFF60D0FFu, false);
    hud_fill(fb, w, h, 4, 4 + 12, 4 + tw + 7, 4 + 13, 0xFF60D0FFu, false);
    hud_text(fb, w, h, 8, 7, t, 0xFFFFE040u);
}
static void vehicle_frame(uint64_t frame)
{
    static uint8_t last;
    uint8_t m = hud_extra_buttons();
    {   static const char *e = (const char *)1; if (e == (const char *)1) e = getenv("PS_VEH_PRESS");   /* test */
        for (const char *p = e; p && *p; ) {
            unsigned long long f = strtoull(p, (char **)&p, 10);
            int k = *p == ':' ? atoi(p + 1) : 0;
            if (f == frame && (k == 1 || k == 2)) m |= (uint8_t)k;
            p = strchr(p, ','); if (p) p++;
        } }
    uint8_t rise = (uint8_t)(m & ~last); last = m;
    if (s_in_battle) {                         /* battle: LB / RB = auto attack off / on */
        if (s_battle_won) rise = 0;            /* victory texts: nothing to switch any more */
        if (rise & HUD_EXTRA_2) s_auto = true;
        if (rise & HUD_EXTRA_1) s_auto = false;
        if ((rise & 3) && battle_log()) fprintf(stderr, "[battle] auto attack %s\n", s_auto ? "on" : "off");
        s_veh_pending = 0;
        return;
    }
    if (rise & HUD_EXTRA_1) { s_veh_pending = 1; s_veh_age = 0; }
    else if (rise & HUD_EXTRA_2) { s_veh_pending = 2; s_veh_age = 0; }
    else if (s_veh_pending && ++s_veh_age > 30) s_veh_pending = 0;   /* not walking on a map: drop it */
}
/* called from the ps_fly_go hook; 1 = done (the hook returns with Z set -> `ret z`) */
int ps_vehicle_go(void)
{
    int act = s_veh_pending;
    if (!act || sms_read8(RAM_GAME_MODE) != 0x09 || sms_read8(0xC265) || !s_exit_fn) return 0;
    s_veh_pending = 0;
    uint8_t planet = sms_read8(0xC308), cur = sms_read8(0xC30E), item, want;
    if (act == 1 && (cur == 4 || cur == 12)) { want = cur; item = cur == 12 ? 0x23 : 0x21; }   /* get out */
    else if (act == 1) { item = planet == 2 && has_item(0x23) ? 0x23 : 0x21; want = item == 0x23 ? 12 : 4; }
    else          { item = 0x22; want = 8; }
    bool ok = false, out = cur == want;
    const char *why = NULL;
    uint16_t bc = z80_bc(&g_z80), de = z80_de(&g_z80), hl = z80_hl(&g_z80);
    uint8_t a = g_z80.a, f = g_z80.f, dir = sms_read8(0xC2D7);
    if (planet >= 4) why = "not on a planet";
    else if (!has_item(item)) why = "item not owned";
    else {
        sms_write8(0xC2D7, sms_read8(0xC810));             /* LABEL_B41: facing direction (LABEL_7732 uses it) */
        if (out) ok = game_call(want == 8 ? s_exit_fn : s_rover_fn);
        else if (want == 8) ok = game_call(s_water_fn) || (s_hover_i && game_call(s_rover_fn));
        else ok = game_call(s_rover_fn);
        if (!ok) why = out ? "no place to get out" : "no place to get in";
    }
    z80_set_bc(&g_z80, bc); z80_set_de(&g_z80, de); z80_set_hl(&g_z80, hl); g_z80.a = a; g_z80.f = f;
    {   static int log = -1; if (log < 0) log = getenv("PS_VEH_LOG") != NULL;
        if (log) fprintf(stderr, "[veh] item %02X vehicle %u -> %s%s\n", item, cur, ok ? (out ? "out" : "in") : "refused: ", ok ? "" : why); }
    if (!ok) { sms_write8(0xC2D7, dir); return 0; }
    sms_write8(0xC30E, out ? 0 : want);
    sms_write8(0xC29D, 0);                                     /* LABEL_B41 */
    for (uint16_t p = 0xC26F; p <= 0xC286; p++) sms_write8(p, 0);
    for (uint16_t p = 0xC800; p <= 0xC8FF; p++) sms_write8(p, 0);
    sms_write8(0xC2E9, 0);                                     /* GameMode_LoadInteraction (its $C300/$C304 = 0 are
                                                                  the VDP scroll shadows: LoadMap sets them after its own
                                                                  fade-out; zeroing them here moved the picture mid-fade) */
    sms_write8(s_it_addr, 0); sms_write8(0xC2D5, 0);           /* GameMode_Interaction exit */
    sms_write8(0xC2DB, 0); sms_write8(0xC2DC, 0);
    sms_write8(RAM_GAME_MODE, 0x08);                           /* GameMode_LoadMap */
    z80f_set(&g_z80, Z80_FLAG_Z, true);
    return 1;
}

/* ---- HUD ---- */
static void on_enc_changed(void) { apply_overworld_rate(); }
static void on_map_changed(void) { ps_automap_enable(s_map_i != 0); }
static void on_panel_changed(void) { hud_set_panel_enabled(s_panel_i != 0); }
static void on_view_changed(void) { ps_automap_set_view(s_reveal_i, s_secrets_i); }
static void on_ff_changed(void) { hud_set_turbo_speed(k_ff_vals[s_ff_i]); }
static void on_panel_toggled(bool on) { s_panel_i = on; save_config(); }   /* pad Back / F2 */

/* ---- BUTTONS page: every pad button / key and what it does (incl. our additions) ---- */
static void buttons_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1)
{
    const uint32_t CYAN = 0xFF60D0FF, WHITE = 0xFFFFFFFF, GREY = 0xFFA0A0B0, YEL = 0xFFFFE040;
    char ff[16];
    snprintf(ff, sizeof ff, "FAST X%g", k_ff_vals[s_ff_i]);
    bool st = hud_start_opens();
    const char *rows[][3] = {
        { NULL,          "PAD",         "KEYBOARD" },
        { "MOVE",        "D-PAD/STICK", "ARROWS" },
        { "BUTTON 1",    "A, X",        "Z" },
        { "BUTTON 2",    "B, Y",        "X" },
        { "HUD",         st ? "START" : "BACK", "F1" },
        { "SIDE PANEL",  st ? "BACK" : "-",     "F2" },
        { ff,            "RT (HOLD)",   "TAB (HOLD)" },
        { "SLOW X0.5",   "LT (HOLD)",   "-" },
        { "ROVER/DIGR",  "LB",          "C" },
        { "HOVERCRAFT",  "RB",          "V" },
        { "AUTO ATTACK", "RB, LB OFF",  "V, C OFF" },   /* in battle */
        { "FULLSCREEN",  "-",           "F11, ALT+ENTER" },
        { "PAUSE (SMS)", st ? "-" : "START", "ENTER" },          /* the console's Pause button (NMI) */
        { "QUIT",        "-",           "ESC" },
        { "HUD TABS",    "LB, RB",      "Q, E" },
        { "HUD CONFIRM", "B",           "ENTER/SPACE/X" },
        { "HUD CLOSE",   st ? "A, START" : "A, BACK", "ESC, Z, F1" },
    };
    int n = (int)(sizeof rows / sizeof rows[0]), y = y0, cp = x0 + 4 + 6 * 12, ck = cp + 6 * 12;   /* 11 + 11 + 14 chars */
    for (int i = 0; i < n && y + 7 <= y1; i++, y += 8) {
        if (!rows[i][0]) { hud_text(fb, w, h, cp, y, rows[i][1], CYAN); hud_text(fb, w, h, ck, y, rows[i][2], CYAN); continue; }
        hud_text(fb, w, h, x0 + 4, y, rows[i][0], i == 6 || i == 7 ? YEL : WHITE);
        hud_text(fb, w, h, cp, y, rows[i][1], strcmp(rows[i][1], "-") ? WHITE : GREY);
        hud_text(fb, w, h, ck, y, rows[i][2], strcmp(rows[i][2], "-") ? WHITE : GREY);
    }
}
static const char *const k_reveal_names[]  = { "EXPLORED", "WHOLE FLOOR" };
static const char *const k_secrets_names[] = { "HIDDEN", "SHOWN" };

static const char *const k_walk_names[] = { "X1 (ORIGINAL)", "X2", "X4" };

/* ---- music / sound effects volume ----
 * The sound driver (bank 12 PSG driver, and the FM drivers of the Japanese ROMs / SEGA AGES) keeps
 * 32-byte channel slots in RAM: music $C00E.. (FM) / $C06E..$C0CE (PSG) and the jingle set
 * $C14E..$C1CE, sound effects in the three slots $C0EE, $C10E, $C12E (Rev A: commands $A0-$CF
 * start in $C0EE / $C10E, $D0-$D4 in $C12E; all SFX headers checked). Slot +0 bit 7 = playing,
 * +1 = the output: PSG $80/$A0/$C0/$E0 (tone 0-2, noise), FM $10+n (FM channel n). A playing SFX
 * slot takes its channel over (the music slot of that channel is muted meanwhile), so every frame
 * the channels used by a playing SFX slot get the SFX volume and all others the music volume.
 * Measured 2026-11-14 (SMS_PSGLOG_IX): FM mode never makes the PSG audible (SFX on FM 4/5).
 * Exception: SFX $B8 (Rev A) starts in the music slots (it stops the music) = music volume. */
static unsigned vol_gain(int pct) { double g = pct / 100.0; return (unsigned)(g * g * 256.0 + 0.5); }  /* loudness-like curve */
static void sound_frame(void)
{
    static const uint16_t slots[3] = { 0xC0EE, 0xC10E, 0xC12E };
    bool sfx_psg[4] = { false }, sfx_fm[9] = { false };
    for (int k = 0; k < 3; k++) {
        if (!(sms_read8(slots[k]) & 0x80)) continue;
        uint8_t c = sms_read8((uint16_t)(slots[k] + 1));
        if (c & 0x80) sfx_psg[(c >> 5) & 3] = true;
        else if ((c & 0xF0) == 0x10 && (c & 0x0F) < 9) sfx_fm[c & 0x0F] = true;
    }
    unsigned gb = vol_gain(s_bgm_i), gs = vol_gain(s_sfx_i);
    for (unsigned i = 0; i < 4; i++) psg_set_channel_gain(i, sfx_psg[i] ? gs : gb);
    for (unsigned i = 0; i < 14; i++) fm_set_channel_gain(i, i < 9 && sfx_fm[i] ? gs : gb);
    {   /* test: PS_SND_LOG=1 prints every change of the SFX channel set */
        static int on = -1; static unsigned last;
        if (on < 0) on = getenv("PS_SND_LOG") != NULL;
        unsigned m = 0;
        for (int i = 0; i < 4; i++) m |= (unsigned)sfx_psg[i] << i;
        for (int i = 0; i < 9; i++) m |= (unsigned)sfx_fm[i] << (8 + i);
        if (on && m != last) { fprintf(stderr, "[snd] sfx psg %X fm %03X\n", m & 15, m >> 8); last = m; }
    }
}
static void on_volume_changed(void) { sound_frame(); }
/* SOUND CHIP choices: the modes available for this ROM (its own driver bank(s) + other PS ROMs in
   the ROM folder), the ROM's own mode marked ORIGINAL; the option exists only with 2+ modes */
static int  s_chip_i, s_chip_n, s_chip_modes[3];
static char s_chip_buf[3][32];
static const char *s_chip_names[3];
static void on_chip_changed(void) { if (s_chip_i >= 0 && s_chip_i < s_chip_n) ps_sound_select(s_chip_modes[s_chip_i]); }
static HudOption k_sound_opts[3];
static int k_sound_nopts;
static void build_sound_opts(void)
{
    s_chip_n = 0;
    for (int m = 0; m < 3; m++) {
        if (!ps_sound_mode_available(m)) continue;
        snprintf(s_chip_buf[s_chip_n], sizeof s_chip_buf[0], "%s%s", ps_sound_mode_name(m),
                 m == ps_sound_native_mode() ? " (ORIGINAL)" : "");
        s_chip_names[s_chip_n] = s_chip_buf[s_chip_n];
        s_chip_modes[s_chip_n++] = m;
    }
    int want = s_chip_pref >= 0 && ps_sound_mode_available(s_chip_pref) ? s_chip_pref
             : ps_sound_mode() >= 0 ? ps_sound_mode() : ps_sound_native_mode();
    s_chip_i = 0;
    for (int i = 0; i < s_chip_n; i++) if (s_chip_modes[i] == want) s_chip_i = i;
    if (s_chip_n) ps_sound_select(s_chip_modes[s_chip_i]);
    k_sound_nopts = 0;
    k_sound_opts[k_sound_nopts++] = (HudOption){ "MUSIC",         k_pct_names, 101, &s_bgm_i, on_volume_changed };
    k_sound_opts[k_sound_nopts++] = (HudOption){ "SOUND EFFECTS", k_pct_names, 101, &s_sfx_i, on_volume_changed };
    if (s_chip_n >= 2)
        k_sound_opts[k_sound_nopts++] = (HudOption){ "SOUND CHIP", s_chip_names, s_chip_n, &s_chip_i, on_chip_changed };
}
static const HudOption k_mult_opts[] = {
    { "EXP",               k_mult_names, N_MULT, &s_exp_i, NULL },
    { "MESETA",            k_mult_names, N_MULT, &s_mes_i, NULL },
    { "DAMAGE DEALT",      k_dmg_names,  N_DMG,  &s_dealt_i, NULL },
    { "DAMAGE RECEIVED",   k_dmg_names,  N_DMG,  &s_recv_i, NULL },
};
static const HudOption k_opts[] = {
    { "ENCOUNTERS",        k_enc_names,  N_ENC,  &s_enc_i, on_enc_changed },
    { "MAP SIDE WINDOW",   k_onoff,      2,      &s_map_i, on_map_changed },
    { "WALK SPEED",        k_walk_names, 3,      &s_walk_i, NULL },
    { "FAST FORWARD",      k_ff_names,   N_FF,   &s_ff_i, on_ff_changed },
    { "SIDE PANEL",        k_onoff,      2,      &s_panel_i, on_panel_changed },
    { "MAP REVEAL",        k_reveal_names, 2,    &s_reveal_i, on_view_changed },
    { "MAP SECRETS",       k_secrets_names, 2,   &s_secrets_i, on_view_changed },
    { "HOVERCRAFT ON LAND", k_onoff,     2,      &s_hover_i, NULL },
    { "FLY LIST",          k_flylist_names, 2,   &s_flyall_i, NULL },
    { "FLY COST",          k_flycost_names, 2,   &s_flyfree_i, NULL },
};

/* ---- MAP page ----
 * Shows one place at a time: a planet or town visited, or an explored dungeon (= floors linked by
 * plain stairs). Left/Right: previous / next place (planets, towns, dungeons). Up/Down: planets =
 * area <-> whole planet, dungeons = floor up / down, towns = nothing. The page opens on the place
 * the party is in (or was in last). Visited places are kept per save slot (ps_automap.c). */
static int s_map_ctx;                  /* 0 none yet, 1 dungeon, 2 planet / town */
int ps_map_context(void) { return s_map_ctx == 2 && !ps_worldmap_valid() ? 0 : s_map_ctx; }

enum { PL_MAP, PL_DUNGEON };
typedef struct { int kind, id; } Place;        /* id: map number, or the dungeon's lowest floor */
static Place s_sel = { -1, -1 };
static int   s_sel_floor = -1;

static int places(Place *out, int max)
{
    int n = 0, px, py;
    for (int m = 0; m < ps_worldmap_num_places() && n < max; m++)   /* planets, then towns (by planet) */
        if (ps_automap_map_known(m, &px, &py)) out[n++] = (Place){ PL_MAP, m };
    int g[64], ng = ps_automap_groups(g, 64);
    for (int i = 0; i < ng && n < max; i++) out[n++] = (Place){ PL_DUNGEON, g[i] };
    return n;
}

static int dungeon_of(int floor) { int f[64]; return ps_automap_group(floor, f, 64) ? f[0] : -1; }

static void select_current(void)
{
    s_sel = (Place){ -1, -1 }; s_sel_floor = -1;
    if (s_map_ctx == 1 && ps_automap_floor() >= 0) {
        s_sel_floor = ps_automap_floor();
        s_sel = (Place){ PL_DUNGEON, dungeon_of(s_sel_floor) };
    } else if (s_map_ctx == 2 && ps_worldmap_current_place() >= 0) s_sel = (Place){ PL_MAP, ps_worldmap_current_place() };
}

static void select_place(Place p)
{
    s_sel = p; s_sel_floor = -1;
    if (p.kind != PL_DUNGEON) return;
    int f[64], n = ps_automap_group(p.id, f, 64), cur = ps_automap_floor();
    s_sel_floor = n ? f[0] : p.id;
    for (int i = 0; i < n; i++) if (f[i] == cur) s_sel_floor = cur;   /* the floor the party is / was on */
}

static void map_page_key(int key)
{
    Place pl[96]; int n = places(pl, 96), i = 0;
    while (i < n && !(pl[i].kind == s_sel.kind && pl[i].id == s_sel.id)) i++;
    if (key == HUD_KEY_LEFT || key == HUD_KEY_RIGHT) {
        if (!n) return;
        i = i >= n ? 0 : (i + (key == HUD_KEY_RIGHT ? 1 : n - 1)) % n;
        select_place(pl[i]);
        return;
    }
    if (s_sel.kind == PL_MAP) {                          /* area <-> whole planet / town */
        ps_worldmap_hud_key(key);
        return;
    }
    if (s_sel.kind == PL_DUNGEON) {                      /* floor up (higher number) / down */
        int f[64], m = ps_automap_group(s_sel.id, f, 64), k = 0;
        while (k < m && f[k] != s_sel_floor) k++;
        if (k >= m) return;
        k += key == HUD_KEY_UP ? 1 : -1;
        if (k >= 0 && k < m) s_sel_floor = f[k];
    }
}

static void map_page_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1)
{
    Place pl[96]; int n = places(pl, 96), i = 0;
    while (i < n && !(pl[i].kind == s_sel.kind && pl[i].id == s_sel.id)) i++;
    if (i >= n && n) { select_place(pl[0]); i = 0; }
    char l1[24], l2[24];
    const char *lines[4]; int nl = 0;
    if (n > 1) { snprintf(l1, sizeof l1, "< MAP %d/%d >", i + 1, n); lines[nl++] = l1; }
    if (!n || s_sel.kind < 0) { ps_automap_hud_draw_floor(fb, w, h, x0, y0, x1, y1, -1, NULL, 0); return; }
    if (s_sel.kind == PL_DUNGEON) {
        int f[64], m = ps_automap_group(s_sel.id, f, 64), k = 0;
        while (k < m && f[k] != s_sel_floor) k++;
        if (m > 1) { snprintf(l2, sizeof l2, "FLOOR %d OF %d", k + 1, m); lines[nl++] = l2; }
        ps_automap_hud_draw_floor(fb, w, h, x0, y0, x1, y1, s_sel_floor, lines, nl);
        return;
    }
    int px = 0, py = 0;
    ps_automap_map_known(s_sel.id, &px, &py);
    bool here = s_map_ctx == 2 && ps_worldmap_current_place() == s_sel.id;
    ps_worldmap_hud_draw_place(fb, w, h, x0, y0, x1, y1, s_sel.id, px, py, here, NULL, lines, nl);
}

static int hud_open_page(void)   /* always the first tab, MAP (test: PS_HUD_PAGE=n forces a page) */
{
    select_current();
    ps_items_hud_opened();
    ps_fly_hud_opened();
    const char *e = getenv("PS_HUD_PAGE");
    if (e) return atoi(e);
    return hud_page_index("MAP");
}

void ps_on_post_reset(void)
{
    static bool pages_added;
    ps_sound_init();          /* first: may append sound banks from other ROMs to the ROM image */
    init_names();
    load_config();
    build_sound_opts();
    ps_automap_init();
    ps_worldmap_init();
    hover_init();
    vehicle_init();
    battle_init();
    ps_items_init();
    ps_fly_init();
    s_map_ctx = 0;
    if (!pages_added) {
        hud_add_option_page("MULTIPLIERS", k_mult_opts, (int)(sizeof k_mult_opts / sizeof k_mult_opts[0]));
        hud_add_page_ex("ITEMS", ps_items_hud_draw, ps_items_hud_key, true);
        hud_add_page_ex("MAP", map_page_draw, map_page_key, true);
        hud_add_page_ex("FLY", ps_fly_hud_draw, ps_fly_hud_key, true);
        hud_add_option_page("SOUND", k_sound_opts, k_sound_nopts);
        hud_add_page("BUTTONS", buttons_draw, NULL);
        static const char *const order[] = { "MAP", "FLY", "ITEMS", "OPTIONS", "MULTIPLIERS", "SOUND", "BUTTONS" };
        hud_set_page_order(order, (int)(sizeof order / sizeof order[0]));
        pages_added = true;
    }
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
    hud_set_turbo_speed(k_ff_vals[s_ff_i]);
    hud_set_title("PHANTASY STAR - OPTIONS");
    hud_set_options(k_opts, (int)(sizeof k_opts / sizeof k_opts[0]));
    hud_set_on_close(save_config);
    hud_set_panel(ps_panel_draw);
    hud_set_panel_enabled(s_panel_i != 0);
    hud_set_on_panel_toggle(on_panel_toggled);
    hud_set_overlay(battle_overlay);
}

void ps_on_frame_post(uint64_t frame)
{
    ps_automap_frame(frame);
    ps_worldmap_frame();
    {   uint8_t mode = sms_read8(0xC202);
        if (mode == 0x0B) s_map_ctx = 1;
        else if (mode == 0x04 || mode == 0x05) s_map_ctx = 2;  /* spaceship flight: the planet (ps_worldmap.c) */
        else if (mode == 0x09 || mode == 0x0F) {            /* map, or the tube walk (GameMode_Road) */
            s_map_ctx = 2;
            int place = ps_worldmap_current_place(), px, py;
            if (place >= 0 && ps_worldmap_valid()) { ps_worldmap_current_pos(&px, &py); ps_automap_note_map(place, px, py); }
        } }
    ps_panel_frame(frame);
    ps_fly_frame();
    vehicle_frame(frame);
    battle_frame();
    sound_frame();

    {   /* test: PS_DAMAGE_LOG=1 prints every HP change of the party (P0..3) and the enemies (E0..7) */
        static int on = -1; static uint8_t prev[12];
        if (on < 0) { on = getenv("PS_DAMAGE_LOG") != NULL; for (int i = 0; i < 12; i++) prev[i] = sms_read8((uint16_t)(RAM_STATS + 0x10 * i + 1)); }
        if (on) for (int i = 0; i < 12; i++) {
            uint8_t hp = sms_read8((uint16_t)(RAM_STATS + 0x10 * i + 1));
            if (hp != prev[i]) fprintf(stderr, "[hp] %llu %c%d %u -> %u mode %02X\n", (unsigned long long)frame,
                                       i < 4 ? 'P' : 'E', i < 4 ? i : i - 4, prev[i], hp, sms_read8(RAM_GAME_MODE));
            prev[i] = hp;
        }
    }
    static uint16_t last_exp;           /* last EXP reward value we produced (0 = none yet) */
    static int      enc_base = -1, enc_written = -1;

    /* Battle rewards: scale once, right after the battle setup stored a new EXP value (16-bit,
     * clamped at 65535; AwardEXP shows it in the victory text and CalculateExp adds it to every
     * living member). The overflow of a character's EXP is handled per character by ps_exp_add.
     * The game levels a character up at most once per battle (original behaviour, kept).
     * Meseta: 16-bit; the game's add (LABEL_297A) clamps the money at 65535 itself. */
    if (exp_mult() != 1 || mes_mult() != 1) {
        uint16_t exp = (uint16_t)(sms_read8(RAM_EXP) | (sms_read8(RAM_EXP + 1) << 8));
        if (exp == 0) last_exp = 0;
        else if (exp != last_exp) {
            uint32_t ne = (uint32_t)exp * exp_mult();
            if (ne > 0xFFFF) ne = 0xFFFF;
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
