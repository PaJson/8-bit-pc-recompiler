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
 *    stunned / hit, low bits = countdown (LABEL $4520). The three hit spots `ld a,4 ; ld ($C120),a`
 *    ($C120 is probably the sound-effect byte: the hit sound) at $3F45, $43AC, $4678 get an observing
 *    [insn_hooks] hook that clears +$1F of the 13 objects at $C160 + 32*n ($C17F + 32*n), exactly
 *    what that IPS's new routine ($90B9 in bank 12) does at the same three spots. Not yet confirmed
 *    by playing: 30000 frames of random input (71 hits) never found a stale flag at those spots.
 *  FAST FORWARD: speed while TAB / pad RT is held (engine hud_set_turbo_speed; LT = x0.5).
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "hud.h"
#include "sms_runtime.h"

extern uint8_t *glue_rom_image(size_t *size);

#define ROM_CRC_SIZE 0x40000
static const struct { uint32_t off; uint8_t orig, fixed; } k_map_fix[2] = {
    { 0x15CB9, 0x8E, 0x7E },
    { 0x15E16, 0x94, 0x2B },
};

static const char *const k_onoff[] = { "OFF (ORIGINAL)", "ON" };
static const double k_ff_vals[] = { 0.5, 1, 2, 4 };
static const char *const k_ff_names[] = { "X0.5 (SLOW)", "X1", "X2", "X4 (DEFAULT)" };
static int s_mapfix_i, s_hitfix_i, s_ff_i = 3;
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
        else if (!strcmp(key, "fast_forward") && v > 0) {
            s_ff_i = 0; for (int i = 1; i < 4; i++) if (fabs(k_ff_vals[i] - v) < fabs(k_ff_vals[s_ff_i] - v)) s_ff_i = i;
        }
    }
    fclose(f);
}
static void save_config(void)
{
    FILE *f = fopen(s_ini, "w"); if (!f) return;
    fprintf(f, "; Golvellius options (also in game: F1 / gamepad Start). 0 = original game.\n");
    fprintf(f, "map_fixes      = %d     ; 1 = screen C5 gets its own room, screen H5 the right cave graphic (SMS Power bug fix)\n", s_mapfix_i);
    fprintf(f, "sword_hit_fix  = %d     ; 1 = every sword swing can hit (original: every other swing only stuns)\n", s_hitfix_i);
    fprintf(f, "fast_forward   = %-5g ; speed while TAB / pad RT is held: 0.5, 1, 2 or 4 (pad LT = x0.5)\n", k_ff_vals[s_ff_i]);
    fclose(f);
}

static void on_map_fix(void) { apply_map_fix(); }
static void on_ff(void) { hud_set_turbo_speed(k_ff_vals[s_ff_i]); }
static const HudOption k_opts[] = {
    { "MAP FIXES",      k_onoff,    2, &s_mapfix_i, on_map_fix },
    { "SWORD HIT FIX",  k_onoff,    2, &s_hitfix_i, NULL },
    { "FAST FORWARD",   k_ff_names, 4, &s_ff_i,     on_ff },
};

/* [insn_hooks] gv_sword_swing at the three hit spots `ld a,4 ; ld ($C120),a`: observe only */
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

void gv_on_post_reset(void)
{
    size_t n = 0; uint8_t *rom = glue_rom_image(&n);
    s_rom_ok = rom && n >= ROM_CRC_SIZE;
    for (int i = 0; i < 2 && s_rom_ok; i++)           /* original, or already fixed by an earlier reset */
        s_rom_ok = rom[k_map_fix[i].off] == k_map_fix[i].orig || rom[k_map_fix[i].off] == k_map_fix[i].fixed;
    if (!s_rom_ok) fprintf(stderr, "[gv_qol] room table bytes not as expected: MAP FIXES disabled\n");
    load_config();
    apply_map_fix();
    hud_set_turbo_speed(k_ff_vals[s_ff_i]);
    hud_set_title("GOLVELLIUS - OPTIONS");
    hud_set_options(k_opts, (int)(sizeof k_opts / sizeof k_opts[0]));
    hud_set_on_close(save_config);
}
