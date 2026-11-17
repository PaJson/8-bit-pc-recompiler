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
 *  TEXT FIXES (own, user decision 2026-11-16): the 5 certain typos of the English ROM, each the same
 *    length, found by their original bytes (no file needed): Winkle's "PASSWARD" (text $03), three
 *    times "GOLD" for "FIND" ($24 "DIDST THOU GOLD THE RING", $2B "GO GOLD MORE!", $2F "HELP THEE
 *    GOLD MANY UNUSUAL OBJECTS"), credits "DESGINED BY" (bank 15). Cave texts use $00 for a space, the
 *    credits '@'. Tools/gv_make_ips.py writes the same fixes (+ MAP FIXES) as an IPS for other emulators.
 *  SOUND CHIP PSG (original: Western consoles have no FM unit) / FM (the Japanese FM Sound Unit). The
 *    boot code ($00C3) detects the unit through port $F2 and keeps the result in $C004 bit 0; the
 *    driver init $770D (A = $80 FM / 0 PSG, from $040F) stores it in $C646 and initialises the chip.
 *    The engine's unit is attached before the boot (gv_on_post_reset); a change in the HUD is done in
 *    the driver's frame update (gv_snd_update at $6DEF, frame interrupt, bank 2 mapped): attach /
 *    detach, $C004, $770D again, the playing song (structure 0) restarted through $77B6.
 *    Each sound has an FM and a PSG version with their own flags: e.g. the big enemies' death sound
 *    $35 protects the effect channel in FM only, so pickups are not heard for ~2 s (CLAUDE.md).
 *  GOLVELLIUS PLUS (our ROM patch, found by its ID block at bank 16 $8000): the ROM does TEXT FIXES, MAP
 *    FIXES, SWORD HIT FIX, SWORD REACH, CAVE DOWN SPEED, KEEP GOLD and the multipliers itself (its title ->
 *    OPTIONS page); the HUD shows those 9 instead of ours, read from / written to the ROM's own storage, and
 *    our versions (ROM-image patches, hooks, ini keys) are off. Its save slots replace PASSWORD SAVE SLOTS;
 *    gv_slots.c makes the maps follow them. Test env: GV_ROPT_SET=<frame>:<option>=<value>,...,
 *    GV_HUD_PAGE=<tab> (the tab the HUD opens on).
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
#include "audio/sn76489.h"
#include "audio/ym2413.h"
#include "video/sms_vdp.h"

extern uint8_t *glue_rom_image(size_t *size);
extern const uint8_t *glue_sram_image(size_t *size);
extern void glue_sram_poke(size_t off, uint8_t v);
extern void glue_sram_flush(void);

#define ROM_CRC_SIZE 0x40000
static const struct { uint32_t off; uint8_t orig, fixed; } k_map_fix[2] = {
    { 0x15CB9, 0x8E, 0x7E },
    { 0x15E16, 0x94, 0x2B },
};

static const char *const k_onoff[] = { "OFF (ORIGINAL)", "ON" };
static const double k_ff_vals[] = { 0.5, 1, 2, 4 };
static const char *const k_ff_names[] = { "0.5X (SLOW)", "1X", "2X", "4X (DEFAULT)" };
static int s_mapfix_i, s_hitfix_i, s_ff_i = 3, s_pw_i, s_tfix_i;
/* MULTIPLIERS tab (x0..x10, index = factor; 1 = original), DINA, TELEPORT, OVERWORLD MUSIC */
static int s_bgm_i = 100, s_sfx_i = 100;            /* SOUND tab: music / effects volume, percent */
static int s_fm_i;                                    /* SOUND CHIP: 0 PSG (original), 1 FM */
static int s_dealt_i = 1, s_recv_i = 1, s_gold_i = 1, s_dina_i, s_tp_i, s_music_i, s_panel_i, s_reach_i, s_down_i;
extern int gv_cave_reveal;                         /* gv_cave.c: DUNGEON MAP option */
extern int gv_cave_info_all;                       /* gv_info.c: CAVE INFO option */
static const char *const k_cinfo[] = { "ENTERED CAVES", "ALL CAVES (SPOILER)" };
static const char *const k_dmap[] = { "EXPLORED PART", "WHOLE DUNGEON" };
static const char *const k_reach[] = { "ORIGINAL", "+4 PIXELS", "+8 PIXELS" };
static const char *const k_mult[] = { "0X", "1X (ORIGINAL)", "2X", "3X", "4X", "5X", "6X", "7X", "8X", "9X", "10X" };
static const char *const k_dina[] = { "150 GOLD (ORIGINAL)", "2600 GOLD (ALIANGER)" };
static const char *const k_downspd[] = { "X1 (ORIGINAL)", "X1.5 (ALIANGER)" };
static const char *const k_tp[]   = { "OFF (ORIGINAL)", "VISITED SCREENS", "PASSWORD CAVES" };
/* Golvellius Plus (our ROM patch, Patches/Golvellius): its own OPTIONS page (title -> OPTIONS) does
   TEXT FIXES, MAP FIXES, SWORD HIT FIX, SWORD REACH, CAVE DOWN SPEED, KEEP GOLD, the three multipliers
   and OVERWORLD SPEED in the ROM. The HUD shows those same 10 options instead of our C versions: read
   from the ROM's RAM (OptVals $C0D1, once OptLoaded $C0F5) or its battery RAM ($8010 + check byte $801A
   = sum xor $5A; a block saved by an older patch = 9 options + check $8019, then OVERWORLD SPEED 1X),
   written to both, so the HUD and the ROM's page are one setting. The ROM's save slots replace the
   PASSWORD SAVE SLOTS (gv_slots.c makes the maps follow the ROM's slots). */
static bool s_plus;
#define PLUS_OPTS    0xC0D1
#define PLUS_LOADED  0xC0F5
#define PLUS_SRAM    0x10
#define PLUS_N       10
static int s_ropt[PLUS_N];                         /* text, map, sword, reach, cave, keep gold, gold, dealt, recv, walk */
static const uint8_t k_ropt_def[PLUS_N] = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 0 }, k_ropt_cnt[PLUS_N] = { 2, 2, 2, 3, 3, 2, 11, 11, 11, 3 };
static const char *const k_r_reach[] = { "OFF (ORIGINAL)", "4 PIXELS", "8 PIXELS" };
static const char *const k_r_cave[]  = { "1X (ORIGINAL)", "1.5X", "2X" };
static const char *const k_r_walk[]  = { "1X (ORIGINAL)", "1.5X", "2X" };
static bool plus_rom(void)
{
    size_t n = 0; const uint8_t *rom = glue_rom_image(&n);
    return rom && n >= 0x40012 && !memcmp(rom + 0x40000, "GOLVELLIUS PLUS\0", 16) && rom[0x40010] == 1
        && rom[0x40011] >= 5;                         /* level 5: 10 options (an older patch has a 9-option layout) */
}
/* the ROM's current values -> s_ropt (RAM once loaded, else the battery RAM, else the defaults) */
static void ropt_read(void)
{
    uint8_t v[PLUS_N];
    if (sms_read8(PLUS_LOADED) == 1) for (int i = 0; i < PLUS_N; i++) v[i] = sms_read8((uint16_t)(PLUS_OPTS + i));
    else {
        size_t n = 0; const uint8_t *sr = glue_sram_image(&n);
        int have = 0;                                  /* as OptLoad: 10 options, else an older block of 9 */
        for (int k = PLUS_N; k >= PLUS_N - 1 && !have; k--) {
            uint8_t sum = 0; bool ok = sr && n > (size_t)(PLUS_SRAM + k);
            for (int i = 0; i < k && ok; i++) sum = (uint8_t)(sum + sr[PLUS_SRAM + i]);
            if (ok && (uint8_t)(sum ^ 0x5A) == sr[PLUS_SRAM + k]) have = k;
        }
        for (int i = 0; i < PLUS_N; i++) v[i] = i < have ? sr[PLUS_SRAM + i] : k_ropt_def[i];
    }
    for (int i = 0; i < PLUS_N; i++) s_ropt[i] = v[i] < k_ropt_cnt[i] ? v[i] : k_ropt_def[i];   /* out of range: as OptLoad */
}
static bool s_ropt_dirty;                          /* (on_close writes the .sav) */
static void ropt_changed(void)                    /* a HUD change -> the ROM's RAM + battery RAM (as OptSave) */
{
    uint8_t sum = 0;
    for (int i = 0; i < PLUS_N; i++) {
        uint8_t v = (uint8_t)s_ropt[i];
        sms_write8((uint16_t)(PLUS_OPTS + i), v);          /* in force at once (before OptLoad: it reads the battery RAM) */
        glue_sram_poke(PLUS_SRAM + i, v);
        sum = (uint8_t)(sum + v);
    }
    glue_sram_poke(PLUS_SRAM + PLUS_N, (uint8_t)(sum ^ 0x5A));
    s_ropt_dirty = true;
}
static char s_music_names[6][24] = { "AUTO (ORIGINAL)" };
static const char *k_music[6] = { s_music_names[0], s_music_names[1], s_music_names[2], s_music_names[3], s_music_names[4], s_music_names[5] };
/* TEXT FIXES: see the header; same table in Tools/gv_make_ips.py */
static const struct { uint32_t lo, hi; uint8_t space; const char *from, *to; } k_typo[] = {
    { 0x1C000, 0x20000, 0x00, "PASSWARD",       "PASSWORD" },        /* text $03, Winkle */
    { 0x1C000, 0x20000, 0x00, "THOU GOLD THE",  "THOU FIND THE" },   /* $24 */
    { 0x1C000, 0x20000, 0x00, "GO GOLD MORE",   "GO FIND MORE" },    /* $2B, sword seller */
    { 0x1C000, 0x20000, 0x00, "HELP THEE GOLD", "HELP THEE FIND" },  /* $2F, rock ring seller */
    { 0x3C000, 0x40000, 0x40, "DESGINED",       "DESIGNED" },        /* credits */
};
#define NTYPO (int)(sizeof k_typo / sizeof k_typo[0])
static long s_typo_at[NTYPO];
static bool s_typo_ok;
static const char *const k_notfound[] = { "NOT FOUND IN THIS ROM" };
static void enc(const char *t, uint8_t space, uint8_t *out) { for (size_t i = 0; t[i]; i++) out[i] = t[i] == ' ' ? space : (uint8_t)t[i]; }
static long find_unique(const uint8_t *rom, uint32_t lo, uint32_t hi, const uint8_t *p, size_t n)
{
    long at = -1;
    for (uint32_t o = lo; o + n <= hi; o++)
        if (!memcmp(rom + o, p, n)) { if (at >= 0) return -2; at = (long)o; }
    return at;
}
/* every typo must be found exactly once; searched once, in the untouched image of the first start (the
   fixed words may exist elsewhere: "PASSWORD" is also on the password screen) */
static void typo_init(void)
{
    static bool done; if (done) return; done = true;
    size_t n = 0; const uint8_t *rom = glue_rom_image(&n);
    s_typo_ok = rom && n >= 0x40000;
    for (int i = 0; i < NTYPO && s_typo_ok; i++) {
        uint8_t a[32]; size_t len = strlen(k_typo[i].from);
        enc(k_typo[i].from, k_typo[i].space, a);
        s_typo_at[i] = find_unique(rom, k_typo[i].lo, k_typo[i].hi, a, len);
        if (s_typo_at[i] < 0) { s_typo_ok = false; fprintf(stderr, "[gv_qol] text fix \"%s\" not found once: TEXT FIXES disabled\n", k_typo[i].from); }
    }
}
static void apply_typos(void)
{
    size_t n = 0; uint8_t *rom = glue_rom_image(&n);
    if (!s_typo_ok || !rom) return;
    for (int i = 0; i < NTYPO; i++) enc(s_tfix_i ? k_typo[i].to : k_typo[i].from, k_typo[i].space, rom + s_typo_at[i]);
}
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
        else if (!strcmp(key, "text_fixes")) s_tfix_i = v != 0;
        else if (!strcmp(key, "damage_dealt")) s_dealt_i = v < 0 ? 0 : v > 10 ? 10 : (int)v;
        else if (!strcmp(key, "damage_received")) s_recv_i = v < 0 ? 0 : v > 10 ? 10 : (int)v;
        else if (!strcmp(key, "gold_multiplier")) s_gold_i = v < 0 ? 0 : v > 10 ? 10 : (int)v;
        else if (!strcmp(key, "dina_2600")) s_dina_i = v != 0;
        else if (!strcmp(key, "side_panel")) s_panel_i = v != 0;
        else if (!strcmp(key, "dungeon_map_whole")) gv_cave_reveal = v != 0;
        else if (!strcmp(key, "cave_info_all")) gv_cave_info_all = v != 0;
        else if (!strcmp(key, "teleport")) s_tp_i = v < 0 ? 0 : v > 2 ? 2 : (int)v;
        else if (!strcmp(key, "sword_reach")) s_reach_i = v < 0 ? 0 : v > 2 ? 2 : (int)v;
        else if (!strcmp(key, "cave_down_fast")) s_down_i = v != 0;
        else if (!strcmp(key, "overworld_music")) s_music_i = v < 0 ? 0 : v > 5 ? 5 : (int)v;
        else if (!strcmp(key, "fm_sound")) s_fm_i = v != 0;
        else if (!strcmp(key, "music_volume")) s_bgm_i = v < 0 ? 0 : v > 100 ? 100 : (int)(v + 0.5);
        else if (!strcmp(key, "sfx_volume")) s_sfx_i = v < 0 ? 0 : v > 100 ? 100 : (int)(v + 0.5);
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
    if (s_plus) fprintf(f, "; Golvellius Plus: TEXT FIXES, MAP FIXES, SWORD HIT FIX, SWORD REACH, CAVE DOWN SPEED, KEEP GOLD and the\n"
                       ";   multipliers are the ROM's own options (title -> OPTIONS, or the HUD): kept in the battery save\n");
    if (!s_plus) fprintf(f, "map_fixes      = %d     ; 1 = screen C5 gets its own room, screen H5 the right cave graphic (SMS Power bug fix)\n", s_mapfix_i);
    if (!s_plus) fprintf(f, "sword_hit_fix  = %d     ; 1 = every sword swing can hit (original: every other swing only stuns)\n", s_hitfix_i);
    if (!s_plus) fprintf(f, "remember_password = %d  ; 1 = passwords are saved with their maps: slot 0 = autosave, 1-5 = LB/RB in Winkle's cave and on the password screen\n", s_pw_i);
    if (!s_plus) fprintf(f, "text_fixes     = %d     ; 1 = the 5 typos fixed: PASSWARD, 3 x GOLD for FIND, DESGINED (credits)\n", s_tfix_i);
    fprintf(f, "cave_info_all  = %d     ; MAP tab / panel: 0 = area names of visited screens, caves you have been inside, 1 = everything (spoiler)\n", gv_cave_info_all);
    if (!s_plus) fprintf(f, "damage_dealt   = %-5d ; sword damage to enemies x0..x10 (1 = original)\n", s_dealt_i);
    if (!s_plus) fprintf(f, "damage_received = %-4d ; damage you take x0..x10 (1 = original; healing is not changed)\n", s_recv_i);
    if (!s_plus) fprintf(f, "gold_multiplier = %-4d ; gold dropped by enemies x0..x10 (1 = original; GOLD MAX still caps it)\n", s_gold_i);
    fprintf(f, "side_panel     = %d     ; widescreen window with a side panel: the surrounding screens, gold, power (F2)\n", s_panel_i);
    fprintf(f, "dungeon_map_whole = %d  ; dungeon map (side panel / MAP tab): 0 = the parts you have seen, 1 = the whole dungeon\n", gv_cave_reveal);
    fprintf(f, "dina_2600      = %d    ; 1 = Dina pays 2600 gold instead of 150 (amount from Alianger's hack)\n", s_dina_i);
    fprintf(f, "teleport       = %d     ; MAP tab teleport: 0 off, 1 to visited screens, 2 to visited password caves\n", s_tp_i);
    if (!s_plus) fprintf(f, "sword_reach    = %d     ; sword hit area: 0 original, 1 = 4 pixels wider on every side, 2 = 8 pixels\n", s_reach_i);
    if (!s_plus) fprintf(f, "cave_down_fast = %d     ; 1 = in top-down caves the hero moves down 1.5x as fast (speed 3 instead of 2, Alianger's idea)\n", s_down_i);
    fprintf(f, "overworld_music = %d    ; 0 = the game's choice (by sword + shield), 1..5 = that theme once unlocked\n", s_music_i);
    fprintf(f, "fm_sound       = %d     ; sound chip: 0 = PSG (original: Western consoles have no FM unit), 1 = FM (Japanese FM Sound Unit)\n", s_fm_i);
    fprintf(f, "music_volume   = %-5d ; music volume in percent (0..100)\n", s_bgm_i);
    fprintf(f, "sfx_volume     = %-5d ; sound effects volume in percent (0..100)\n", s_sfx_i);
    fprintf(f, "fast_forward   = %-5g ; speed while TAB / pad RT is held: 0.5, 1, 2 or 4 (pad LT = x0.5)\n", k_ff_vals[s_ff_i]);
    safe_fclose(f, s_ini);
}

static void on_map_fix(void) { apply_map_fix(); }
static void on_typo(void) { apply_typos(); }
static void on_panel(void) { hud_set_panel_enabled(s_panel_i != 0); }
extern void gv_map_init(void);
extern void gv_panel_draw(uint32_t *b, int w, int h);
extern void gv_panel_test(uint64_t frame);
static void save_config(void);
static void on_panel_toggled(bool on) { s_panel_i = on; save_config(); }   /* F2 */
extern void gv_map_save(void);
extern void gv_map_hud_closed(void);
extern void gv_cave_save(void);
extern void gv_cave_frame(void);
extern void gv_cave_test(uint64_t frame);
static void on_close(void)
{
    save_config(); gv_map_save(); gv_cave_save(); gv_map_hud_closed();
    if (s_ropt_dirty) { s_ropt_dirty = false; glue_sram_flush(); }   /* Plus: the ROM's options into the .sav now */
}
static int open_page(void)
{
    const char *t = getenv("GV_HUD_PAGE");               /* test: the tab the HUD opens on (window shots) */
    return hud_page_index(t ? t : "MAP");
}
static void on_ff(void) { hud_set_turbo_speed(k_ff_vals[s_ff_i]); }
static HudOption k_opts[] = {          /* not const: TEXT FIXES gets k_notfound on another ROM */
    { "MAP FIXES",      k_onoff,    2, &s_mapfix_i, on_map_fix },
    { "SWORD HIT FIX",  k_onoff,    2, &s_hitfix_i, NULL },
    { "FAST FORWARD",   k_ff_names, 4, &s_ff_i,     on_ff },
    { "SIDE PANEL",     k_onoff,    2, &s_panel_i,  on_panel },
    { "DUNGEON MAP",    k_dmap,     2, &gv_cave_reveal, NULL },
    { "CAVE INFO",      k_cinfo,    2, &gv_cave_info_all, NULL },
    { "PASSWORD SAVE SLOTS", k_onoff, 2, &s_pw_i,   NULL },
    { "TEXT FIXES",     k_onoff,    2, &s_tfix_i,   on_typo },
    { "DINA'S PAYOUT",  k_dina,     2, &s_dina_i,   NULL },
    { "MAP TELEPORT",   k_tp,       3, &s_tp_i,     NULL },
    { "SWORD REACH",    k_reach,    3, &s_reach_i,  NULL },
    { "CAVE DOWN SPEED", k_downspd, 2, &s_down_i,   NULL },
};
static const HudOption k_mult_opts[] = {
    { "DAMAGE DEALT",    k_mult, 11, &s_dealt_i, NULL },
    { "DAMAGE RECEIVED", k_mult, 11, &s_recv_i,  NULL },
    { "GOLD DROPS",      k_mult, 11, &s_gold_i,  NULL },
};
/* Golvellius Plus: the ROM's 10 options first (its OPTIONS page order; MULTIPLIERS tab = its last three),
   then our PC-only options */
static HudOption k_opts_plus[] = {
    { "TEXT FIXES",     k_onoff,    2, &s_ropt[0],  ropt_changed },
    { "MAP FIXES",      k_onoff,    2, &s_ropt[1],  ropt_changed },
    { "SWORD HIT FIX",  k_onoff,    2, &s_ropt[2],  ropt_changed },
    { "SWORD REACH",    k_r_reach,  3, &s_ropt[3],  ropt_changed },
    { "CAVE DOWN SPEED", k_r_cave,  3, &s_ropt[4],  ropt_changed },
    { "OVERWORLD SPEED", k_r_walk,  3, &s_ropt[9],  ropt_changed },
    { "KEEP GOLD",      k_onoff,    2, &s_ropt[5],  ropt_changed },
    { "FAST FORWARD",   k_ff_names, 4, &s_ff_i,     on_ff },
    { "SIDE PANEL",     k_onoff,    2, &s_panel_i,  on_panel },
    { "DUNGEON MAP",    k_dmap,     2, &gv_cave_reveal, NULL },
    { "CAVE INFO",      k_cinfo,    2, &gv_cave_info_all, NULL },
    { "DINA'S PAYOUT",  k_dina,     2, &s_dina_i,   NULL },
    { "MAP TELEPORT",   k_tp,       3, &s_tp_i,     NULL },
};
static const HudOption k_mult_plus[] = {
    { "GOLD DROPS",      k_mult, 11, &s_ropt[6], ropt_changed },
    { "DAMAGE DEALT",    k_mult, 11, &s_ropt[7], ropt_changed },
    { "DAMAGE RECEIVED", k_mult, 11, &s_ropt[8], ropt_changed },
};
static bool s_music_dirty;
static void on_music(void) { s_music_dirty = true; }

/* MUSIC / SOUND EFFECTS volume (SOUND tab, like Phantasy Star's). The sound driver (bank 1, $6DF0..)
   runs 10 channel structures of $26 bytes at $C64B ($7891 table): +0 = 0 when idle, +6 = the output
   channel (set by a song command, $7248; read by the output stage $751C: FM channel 0..8 in FM mode,
   $C646 bit 7), +$14 = the sound number that owns it ($77E2). Sounds $01-$20 are the songs and
   jingles (4-5 tracks), $21 and up the effects (1-2 tracks). Every frame the outputs driven by an
   active effect get the effect volume, all others the music volume (host mixing only). */
static char s_pct_buf[101][6];
static const char *k_pct[101];
static unsigned vol_gain(int pct) { double g = pct / 100.0; return (unsigned)(g * g * 256.0 + 0.5); }  /* loudness-like curve */
static unsigned s_snd_frames;                                /* frames (sound_frame runs every frame) */
static void sound_frame(void)
{
    s_snd_frames++;
    bool fm = (sms_read8(0xC646) & 0x80) != 0, sfx_fm[9] = { false }, sfx_psg[4] = { false };
    for (int n = 0; n < 10; n++) {
        uint16_t b = (uint16_t)(0xC64B + 0x26 * n);
        if (!sms_read8(b) || sms_read8((uint16_t)(b + 0x14)) < 0x21) continue;
        int c = sms_read8((uint16_t)(b + 6));
        if (fm) { if (c < 9) sfx_fm[c] = true; } else sfx_psg[c & 3] = true;
    }
    unsigned gb = vol_gain(s_bgm_i), gs = vol_gain(s_sfx_i);
    for (unsigned i = 0; i < 4; i++) psg_set_channel_gain(i, sfx_psg[i] ? gs : gb);
    for (unsigned i = 0; i < 14; i++) fm_set_channel_gain(i, i < 9 && sfx_fm[i] ? gs : gb);
    {   /* test: GV_SND_LOG=1 prints every change of the effect channel set */
        static int on = -1; static unsigned last;
        if (on < 0) on = getenv("GV_SND_LOG") != NULL;
        unsigned m = 0;
        for (int i = 0; i < 4; i++) m |= (unsigned)sfx_psg[i] << i;
        for (int i = 0; i < 9; i++) m |= (unsigned)sfx_fm[i] << (8 + i);
        if (on > 0 && getenv("GV_SND_LOG")[0] == '2') {   /* GV_SND_LOG=2: every change of any structure */
            static uint8_t prev[10][3]; static unsigned fr;
            fr++;
            for (int n = 0; n < 10; n++) { uint16_t b = (uint16_t)(0xC64B + 0x26 * n);
                uint8_t v[3] = { sms_read8(b), sms_read8((uint16_t)(b + 0x14)), sms_read8((uint16_t)(b + 6)) };
                if (memcmp(v, prev[n], 3)) { fprintf(stderr, "[snd2] %u s%d act %02X snd %02X out %d (C802 %02X)\n", fr, n, v[0], v[1], v[2], sms_read8(0xC802)); memcpy(prev[n], v, 3); } }
            return;
        }
        if (on && m != last) {
            fprintf(stderr, "[snd] effects on psg %X fm %03X:", m & 15, m >> 8);
            for (int n = 0; n < 10; n++) { uint16_t b = (uint16_t)(0xC64B + 0x26 * n);
                if (sms_read8(b)) fprintf(stderr, " s%d=%02X>%d", n, sms_read8((uint16_t)(b + 0x14)), sms_read8((uint16_t)(b + 6))); }
            fprintf(stderr, "\n");
            last = m;
        }
    }
}
static void on_volume_changed(void) { sound_frame(); }
static const char *k_chip[] = { "PSG (ORIGINAL)", "FM" };
static HudOption k_sound_opts[] = {
    { "SOUND CHIP",      k_chip, 2,   &s_fm_i,    NULL },          /* switched in gv_snd_update */
    { "OVERWORLD MUSIC", k_music, 6, &s_music_i, on_music },
    { "MUSIC",           k_pct, 101, &s_bgm_i,   on_volume_changed },
    { "SOUND EFFECTS",   k_pct, 101, &s_sfx_i,   on_volume_changed },
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
/* [insn_hooks] $6DEF (the sound driver's frame update, called from the frame interrupt $079B with
   bank 2 mapped; observe only): SOUND CHIP switched in the HUD -> the game's own init for the other
   chip, then the playing song again (effects in progress are dropped). */
int gv_snd_update(void)
{
    bool want = s_fm_i != 0, cur = (sms_read8(0xC646) & 0x80) != 0;
    if (want == cur && fm_attached() == want) return 0;
    Z80State keep = g_z80;                                   /* our calls must not disturb the driver */
    uint8_t song = sms_read8(0xC64B) && sms_read8(0xC64B + 0x14) <= 0x20 ? sms_read8(0xC64B + 0x14) : 0;
    fm_set_attached(want);
    sms_write8(0xC004, (uint8_t)((sms_read8(0xC004) & 0xFE) | want));   /* as the boot code's result */
    g_z80.a = want ? 0x80 : 0x00;
    call_game(0x770D);                                       /* init: $C646, chip, all channels off */
    if (song) { g_z80.a = song; call_game(0x77B6); }         /* $77A6 without its di / ei */
    if (qlog()) fprintf(stderr, "[gv] sound chip -> %s, song %02X restarted\n", want ? "FM" : "PSG", song);
    uint64_t cyc = g_z80.cyc;
    g_z80 = keep;                                            /* registers (and IFF: $770D ends with ei) back */
    g_z80.cyc = cyc;
    return 0;
}
/* test: GV_SND_TEST=<frame>:<sound>,... starts each sound at the first game-context hook (main loop
   head, sword swing) on or after that frame */
static void snd_test_poll(void)
{
    static const char *e; static int init; static unsigned done;
    if (!init) { init = 1; e = getenv("GV_SND_TEST"); }
    unsigned i = 0;
    for (const char *p = e; p && *p; i++) {
        unsigned n = (unsigned)strtoul(p, (char **)&p, 10), s = 0;
        if (*p == ':') s = (unsigned)strtoul(p + 1, (char **)&p, 16);
        if (i < 32 && !(done >> i & 1) && s_snd_frames >= n) {
            done |= 1u << i;
            uint8_t a = g_z80.a;
            fprintf(stderr, "[snd] test: frame %u starts sound %02X\n", s_snd_frames, s);
            play_song((int)s);
            g_z80.a = a;
        }
        while (*p == ',') p++;
    }
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
    snd_test_poll();
    if (s_tp_req < 0 && !s_music_dirty) return 0;
    if (!on_overworld()) { if (sms_read8(0xC802) < 5) s_tp_req = -1; return 0; }   /* wait; drop it on the title */
    Z80State keep = g_z80;
    if (s_tp_req >= 0) {
        int to = s_tp_req; s_tp_req = -1;
        if (getenv("GV_LOG")) fprintf(stderr, "[gv] teleport %d -> %d\n", sms_read8(0xC80D), to);
        sms_write8(0xC802, 6);                               /* as the password start ($2023) */
        sms_write8(0xC80D, (uint8_t)to);
        /* $C84C = the extra tile set of screens with a layout id >= $53 (set when walking in, $1BC5;
           the full load $1978 -> $1BD8 only reads it): without this a teleport from a screen without
           it showed the target with wrong tiles (checked: A1, E3 from L14) */
        {   size_t n; const uint8_t *rom = glue_rom_image(&n);
            if (rom && n >= 0x16000) sms_write8(0xC84C, rom[0x15947 + rom[0x15890 + to] * 7] >= 0x53); }
        s_tp_place = 1;                                     /* the hero: when the display is back on */
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

/* ==== CAVE DOWN SPEED (Alianger's "faster downward speed in top-down view caves", our own code) ====
 * The top-down cave hero ($45E0.., object $C100) moves with `$4641 ld hl,$466B ; add hl,de ; ld a,(hl) ;
 * $4646 call $2F34`: E = the pad direction $C801 (0 DL, 1 L, 2 UL, 3 D, 5 U, 6 DR, 7 R, 8 UR; 4 = none is
 * handled before), $466B = the game's direction per input, $2F34 = velocity (ix+6..9) = the direction's
 * unit vector ($2F93) times the speed byte (ix+4) & $3F. The hero is created with speed 2 ($45CF).
 * Hook $4646 (observe): speed 3 for the three downward inputs, 2 for the others (his patch does the same
 * with a flag bit in the table: down = 1.5x; the boss arena, mode $0A, uses another handler there too). */
static bool s_down_set;
int gv_cave_down(void)
{
    if (s_down_i) {
        uint8_t d = sms_read8(0xC801);
        sms_write8(0xC104, (uint8_t)((d == 0 || d == 3 || d == 6) ? 3 : 2));
        s_down_set = true;
    } else if (s_down_set) { sms_write8(0xC104, 2); s_down_set = false; }   /* switched off: the original speed */
    return 0;
}

/* ==== SWORD REACH ==== */
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
    snd_test_poll();
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
extern void gv_slots_pw_made(void);
extern bool gv_slots_pw_fill(uint8_t pw[32]);
extern void gv_slots_pw_loop(void);
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
void gv_pw_reload(void) { pw_load(); }
void gv_pw_redraw(void) { s_pw_draw = true; }
bool gv_slots_enabled(void) { return s_pw_i != 0; }
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
    gv_slots_pw_made();
    return 0;
}
int gv_pw_entry(void)
{
    if (!s_pw_i && !(s_plus && getenv("GV_PASSWORD"))) return 0;   /* original: clear the buffer (test env: Plus too) */
    if (!(getenv("GV_PASSWORD") && s_pw_have) && !gv_slots_pw_fill(s_pw)) return 0;   /* test env first */
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
    if (s_pw_i) gv_slots_pw_loop();
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
/* test (Golvellius Plus research): GV_RAM_USE=<file> ORs the RAM $C000-$DFFF of every frame together and
   writes the 8 KB result at exit (merged with the file if it already exists): bytes that stay 0 over many
   different runs are candidates for the ROM patch's own variables */
static uint8_t s_ram_use[0x2000];
static const char *s_ram_use_path;
static void ram_use_write(void)
{
    FILE *f = fopen(s_ram_use_path, "rb");
    if (f) { uint8_t old[0x2000]; if (fread(old, 1, sizeof old, f) == sizeof old) for (int i = 0; i < 0x2000; i++) s_ram_use[i] |= old[i]; fclose(f); }
    f = fopen(s_ram_use_path, "wb");
    if (f) { fwrite(s_ram_use, 1, sizeof s_ram_use, f); fclose(f); }
}
static void ram_use_frame(void)
{
    static int on = -1;
    if (on < 0) { s_ram_use_path = getenv("GV_RAM_USE"); on = s_ram_use_path != NULL; if (on) atexit(ram_use_write); }
    if (on) for (int i = 0; i < 0x2000; i++) s_ram_use[i] |= sms_read8((uint16_t)(0xC000 + i));
}

/* test: GV_RAM_TRACE=<from>-<to>:<hex addr>+<len>,... prints those RAM bytes after every frame in the range */
static void ram_trace_frame(uint64_t frame)
{
    static const char *e = (const char *)1;
    static long lo, hi;
    static const char *list;
    if (e == (const char *)1) {
        e = getenv("GV_RAM_TRACE");
        if (e && sscanf(e, "%ld-%ld:", &lo, &hi) == 2) list = strchr(e, ':') + 1; else e = NULL;
    }
    if (!e || (long)frame < lo || (long)frame > hi) return;
    fprintf(stderr, "[trace] %5ld", (long)frame);
    for (const char *p = list; *p; ) {
        unsigned a = 0, n = 1;
        int used = 0;
        if (sscanf(p, "%x+%u%n", &a, &n, &used) < 1 || !used) {
            if (sscanf(p, "%x%n", &a, &used) < 1 || !used) break;
            n = 1;
        }
        fprintf(stderr, " %04X:", a);
        for (unsigned i = 0; i < n; i++) fprintf(stderr, "%02X", sms_read8((uint16_t)(a + i)));
        p += used;
        if (*p == ',') p++;
    }
    fputc('\n', stderr);
}

void gv_on_frame_post(uint64_t frame)
{
    ram_trace_frame(frame);
    if (s_plus && !hud_is_open()) ropt_read();       /* the HUD shows what the ROM's OPTIONS page set */
    if (s_plus) {   /* test: GV_ROPT_SET=<frame>:<option 0..8>=<value>,... = a HUD change of a ROM option */
        static const char *e = (const char *)1; if (e == (const char *)1) e = getenv("GV_ROPT_SET");
        for (const char *q = e; q && *q; ) {
            unsigned long at = strtoul(q, (char **)&q, 10); int i = -1, v = 0;
            if (*q == ':') { i = (int)strtol(q + 1, (char **)&q, 10); if (*q == '=') v = (int)strtol(q + 1, (char **)&q, 10); }
            if (at == frame && i >= 0 && i < PLUS_N && v >= 0 && v < k_ropt_cnt[i]) {
                s_ropt[i] = v; ropt_changed(); on_close();
                fprintf(stderr, "[gv] test: ROM option %d = %d (frame %lu)\n", i, v, at);
            }
            q = strchr(q, ','); if (q) q++;
        } }
    {   /* test: GV_FM_SWITCH=<frame>:<0|1>,... sets SOUND CHIP at that frame (as the HUD would) */
        static const char *e = (const char *)1; if (e == (const char *)1) e = getenv("GV_FM_SWITCH");
        for (const char *q = e; q && *q; ) {
            unsigned long at = strtoul(q, (char **)&q, 10); int v = *q == ':' ? atoi(q + 1) : 0;
            if (at == frame) s_fm_i = v != 0;
            q = strchr(q, ','); if (q) q++;
        } }
    if (frame % 30 == 0) music_names();              /* THEME n (LOCKED) follows the equipment */
    tp_place_tick();
    { extern void gv_slots_frame(uint64_t); gv_slots_frame(frame); }
    gv_cave_frame();
    gv_cave_test(frame);
    sound_frame();
    { extern void gv_info_test(uint64_t); gv_info_test(frame); }
    gv_panel_test(frame);
    ram_use_frame();
    {   /* test: GV_SPR_LOG=1 prints overworld frames whose end-of-frame sprite table has no sprite near
           the hero ($C101 / $C102) although the frame before had one (user: the hero vanishes for one
           frame when the top bar goes); the table's first 12 Y values show what changed */
        static int on = -1; if (on < 0) on = getenv("GV_SPR_LOG") != NULL;
        static int had;
        if (on && sms_read8(0xC802) == 6 && (g_vdp.reg[1] & 0x40)) {
            int sat = (g_vdp.reg[5] & 0x7E) << 7, hy = sms_read8(0xC101), hx = sms_read8(0xC102), n = 0, near = 0;
            for (int i = 0; i < 64; i++) {
                int y = g_vdp.vram[(sat + i) & 0x3FFF]; if (y == 0xD0) break;
                n++;
                int x = g_vdp.vram[(sat + 128 + 2 * i) & 0x3FFF];
                int dy = ((y + 1 - hy) & 255); if (dy > 127) dy -= 256;
                if (x + 8 > hx - 16 && x < hx + 16 && dy > -40 && dy < 24) near++;
            }
            if (had && !near) {
                fprintf(stderr, "[spr] frame %llu: no hero sprite (hero y %d x %d), %d sprites, Y:", (unsigned long long)frame, hy, hx, n);
                for (int i = 0; i < 12; i++) fprintf(stderr, " %d", g_vdp.vram[(sat + i) & 0x3FFF]);
                fprintf(stderr, "\n");
            }
            had = near > 0;
        } else had = 0;
    }
    {   static long tf = -2; static int ts;             /* test: GV_TP_TEST=<frame>:<screen> = a teleport request */
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
    s_plus = plus_rom();
    load_config();
    if (s_plus) {                                     /* the ROM does these (its OPTIONS page) and has its own save slots */
        s_mapfix_i = s_hitfix_i = s_tfix_i = s_reach_i = s_down_i = s_pw_i = 0;
        s_dealt_i = s_recv_i = s_gold_i = 1;
        ropt_read();
    }
    { extern void gv_slots_set_plus(bool on); gv_slots_set_plus(s_plus); }
    fm_set_attached(s_fm_i);                          /* before the boot code looks for the unit */
    pw_load();
    {   const char *t = getenv("GV_PASSWORD");            /* test: 64 hex digits = a password to pre-fill */
        if (t && strlen(t) >= 64) { for (int i = 0; i < 32; i++) { unsigned v; sscanf(t + 2 * i, "%2x", &v); s_pw[i] = (uint8_t)v; } s_pw_have = true; } }
    if (!s_plus) {                                    /* (Plus: the ROM fixes the map / texts itself) */
        apply_map_fix();
        typo_init();
        if (!s_typo_ok) {
            s_tfix_i = 0;
            for (unsigned i = 0; i < sizeof k_opts / sizeof k_opts[0]; i++)
                if (k_opts[i].value == &s_tfix_i) { k_opts[i].choices = k_notfound; k_opts[i].nchoices = 1; }
        }
        apply_typos();
    }
    hud_set_turbo_speed(k_ff_vals[s_ff_i]);
    hud_set_title("GOLVELLIUS - OPTIONS");
    if (s_plus) hud_set_options(k_opts_plus, (int)(sizeof k_opts_plus / sizeof k_opts_plus[0]));
    else hud_set_options(k_opts, (int)(sizeof k_opts / sizeof k_opts[0]));
    hud_set_on_close(on_close);
    gv_map_init();
    { static bool once; if (!once) { once = true; atexit(gv_cave_save);
        extern void gv_slots_init(void); extern void gv_slots_exit(void); gv_slots_init(); atexit(gv_slots_exit); } }
    {   static bool pages;
        if (!pages) { pages = true;
            if (s_plus) hud_add_option_page("MULTIPLIERS", k_mult_plus, (int)(sizeof k_mult_plus / sizeof k_mult_plus[0]));
            else hud_add_option_page("MULTIPLIERS", k_mult_opts, (int)(sizeof k_mult_opts / sizeof k_mult_opts[0]));
            for (int i = 0; i <= 100; i++) { snprintf(s_pct_buf[i], sizeof s_pct_buf[i], "%d%%", i); k_pct[i] = s_pct_buf[i]; }
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
