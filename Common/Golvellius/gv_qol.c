/*
 * gv_qol.c - Golvellius (USA, Europe, Brazil), CRC32 A51376FE: quality-of-life options and fixes.
 * Every option defaults to the original game; settings in gv_qol.ini (working directory = the game
 * folder, or $GV_QOL_INI), changed in the HUD (F1 / pad Start), saved when the HUD closes.
 *
 *  MAP FIXES (SMS Power "Golvellius SMS Bugfix" v1.1, our own runtime version): two bytes of the
 *    7-byte room table in bank 5, read from the ROM at every screen change, so they are patched
 *    in the in-memory ROM image and can be switched live:
 *      $15CB9 $8E -> $7E  screen C5 used room 142 (a duplicate of E1) instead of its own room 126
 *      $15E16 $94 -> $2B  wrong cave graphic on screen H5
 *  SWORD HIT FIX (Alianger's Improvement Hack v1.1 "sword hit detection fix", own code): original:
 *    every other sword hit only stuns unless the sword is held out. Object field +$1F: bit 7 =
 *    stunned / hit, low bits = countdown (code at $4520). The three swing starts `ld a,4 ; ld ($C120),a`
 *    ($C120 = sound effect 4 = the swing; a test fired it on every sword press) at $3F45, $43AC, $4678 get an observing
 *    [insn_hooks] hook that clears +$1F of the 13 objects at $C160 + 32*n ($C17F + 32*n), exactly
 *    what that IPS's new routine ($90B9 in bank 12) does at the same three spots. Not yet confirmed
 *    by playing: 30000 frames of random input (71 swings) never found a stale flag at those spots.
 *  FAST FORWARD: speed while TAB / pad RT is held (engine hud_set_turbo_speed; LT = x0.5).
 *  MULTIPLIERS tab (x0..x10): DAMAGE DEALT ($33EE), DAMAGE RECEIVED ($37D8, healing unchanged),
 *    GOLD DROPS ($35EA, 6-digit BCD add, the game's GOLD MAX cap still applies).
 *  DINA'S PAYOUT 150 / 2600 (bank 7 $914C), MAP TELEPORT off / visited screens / visited password
 *    caves (MAP tab, zoomed screen, B twice; done at the main loop head $0435 the way $042F loads a
 *    screen), SOUND tab OVERWORLD MUSIC (auto or a theme unlocked by sword + shield, $19FE; switched
 *    live at $0435), SIDE PANEL (gv_panel.c: surroundings, gold, power, world map; F2; pad Back stays
 *    the console's Pause).
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "hud.h"
#include "sms_runtime.h"
#include "safe_file.h"
#include "gv_patch.h"
#include "video/sms_vdp.h"

extern uint8_t *glue_rom_image(size_t *size);

#define ROM_CRC_SIZE 0x40000
static const struct { uint32_t off; uint8_t orig, fixed; } k_map_fix[2] = {
    { 0x15CB9, 0x8E, 0x7E },
    { 0x15E16, 0x94, 0x2B },
};

static const char *const k_onoff[] = { "OFF (ORIGINAL)", "ON" };
static const double k_ff_vals[] = { 0.5, 1, 2, 4 };
static const char *const k_ff_names[] = { "X0.5 (SLOW)", "X1", "X2", "X4 (DEFAULT)" };
static int s_mapfix_i, s_hitfix_i, s_ff_i = 3, s_pw_i, s_cave_i, s_free_i;
/* MULTIPLIERS tab (x0..x10, index = factor; 1 = original), DINA, TELEPORT, OVERWORLD MUSIC */
static int s_dealt_i = 1, s_recv_i = 1, s_gold_i = 1, s_dina_i, s_tp_i, s_music_i, s_panel_i, s_8way_i, s_reach_i;
static const char *const k_reach[] = { "ORIGINAL", "+4 PIXELS", "+8 PIXELS" };
static const char *const k_mult[] = { "X0", "X1 (ORIGINAL)", "X2", "X3", "X4", "X5", "X6", "X7", "X8", "X9", "X10" };
static const char *const k_dina[] = { "150 GOLD (ORIGINAL)", "2600 GOLD (ALIANGER)" };
static const char *const k_tp[]   = { "OFF (ORIGINAL)", "VISITED SCREENS", "PASSWORD CAVES" };
static char s_music_names[6][24] = { "AUTO (ORIGINAL)" };
static const char *k_music[6] = { s_music_names[0], s_music_names[1], s_music_names[2], s_music_names[3], s_music_names[4], s_music_names[5] };
/* Alianger's cave wall tweaks (Improvement Hack v1.1, level data in bank 5): applied from the user's own
   IPS file at run time (gv_patch.c); takes effect at the next cave screen the game loads */
static GvPatch s_cave = { "Golvellius v1.1 top down cave wall tweaks.ips", 0xAC5D5CDDu, 434 };
/* Alianger's free crystals (a cheat, user decision: default off): the price bytes of the crystal caves in
   the cave table bank 7 $8D87 (2 bytes per cave, `($CAD3-$B0)*2`, copied to $C16C/$C16D when a cave is
   entered: so it applies from the next cave entered) and the crystal seller's text at $A486. All data. */
static GvPatch s_free = { "Golvellius free crystals.ips", 0x0C3E537Bu, 288 };
static const char *const k_alianger[] = { "OFF (ORIGINAL)", "ON (ALIANGER)" };
static const char *const k_missing[]  = { "IPS FILE MISSING" };
static const char *const k_cheat[]    = { "OFF (ORIGINAL)", "ON (CHEAT, ALIANGER)" };
static bool s_rom_ok;
static const char *s_ini = "gv_qol.ini";

static void apply_map_fix(void)
{
    size_t n = 0; uint8_t *rom = glue_rom_image(&n);
    if (!s_rom_ok || !rom) return;
    for (int i = 0; i < 2; i++) rom[k_map_fix[i].off] = s_mapfix_i ? k_map_fix[i].fixed : k_map_fix[i].orig;
}

static void load_config(void)
{
    const char *e = getenv("GV_QOL_INI"); if (e) s_ini = e;
    FILE *f = fopen(s_ini, "r"); if (!f) return;
    char line[200];
    while (fgets(line, sizeof line, f)) {
        char *c = strchr(line, ';'); if (c) *c = 0;
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = 0; char key[64] = ""; sscanf(line, " %63s", key);
        double v = atof(eq + 1);
        if (!strcmp(key, "map_fixes")) s_mapfix_i = v != 0;
        else if (!strcmp(key, "sword_hit_fix")) s_hitfix_i = v != 0;
        else if (!strcmp(key, "remember_password")) s_pw_i = v != 0;
        else if (!strcmp(key, "cave_wall_tweaks")) s_cave_i = v != 0;
        else if (!strcmp(key, "free_crystals")) s_free_i = v != 0;
        else if (!strcmp(key, "damage_dealt")) s_dealt_i = v < 0 ? 0 : v > 10 ? 10 : (int)v;
        else if (!strcmp(key, "damage_received")) s_recv_i = v < 0 ? 0 : v > 10 ? 10 : (int)v;
        else if (!strcmp(key, "gold_multiplier")) s_gold_i = v < 0 ? 0 : v > 10 ? 10 : (int)v;
        else if (!strcmp(key, "dina_2600")) s_dina_i = v != 0;
        else if (!strcmp(key, "side_panel")) s_panel_i = v != 0;
        else if (!strcmp(key, "teleport")) s_tp_i = v < 0 ? 0 : v > 2 ? 2 : (int)v;
        else if (!strcmp(key, "eight_way_walking")) s_8way_i = v != 0;
        else if (!strcmp(key, "sword_reach")) s_reach_i = v < 0 ? 0 : v > 2 ? 2 : (int)v;
        else if (!strcmp(key, "overworld_music")) s_music_i = v < 0 ? 0 : v > 5 ? 5 : (int)v;
        else if (!strcmp(key, "fast_forward") && v > 0) {
            s_ff_i = 0; for (int i = 1; i < 4; i++) if (fabs(k_ff_vals[i] - v) < fabs(k_ff_vals[s_ff_i] - v)) s_ff_i = i;
        }
    }
    fclose(f);
}
static void save_config(void)
{
    FILE *f = safe_fopen_w(s_ini, "w"); if (!f) return;   /* "<ini>.tmp", swapped in when complete */
    fprintf(f, "; Golvellius options (also in game: F1 / gamepad Start). 0 = original game.\n");
    fprintf(f, "map_fixes      = %d     ; 1 = screen C5 gets its own room, screen H5 the right cave graphic (SMS Power bug fix)\n", s_mapfix_i);
    fprintf(f, "sword_hit_fix  = %d     ; 1 = every sword swing can hit (original: every other swing only stuns)\n", s_hitfix_i);
    fprintf(f, "remember_password = %d  ; 1 = the last password shown is saved and pre-filled on the password screen\n", s_pw_i);
    fprintf(f, "cave_wall_tweaks = %d   ; 1 = Alianger's top-down cave wall tweaks (needs his IPS file in ROMs/Patches)\n", s_cave_i);
    fprintf(f, "free_crystals  = %d     ; 1 = cheat: crystals cost nothing (Alianger's IPS file in ROMs/Patches)\n", s_free_i);
    fprintf(f, "damage_dealt   = %-5d ; sword damage to enemies x0..x10 (1 = original)\n", s_dealt_i);
    fprintf(f, "damage_received = %-4d ; damage you take x0..x10 (1 = original; healing is not changed)\n", s_recv_i);
    fprintf(f, "gold_multiplier = %-4d ; gold dropped by enemies x0..x10 (1 = original; GOLD MAX still caps it)\n", s_gold_i);
    fprintf(f, "side_panel     = %d     ; widescreen window with a side panel: the surrounding screens, gold, power (F2)\n", s_panel_i);
    fprintf(f, "dina_2600      = %d     ; 1 = Dina pays 2600 gold instead of 150 (amount from Alianger's hack)\n", s_dina_i);
    fprintf(f, "teleport       = %d     ; MAP tab teleport: 0 off, 1 to visited screens, 2 to visited password caves\n", s_tp_i);
    fprintf(f, "eight_way_walking = %d  ; 1 = diagonal input walks diagonally on the overworld (original: left / right only)\n", s_8way_i);
    fprintf(f, "sword_reach    = %d     ; sword hit area: 0 original, 1 = 4 pixels wider on every side, 2 = 8 pixels\n", s_reach_i);
    fprintf(f, "overworld_music = %d    ; 0 = the game's choice (by sword + shield), 1..5 = that theme once unlocked\n", s_music_i);
    fprintf(f, "fast_forward   = %-5g ; speed while TAB / pad RT is held: 0.5, 1, 2 or 4 (pad LT = x0.5)\n", k_ff_vals[s_ff_i]);
    safe_fclose(f, s_ini);
}

static void on_map_fix(void) { apply_map_fix(); }
static void on_cave(void) { gv_patch_set(&s_cave, s_cave_i != 0); }
static void on_free(void) { gv_patch_set(&s_free, s_free_i != 0); }
static void on_panel(void) { hud_set_panel_enabled(s_panel_i != 0); }
extern void gv_map_init(void);
extern void gv_panel_draw(uint32_t *b, int w, int h);
extern void gv_panel_test(uint64_t frame);
static void save_config(void);
static void on_panel_toggled(bool on) { s_panel_i = on; save_config(); }   /* F2 */
extern void gv_map_save(void);
extern void gv_map_hud_closed(void);
static void on_close(void) { save_config(); gv_map_save(); gv_map_hud_closed(); }
static int open_page(void) { return hud_page_index("MAP"); }
static void on_ff(void) { hud_set_turbo_speed(k_ff_vals[s_ff_i]); }
static HudOption k_opts[] = {          /* not const: an option whose IPS file is missing gets k_missing */
    { "MAP FIXES",      k_onoff,    2, &s_mapfix_i, on_map_fix },
    { "SWORD HIT FIX",  k_onoff,    2, &s_hitfix_i, NULL },
    { "FAST FORWARD",   k_ff_names, 4, &s_ff_i,     on_ff },
    { "SIDE PANEL",     k_onoff,    2, &s_panel_i,  on_panel },
    { "REMEMBER PASSWORD", k_onoff, 2, &s_pw_i,     NULL },
    { "CAVE WALL TWEAKS", k_alianger, 2, &s_cave_i, on_cave },
    { "FREE CRYSTALS",  k_cheat,    2, &s_free_i,   on_free },
    { "DINA'S PAYOUT",  k_dina,     2, &s_dina_i,   NULL },
    { "MAP TELEPORT",   k_tp,       3, &s_tp_i,     NULL },
    { "8-WAY WALKING",  k_onoff,    2, &s_8way_i,   NULL },
    { "SWORD REACH",    k_reach,    3, &s_reach_i,  NULL },
};
static const HudOption k_mult_opts[] = {
    { "DAMAGE DEALT",    k_mult, 11, &s_dealt_i, NULL },
    { "DAMAGE RECEIVED", k_mult, 11, &s_recv_i,  NULL },
    { "GOLD DROPS",      k_mult, 11, &s_gold_i,  NULL },
};
static bool s_music_dirty;
static void on_music(void) { s_music_dirty = true; }
static const HudOption k_sound_opts[] = {
    { "OVERWORLD MUSIC", k_music, 6, &s_music_i, on_music },
};

/* ==== MULTIPLIERS, DINA, OVERWORLD MUSIC, MAP TELEPORT ([insn_hooks]; all return 0 = original) ==== */
static void call_game(uint16_t addr)          /* run a game routine from a hook (its RET pops the dummy) */
{
    g_z80.sp = (uint16_t)(g_z80.sp - 2); sms_write16(g_z80.sp, 0x0000);
    call_by_address(addr);
}
static int bcd2(uint8_t b) { return (b >> 4) * 10 + (b & 15); }
static uint8_t to_bcd(int v) { return (uint8_t)(((v / 10) % 10) << 4 | (v % 10)); }
static bool qlog(void) { static int on = -1; if (on < 0) on = getenv("GV_LOG") != NULL; return on; }   /* test */

/* $33EE `ld e,a`: A = the sword's damage to the enemy at IX (sword level $C848 = 1/3/6, or table $3421
   for enemy types $50-$5F); then `ld a,(ix+$15) ; sub e` = its HP. Scaled damage is capped at the HP
   (exactly 0 = the game's own kill path `ret z`). */
int gv_dmg_dealt(void)
{
    int d = g_z80.a * s_dealt_i, hp = sms_read8((uint16_t)(g_z80.ix + 0x15));
    if (d > hp && s_dealt_i != 1) d = hp;
    if (qlog()) fprintf(stderr, "[gv] hit: damage %d -> %d, enemy HP %d\n", g_z80.a, s_dealt_i == 1 ? g_z80.a : d, hp);
    if (s_dealt_i == 1) return 0;
    g_z80.e = (uint8_t)d;
    return 1;                                                 /* -> $33EF */
}
/* $37D8 `ld a,(hl)`: HL = table $3697 + the type of what touched the hero ($C80E); negative = damage
   (power sub-units, shield applied afterwards at $3804), positive = healing (not scaled). */
int gv_dmg_recv(void)
{
    int v0 = (int8_t)sms_read8((uint16_t)(g_z80.h << 8 | g_z80.l)), v = v0;
    if (v < 0) { v *= s_recv_i; if (v < -128) v = -128; }
    if (qlog()) fprintf(stderr, "[gv] touched: %d -> %d (power %02X)\n", v0, v, sms_read8(0xC836));
    if (s_recv_i == 1) return 0;
    g_z80.a = (uint8_t)v;
    return 1;                                                 /* -> $37D9 (`and a` sets the flags again) */
}
/* $35EA: E = the enemy's gold drop (BCD, in tens) about to be added to the gold $C83F-$C841 (BCD tens);
   we add the scaled amount over all 6 digits and hand the game's own GOLD MAX cap at $3604 the carry
   (carry = overflow past 999999 -> gold = max). */
int gv_gold_drop(void)
{
    if (qlog()) fprintf(stderr, "[gv] gold drop %d0 x%d (gold %02X%02X%02X)\n", bcd2(g_z80.e), s_gold_i, sms_read8(0xC83F), sms_read8(0xC840), sms_read8(0xC841));
    if (s_gold_i == 1) return 0;
    long g = bcd2(sms_read8(0xC83F)) * 10000L + bcd2(sms_read8(0xC840)) * 100L + bcd2(sms_read8(0xC841));
    g += (long)bcd2(g_z80.e) * s_gold_i;
    bool over = g > 999999;
    if (!over) {
        sms_write8(0xC83F, to_bcd((int)(g / 10000))); sms_write8(0xC840, to_bcd((int)(g / 100 % 100)));
        sms_write8(0xC841, to_bcd((int)(g % 100)));
    }
    g_z80.f = (uint8_t)((g_z80.f & ~1) | (over ? 1 : 0));
    return 1;                                                 /* -> $3604 */
}
/* bank 7 $914C `ld bc,$0015`: Dina's payout (BCD tens: 150), 2600 = Alianger's amount */
int gv_dina(void)
{
    if (!s_dina_i) return 0;
    g_z80.b = 0x02; g_z80.c = 0x60;
    return 1;                                                 /* -> $914F */
}

/* Overworld music: $19EE picks table $1A15[$C848 + $C849 - 1] (sword + shield level), 8 entries,
   5 different songs. A theme is unlocked once the equipment has reached it. */
static const uint8_t k_theme[5] = { 0x20, 0x19, 0x06, 0x0C, 0x03 }, k_theme_from[5] = { 0, 1, 3, 4, 7 };
static int equip_idx(void)
{
    int i = sms_read8(0xC848) + sms_read8(0xC849) - 1;
    return i < 0 ? 0 : i > 7 ? 7 : i;
}
static bool theme_unlocked(int k) { return sms_read8(0xC802) >= 5 && k_theme_from[k] <= equip_idx(); }
static void music_names(void)
{
    for (int k = 0; k < 5; k++)
        snprintf(s_music_names[k + 1], sizeof s_music_names[k + 1], theme_unlocked(k) ? "THEME %d" : "THEME %d (LOCKED)", k + 1);
}
static int music_song(void)                    /* what the overworld should play now */
{
    if (s_music_i > 0 && theme_unlocked(s_music_i - 1)) return k_theme[s_music_i - 1];
    size_t n = 0; const uint8_t *rom = glue_rom_image(&n);
    return rom && n > 0x1A1D ? rom[0x1A15 + equip_idx()] : -1;
}
/* $19FE `ld a,(hl)` = the song from that table */
int gv_ow_music(void)
{
    if (qlog()) fprintf(stderr, "[gv] overworld song: table %02X, option %d%s\n", sms_read8((uint16_t)(g_z80.h << 8 | g_z80.l)), s_music_i,
                        s_music_i && theme_unlocked(s_music_i - 1) ? " (used)" : "");
    if (s_music_i == 0 || !theme_unlocked(s_music_i - 1)) return 0;
    g_z80.a = k_theme[s_music_i - 1];
    return 1;                                                 /* -> $19FF `call $77A6` */
}

/* Password caves = screens whose cave is Winkle (content code 3): screen -> room `$15890[s]`, room records
   `$15947 + 7*room` (byte 3 = the cave id, `$CAD3`; 0 = `$AE`), content code = bank 7 `$8CDA[id >> 4]` for
   ids below $50, else `$8C8F[id]` (bank 7 $8E44). From the ROM: M2 E4 A7 J9 L9 F10. */
static bool s_pw_cave[182];
static void find_pw_caves(void)
{
    size_t n = 0; const uint8_t *rom = glue_rom_image(&n);
    if (!rom || n < 0x20000) return;
    const uint8_t *b7 = rom + 7 * 0x4000 - 0x8000;
    for (int s = 0; s < 182; s++) {
        int room = rom[0x15890 + s], id = rom[0x15947 + room * 7 + 3];
        if (!id) id = 0xAE;
        int code = (id >> 4) < 5 ? b7[0x8CDA + (id >> 4)] : b7[0x8C8F + id];
        s_pw_cave[s] = code == 3;
    }
}
bool gv_pw_cave(int idx) { return idx >= 0 && idx < 182 && s_pw_cave[idx]; }

/* MAP TELEPORT: the MAP tab asks (gv_tp_request), the main loop head ($0435, once per frame) does what
   the game does at $042F: `call $0D1A ; call $1978` = load the screen in $C80D (picture, enemies,
   music). Only on the overworld (mode 6/7 with the overworld display, no scroll). The hero then stands
   on the free cell nearest to the screen's entrance (the metatile = room byte 6, $CAD6; nearest to the
   centre if there is none): free = attribute $D000[metatile] <= the hero's terrain level $C817 (the
   test of $2D06 / $3DB9), not on the edge (that would scroll). Cell (row, col) = hero y 8 + 16 * row,
   x 16 + 16 * col. */
extern bool gv_map_seen(int idx);
static int s_tp_req = -1;
static bool on_overworld(void)
{
    uint8_t m = sms_read8(0xC802);
    return (m == 6 || m == 7) && (g_vdp.reg[0] & 0x20) && sms_read8(0xC814) == 0 && sms_read8(0xC80D) < 182;
}
bool gv_tp_allowed(int idx)
{
    if (!s_tp_i || idx < 0 || idx >= 182 || !gv_map_seen(idx) || !on_overworld() || idx == sms_read8(0xC80D)) return false;
    return s_tp_i == 1 || s_pw_cave[idx];
}
void gv_tp_request(int idx) { if (gv_tp_allowed(idx)) s_tp_req = idx; }
static void tp_place_hero(void)
{
    uint8_t ent = sms_read8(0xCAD6), lvl = sms_read8(0xC817);
    int er = 6, ec = 7, best = -1, bd = 1 << 30;
    int cell = sms_read8(0xCAD2);                            /* room byte 2 = the entrance cell (also while hidden) */
    if (sms_read8(0xCAD3) && ent && cell < 180) { er = cell / 15; ec = cell % 15; }
    for (int r = 1; r < 11; r++)
        for (int c = 1; c < 14; c++) {
            uint8_t mt = sms_read8((uint16_t)(0xCA00 + r * 15 + c));
            if ((ent && mt == ent) || (r == er && c == ec) || sms_read8((uint16_t)(0xD000 + mt)) > lvl) continue;
            int d = (r - er) * (r - er) * 4 + (c - ec) * (c - ec) * 4 + (r < er ? 1 : 0);   /* below first */
            if (d < bd) { bd = d; best = r * 15 + c; }
        }
    if (best < 0) return;                                    /* no free cell: keep the position */
    sms_write8(0xC101, (uint8_t)(8 + 16 * (best / 15)));
    sms_write8(0xC102, (uint8_t)(16 + 16 * (best % 15)));
    sms_write8(0xC105, sms_read8(0xC105) & 0x7F);            /* standing, not walking */
    sms_write8(0xC110, sms_read8(0xC110) & 0x7F);
}
static void play_song(int song)
{
    int old = sms_slot_bank(0x8000);
    sms_write8(0xFFFF, 2);                                   /* as $19E9: the driver's data bank */
    g_z80.a = (uint8_t)song;
    call_game(0x77A6);
    sms_write8(0xFFFF, (uint8_t)old);
}
/* The game's own load sequence (display off, area id $C819, its metatiles $CC00 / terrain $D000, the
   screen $CA00, display on, fade-in) runs inside the teleport's call_game($0D1A / $1978) and takes
   ~110 frames. The frame hook places the hero the frame the display comes back (else he is seen at
   the old spot during the fade-in); after the calls he is placed again (same tables, same cell).
   s_tp_place: 1 = waiting for the display to go off, 2 = to come back on, 0 = idle / done. */
static int s_tp_place;
static void tp_place_tick(void)                             /* every frame (gv_on_frame_post) */
{
    if (!s_tp_place) return;
    bool on = (g_vdp.reg[1] & 0x40) != 0;
    if (s_tp_place == 1) { if (!on) s_tp_place = 2; return; }
    if (!on) return;
    s_tp_place = 0;
    if (sms_read8(0xC802) == 6) tp_place_hero();
}
int gv_frame_top(void)
{
    if (s_tp_req < 0 && !s_music_dirty) return 0;
    if (!on_overworld()) { if (sms_read8(0xC802) < 5) s_tp_req = -1; return 0; }   /* wait; drop it on the title */
    Z80State keep = g_z80;
    if (s_tp_req >= 0) {
        int to = s_tp_req; s_tp_req = -1;
        if (getenv("GV_LOG")) fprintf(stderr, "[gv] teleport %d -> %d\n", sms_read8(0xC80D), to);
        sms_write8(0xC802, 6);                               /* as the password start ($2023) */
        sms_write8(0xC80D, (uint8_t)to);
        s_tp_place = 1;                                      /* the hero: when the display is back on */
        call_game(0x0D1A);
        call_game(0x1978);                                   /* its music pick goes through gv_ow_music */
        s_tp_place = 0;
        tp_place_hero();
        if (qlog()) fprintf(stderr, "[gv] teleport: hero at %d,%d\n", sms_read8(0xC101), sms_read8(0xC102));
        s_music_dirty = false;
    } else if (s_music_dirty) {
        s_music_dirty = false;
        int song = music_song();
        if (qlog()) fprintf(stderr, "[gv] music switch: song %02X\n", song);
        if (song >= 0) play_song(song);
    }
    uint64_t cyc = g_z80.cyc; g_z80 = keep; g_z80.cyc = cyc;
    return 0;                                                /* the frame goes on as usual */
}

/* ==== 8-WAY WALKING and SWORD REACH ====
 * Overworld walking (mode 6): $0DCA turns the pad into 9 directions $C801 (0 DL, 1 L, 2 UL, 3 D, 4 none,
 * 5 U, 6 DR, 7 R, 8 UR); $3D40 dispatches the hero (IX = $C100) through table $3D6B, where the original
 * maps every diagonal to left / right. The hero's facing (ix+$10): left $01, right $07, up $85, down $83
 * (bit 7 = vertical); 1 px per frame; a turn is only taken when the hero is on an 8-px line across the
 * new direction (else the handler keeps the old direction), walls are tested when a new 16-px cell is
 * entered ($2D06). gv_walk_dir (hook on $3D40 `ld a,($C801)`) turns a diagonal into one of the game's
 * own straight moves per frame: it keeps the current axis until the hero is on an 8-px line, then
 * takes the other axis (a fine staircase = diagonal); an axis that did not move the hero last time is
 * skipped (walls), so the hero slides along them. All the game's own rules (walls, caves, the screen
 * edge scroll) stay in force because the game's handlers do the moving. */
static uint8_t s_walk_last_y, s_walk_last_x; static int s_walk_axis = -1;   /* 0 horizontal, 1 vertical */
static bool s_walk_blocked[2];
int gv_walk_dir(void)
{
    if (!s_8way_i || sms_read8(0xC802) != 6) { s_walk_axis = -1; return 0; }
    int d = sms_read8(0xC801);
    if (d != 0 && d != 2 && d != 6 && d != 8) { s_walk_axis = -1; return 0; }
    uint8_t y = sms_read8(0xC101), x = sms_read8(0xC102), f = sms_read8(0xC110);
    if (s_walk_axis >= 0) s_walk_blocked[s_walk_axis] = (y == s_walk_last_y && x == s_walk_last_x);
    int cur = (f & 0x80) ? 1 : 0, axis;
    bool aligned = cur ? (y & 7) == 0 : (x & 7) == 0;          /* on a line where the game lets it turn */
    if (!aligned) axis = cur;
    else if (s_walk_axis < 0) axis = 0;
    else axis = s_walk_blocked[!cur] ? cur : !cur;              /* alternate, unless the other way is shut */
    if (s_walk_blocked[0] && s_walk_blocked[1]) { s_walk_blocked[0] = s_walk_blocked[1] = false; }
    s_walk_axis = axis; s_walk_last_y = y; s_walk_last_x = x;
    int h = d <= 2 ? 1 : 7, v = (d == 2 || d == 8) ? 5 : 3;
    g_z80.a = (uint8_t)(axis ? v : h);
    return 1;                                                   /* -> $3D43 */
}
/* $3509: an enemy tests its box against the sword objects $C120 / $C140 (type $84 while they hit; box
   +$16..+$19 = low / high of both axes, $3538). SWORD REACH widens the box of an active sword whenever
   the sword's own code has (re)written it (never our own widened box again). */
int gv_sword_reach(void)
{
    if (!s_reach_i) return 0;
    static uint8_t wrote[2][4];
    for (int k = 0; k < 2; k++) {
        uint16_t o = (uint16_t)(0xC120 + 0x20 * k);
        if (sms_read8(o) != 0x84) continue;
        uint8_t b[4]; for (int i = 0; i < 4; i++) b[i] = sms_read8((uint16_t)(o + 0x16 + i));
        if (!memcmp(b, wrote[k], 4)) continue;                 /* still the box we widened */
        int n = 4 * s_reach_i;
        int lo0 = b[0] - n, hi0 = b[1] + n, lo1 = b[2] - n, hi1 = b[3] + n;
        b[0] = (uint8_t)(lo0 < 0 ? 0 : lo0); b[1] = (uint8_t)(hi0 > 255 ? 255 : hi0);
        b[2] = (uint8_t)(lo1 < 0 ? 0 : lo1); b[3] = (uint8_t)(hi1 > 255 ? 255 : hi1);
        for (int i = 0; i < 4; i++) sms_write8((uint16_t)(o + 0x16 + i), b[i]);
        memcpy(wrote[k], b, 4);
        if (qlog()) fprintf(stderr, "[gv] sword box %02X-%02X %02X-%02X\n", b[0], b[1], b[2], b[3]);
    }
    return 0;
}

/* [insn_hooks] gv_sword_swing at the three swing starts `ld a,4 ; ld ($C120),a`: observe only */
int gv_sword_swing(void)
{
    static int log = -1; if (log < 0) log = getenv("GV_LOG") != NULL;     /* test */
    if (log) {
        int stale = 0; for (int n = 0; n < 13; n++) stale += sms_read8((uint16_t)(0xC17F + 32 * n)) != 0;
        static unsigned long swings, with_stale; swings++; with_stale += stale > 0;
        fprintf(stderr, "[gv] swing %lu: %d object(s) with a stale hit flag (%lu of %lu swings so far)%s\n",
                swings, stale, with_stale, swings, s_hitfix_i ? " -> cleared" : "");
    }
    if (s_hitfix_i)
        for (int n = 0; n < 13; n++) sms_write8((uint16_t)(0xC17F + 32 * n), 0);
    return 0;
}

/* ---- REMEMBER PASSWORD (a battery save that keeps the password system) ----
 * The password generator (bank 7 $852B: copies the 20 progress bytes $C1B0..$C1C3 to $C190, then
 * shifts them into 32 five-bit symbols at $C170, `ret` at $8553) runs in the password house (and
 * at game over). gv_pw_made (observing hook on that `ret`) saves the 32 symbols to
 * <save dir>/<rom name>.password (not with SMS_NO_SAVE). The password screen (bank 7 $85F6: clears
 * $C170..$C18F, 13 bytes up to $8603) gets them back instead of zeros (gv_pw_entry, resume $8603),
 * so the last password is already entered; the game's own decoder checks and loads it. Typed
 * passwords work as before. Same idea as Ichigobankai's SRAM hack, which also saves the password
 * (+ the screen $C80D and $C100..$C11F to resume at the password house: not done here yet). */
extern const char *glue_save_path(void);
static uint8_t s_pw[32]; static bool s_pw_have, s_pw_draw;
static void pw_path(char *out, size_t n)
{
    const char *sp = glue_save_path();
    snprintf(out, n, "%s", sp && sp[0] ? sp : "golvellius.sav");
    size_t l = strlen(out);
    if (l > 4 && !strcmp(out + l - 4, ".sav")) out[l - 4] = 0;
    strncat(out, ".password", n - strlen(out) - 1);
}
static void pw_load(void)
{
    char p[1100]; pw_path(p, sizeof p);
    FILE *f = fopen(p, "rb"); s_pw_have = false;
    if (f) { s_pw_have = fread(s_pw, 1, 32, f) == 32; fclose(f); }
    for (int i = 0; i < 32 && s_pw_have; i++)               /* symbols are 0..31 (5 bits): else damaged */
        if (s_pw[i] > 31) { s_pw_have = false; fprintf(stderr, "[gv] %s: not a valid password (ignored)\n", p); }
}
static bool pw_log(void) { static int on = -1; if (on < 0) on = getenv("GV_LOG") != NULL; return on; }
int gv_pw_made(void)
{
    for (int i = 0; i < 32; i++) s_pw[i] = sms_read8((uint16_t)(0xC170 + i));
    s_pw_have = true;
    if (pw_log()) { fprintf(stderr, "[gv] password made:"); for (int i = 0; i < 32; i++) fprintf(stderr, " %02X", s_pw[i]); fprintf(stderr, "\n"); }
    if (!s_pw_i || getenv("SMS_NO_SAVE")) return 0;
    char p[1100]; pw_path(p, sizeof p);
    FILE *f = safe_fopen_w(p, "wb");                        /* "<file>.tmp", swapped in when complete */
    if (f) { fwrite(s_pw, 1, 32, f); safe_fclose(f, p); }
    return 0;
}
int gv_pw_entry(void)
{
    if (!s_pw_i || !s_pw_have) return 0;                    /* original: clear the buffer */
    for (int i = 0; i < 32; i++) sms_write8((uint16_t)(0xC170 + i), s_pw[i]);
    s_pw_draw = true;
    if (pw_log()) fprintf(stderr, "[gv] password screen: pre-filled with the saved password\n");
    return 1;                                               /* -> $8603 */
}

/* [insn_hooks] gv_pw_loop at the password screen's input loop head (bank 7 $8620): after a
   pre-fill, show the 32 symbols once with the game's own routine $87A7 (draws the symbol of the
   buffer at cursor position $C16F, as after typing one), cursor back to position 0 */
int gv_pw_loop(void)
{
    if (!s_pw_draw) return 0;
    s_pw_draw = false;
    Z80State keep = g_z80;
    for (int i = 0; i < 32; i++) {
        sms_write8(0xC16F, (uint8_t)i);
        g_z80.sp = (uint16_t)(g_z80.sp - 2); sms_write16(g_z80.sp, 0x0000);   /* popped by its ret */
        call_by_address(0x87A7);
    }
    sms_write8(0xC16F, 0);
    uint64_t cyc = g_z80.cyc; g_z80 = keep; g_z80.cyc = cyc;
    return 0;
}

/* test: GV_TEST_GENPW=<frame> runs the game's password generator (bank 7 $83D6: state -> $C1B0.., jp $852B) */
void gv_on_frame_post(uint64_t frame)
{
    if (frame % 30 == 0) music_names();              /* THEME n (LOCKED) follows the equipment */
    tp_place_tick();
    gv_panel_test(frame);
    {   static long tf = -2; static int ts;              /* test: GV_TP_TEST=<frame>:<screen> = a teleport request */
        if (tf == -2) { const char *e = getenv("GV_TP_TEST"); tf = -1; if (e) sscanf(e, "%ld:%d", &tf, &ts); }
        if (tf >= 0 && frame == (uint64_t)tf) s_tp_req = ts; }
    {   static long mf = -2; static int mv;              /* test: GV_MUSIC_TEST=<frame>:<option 0..5> */
        if (mf == -2) { const char *e = getenv("GV_MUSIC_TEST"); mf = -1; if (e) sscanf(e, "%ld:%d", &mf, &mv); }
        if (mf >= 0 && frame == (uint64_t)mf) { s_music_i = mv; s_music_dirty = true; } }
    {   /* test: GV_HUD_OPEN=<frame> opens the HUD (window tests: SMS_WINDOW_SHOT + _EXIT) */
        static long ho = -2;
        if (ho == -2) { const char *e = getenv("GV_HUD_OPEN"); ho = e ? atol(e) : -1; }
        if (ho >= 0 && frame == (uint64_t)ho && !hud_is_open()) hud_key(HUD_KEY_TOGGLE);
    }
    static long at = -2;
    if (at == -2) { const char *e = getenv("GV_TEST_GENPW"); at = e ? atol(e) : -1; }
    if (at < 0 || frame != (uint64_t)at) return;
    Z80State keep = g_z80;
    int old = sms_slot_bank(0x8000);
    g_z80.iff1 = g_z80.iff2 = 0;                  /* no VBlank in the middle of our call */
    sms_write8(0xFFFF, 7);
    g_z80.sp = (uint16_t)(g_z80.sp - 2); sms_write16(g_z80.sp, 0x0000);
    call_by_address(0x83D6);
    sms_write8(0xFFFF, (uint8_t)old);
    uint64_t cyc = g_z80.cyc; g_z80 = keep; g_z80.cyc = cyc;
}

void gv_on_post_reset(void)
{
    size_t n = 0; uint8_t *rom = glue_rom_image(&n);
    s_rom_ok = rom && n >= ROM_CRC_SIZE;
    for (int i = 0; i < 2 && s_rom_ok; i++)           /* original, or already fixed by an earlier reset */
        s_rom_ok = rom[k_map_fix[i].off] == k_map_fix[i].orig || rom[k_map_fix[i].off] == k_map_fix[i].fixed;
    if (!s_rom_ok) fprintf(stderr, "[gv_qol] room table bytes not as expected: MAP FIXES disabled\n");
    load_config();
    pw_load();
    {   const char *t = getenv("GV_PASSWORD");            /* test: 64 hex digits = a password to pre-fill */
        if (t && strlen(t) >= 64) { for (int i = 0; i < 32; i++) { unsigned v; sscanf(t + 2 * i, "%2x", &v); s_pw[i] = (uint8_t)v; } s_pw_have = true; } }
    apply_map_fix();
    {   static bool patches_done;                     /* once: they remember the original bytes */
        if (!patches_done) { patches_done = true; gv_patch_init(&s_cave); gv_patch_init(&s_free); } }
    {   GvPatch *const pp[] = { &s_cave, &s_free }; int *const pv[] = { &s_cave_i, &s_free_i };
        for (int k = 0; k < 2; k++) {
            if (!pp[k]->avail) {                      /* file missing: the option stays off */
                *pv[k] = 0;
                for (unsigned i = 0; i < sizeof k_opts / sizeof k_opts[0]; i++)
                    if (k_opts[i].value == pv[k]) { k_opts[i].choices = k_missing; k_opts[i].nchoices = 1; }
            }
            gv_patch_set(pp[k], *pv[k] != 0);
        } }
    hud_set_turbo_speed(k_ff_vals[s_ff_i]);
    hud_set_title("GOLVELLIUS - OPTIONS");
    hud_set_options(k_opts, (int)(sizeof k_opts / sizeof k_opts[0]));
    hud_set_on_close(on_close);
    gv_map_init();
    {   static bool pages;
        if (!pages) { pages = true;
            hud_add_option_page("MULTIPLIERS", k_mult_opts, (int)(sizeof k_mult_opts / sizeof k_mult_opts[0]));
            hud_add_option_page("SOUND", k_sound_opts, (int)(sizeof k_sound_opts / sizeof k_sound_opts[0])); } }
    {   static const char *const order[] = { "MAP", "OPTIONS", "MULTIPLIERS", "SOUND" };
        hud_set_page_order(order, 4); }
    find_pw_caves();
    music_names();
    hud_set_open_page(open_page);
    hud_set_panel(gv_panel_draw);                     /* surroundings, gold, power (gv_panel.c) */
    hud_set_panel_enabled(s_panel_i != 0);
    hud_set_on_panel_toggle(on_panel_toggled);
    hud_set_back_pause(true);                         /* pad Back stays the console's Pause (user) */
}
