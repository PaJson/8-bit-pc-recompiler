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
static int s_mapfix_i, s_hitfix_i, s_ff_i = 3, s_pw_i;
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
    fprintf(f, "remember_password = %d  ; 1 = the last password shown is saved and pre-filled on the password screen\n", s_pw_i);
    fprintf(f, "fast_forward   = %-5g ; speed while TAB / pad RT is held: 0.5, 1, 2 or 4 (pad LT = x0.5)\n", k_ff_vals[s_ff_i]);
    fclose(f);
}

static void on_map_fix(void) { apply_map_fix(); }
static void on_ff(void) { hud_set_turbo_speed(k_ff_vals[s_ff_i]); }
static const HudOption k_opts[] = {
    { "MAP FIXES",      k_onoff,    2, &s_mapfix_i, on_map_fix },
    { "SWORD HIT FIX",  k_onoff,    2, &s_hitfix_i, NULL },
    { "FAST FORWARD",   k_ff_names, 4, &s_ff_i,     on_ff },
    { "REMEMBER PASSWORD", k_onoff, 2, &s_pw_i,     NULL },
};

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
}
static bool pw_log(void) { static int on = -1; if (on < 0) on = getenv("GV_LOG") != NULL; return on; }
int gv_pw_made(void)
{
    for (int i = 0; i < 32; i++) s_pw[i] = sms_read8((uint16_t)(0xC170 + i));
    s_pw_have = true;
    if (pw_log()) { fprintf(stderr, "[gv] password made:"); for (int i = 0; i < 32; i++) fprintf(stderr, " %02X", s_pw[i]); fprintf(stderr, "\n"); }
    if (!s_pw_i || getenv("SMS_NO_SAVE")) return 0;
    char p[1100]; pw_path(p, sizeof p);
    FILE *f = fopen(p, "wb");
    if (f) { fwrite(s_pw, 1, 32, f); fclose(f); }
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
    hud_set_turbo_speed(k_ff_vals[s_ff_i]);
    hud_set_title("GOLVELLIUS - OPTIONS");
    hud_set_options(k_opts, (int)(sizeof k_opts / sizeof k_opts[0]));
    hud_set_on_close(save_config);
}
