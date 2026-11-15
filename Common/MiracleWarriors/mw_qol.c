/*
 * mw_qol.c - Miracle Warriors options (HUD: F1 / gamepad Start; mw_qol.ini in the game folder or
 * $MW_QOL_INI). Every option defaults to the original game.
 *
 *  OPTIONS tab: FAST FORWARD = speed while TAB / pad RT is held (engine hud_set_turbo_speed; LT = x0.5).
 *  SOUND tab  : MUSIC / SOUND EFFECTS volume (host mixing only, the game's sound data is untouched).
 *
 * Sound driver (bank 15; the same 32-byte channel slot scheme as Phantasy Star's Sega driver, found
 * 2026-11-15 with SMS_PSGLOG_IX): slots at $DD0E + $20 n; +0 bit 7 = playing ($C0 = the rhythm
 * slot), +1 = the output: PSG $80 / $A0 / $C0 / $E0 (tone 0-2, noise) or FM $10 + n (FM channel n).
 * Music: $DD0E..$DD8E = FM 0-4, $DDAE = rhythm; sound effects: the three slots $DDCE, $DDEE, $DE0E
 * (random play: effects in $DE0E on FM 5, sometimes 3 / 4). The game plays FM when the FM unit
 * answers on port $F2 (the runner always reports it). Every frame the outputs used by a playing
 * effect slot get the effect volume, all others the music volume.
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

static const double k_ff_vals[] = { 0.5, 1, 2, 4 };
static const char *const k_ff_names[] = { "X0.5 (SLOW)", "X1", "X2", "X4 (DEFAULT)" };
static int s_ff_i = 3, s_bgm_i = 100, s_sfx_i = 100;
static char s_pct_buf[101][6];
static const char *k_pct[101];

static const char *s_ini = "mw_qol.ini";
static void load_config(void)
{
    const char *e = getenv("MW_QOL_INI"); if (e) s_ini = e;
    FILE *f = fopen(s_ini, "r"); if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char key[64]; double v;
        if (line[0] == ';' || line[0] == '#' || sscanf(line, " %63[a-z_0-9] = %lf", key, &v) != 2) continue;
        if (!strcmp(key, "music_volume")) s_bgm_i = v < 0 ? 0 : v > 100 ? 100 : (int)(v + 0.5);
        else if (!strcmp(key, "sfx_volume")) s_sfx_i = v < 0 ? 0 : v > 100 ? 100 : (int)(v + 0.5);
        else if (!strcmp(key, "fast_forward") && v > 0) {
            s_ff_i = 0; for (int i = 1; i < 4; i++) if (fabs(k_ff_vals[i] - v) < fabs(k_ff_vals[s_ff_i] - v)) s_ff_i = i;
        }
    }
    fclose(f);
}
static void save_config(void)
{
    if (getenv("SMS_NO_SAVE")) return;
    FILE *f = safe_fopen_w(s_ini, "w"); if (!f) return;   /* "<ini>.tmp", swapped in when complete */
    fprintf(f, "; Miracle Warriors options (also in game: F1 / gamepad Start).\n");
    fprintf(f, "music_volume   = %-5d ; music volume in percent (0..100)\n", s_bgm_i);
    fprintf(f, "sfx_volume     = %-5d ; sound effects volume in percent (0..100)\n", s_sfx_i);
    fprintf(f, "fast_forward   = %-5g ; speed while TAB / pad RT is held: 0.5, 1, 2 or 4 (pad LT = x0.5)\n", k_ff_vals[s_ff_i]);
    safe_fclose(f, s_ini);
}

static unsigned vol_gain(int pct) { double g = pct / 100.0; return (unsigned)(g * g * 256.0 + 0.5); }  /* loudness-like curve */
static void sound_frame(void)
{
    static const uint16_t slots[3] = { 0xDDCE, 0xDDEE, 0xDE0E };
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
    {   /* test: MW_SND_LOG=1 prints every change of the effect channel set */
        static int on = -1; static unsigned last;
        if (on < 0) on = getenv("MW_SND_LOG") != NULL;
        unsigned m = 0;
        for (int i = 0; i < 4; i++) m |= (unsigned)sfx_psg[i] << i;
        for (int i = 0; i < 9; i++) m |= (unsigned)sfx_fm[i] << (8 + i);
        if (on && m != last) { fprintf(stderr, "[snd] effects on psg %X fm %03X\n", m & 15, m >> 8); last = m; }
    }
}
static void on_volume_changed(void) { sound_frame(); }
static void on_ff(void) { hud_set_turbo_speed(k_ff_vals[s_ff_i]); }
static void on_close(void) { save_config(); }

static const HudOption k_opts[] = {
    { "FAST FORWARD", k_ff_names, 4, &s_ff_i, on_ff },
};
static HudOption k_sound_opts[] = {
    { "MUSIC",         k_pct, 101, &s_bgm_i, on_volume_changed },
    { "SOUND EFFECTS", k_pct, 101, &s_sfx_i, on_volume_changed },
};

void mw_on_post_reset(void)
{
    load_config();
    hud_set_turbo_speed(k_ff_vals[s_ff_i]);
    hud_set_title("MIRACLE WARRIORS - OPTIONS");
    hud_set_options(k_opts, (int)(sizeof k_opts / sizeof k_opts[0]));
    hud_set_on_close(on_close);
    {   static bool pages;
        if (!pages) { pages = true;
            for (int i = 0; i <= 100; i++) { snprintf(s_pct_buf[i], sizeof s_pct_buf[i], "%d%%", i); k_pct[i] = s_pct_buf[i]; }
            hud_add_option_page("SOUND", k_sound_opts, (int)(sizeof k_sound_opts / sizeof k_sound_opts[0])); } }
    {   static const char *const order[] = { "OPTIONS", "SOUND" };
        hud_set_page_order(order, 2); }
}
void mw_on_frame_post(uint64_t frame)
{
    (void)frame;
    sound_frame();
}
