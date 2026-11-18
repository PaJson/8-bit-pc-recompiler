/*
 * mw_qol.c - Miracle Warriors options (HUD: F1 / gamepad Start; mw_qol.ini in the game folder or
 * $MW_QOL_INI). Every option defaults to the original game.
 *
 *  OPTIONS tab: FAST FORWARD = speed while TAB / pad RT is held (engine hud_set_turbo_speed; LT = x0.5).
 *  MULTIPLIERS: EXP (the level bar, per hit) / GUILDERS / FANGS / CHARACTER POINTS x0..x100 (x1 = original).
 *  SOUND tab  : MUSIC / SOUND EFFECTS volume (host mixing only, the game's sound data is untouched),
 *               SOUND CHIP PSG (original: Western consoles have no FM unit) / FM (Japanese FM unit).
 *
 * SOUND CHIP: the boot code ($3D00) detects the unit through port $F2 and keeps the result in $DD00
 * (1 = FM), which the driver reads at every frame update and when a sound starts (FM / PSG record
 * tables $8BED / $8B53). The engine's unit is attached before the boot (mw_on_post_reset); a change
 * in the HUD is done at the driver's frame update (mw_snd_update at bank 15 $89AF, called from the
 * frame interrupt $013B with bank 15 mapped): attach / detach, $DD00, stop all ($9294: every channel
 * slot cleared, PSG + FM silent), then the last song command again through $DD02, which the same
 * update processes ($8AC6).
 *
 * Battle rewards (found 2026-11-18 from the ROM): the status panel's four numbers are 16-bit words
 * $C430 guilders (cap 60000, add $4A41 / sub $4A4F), $C432 (cap 50), $C434 fangs (cap 60000, add $4A81;
 * the shop at $59D0 turns fangs into 5 guilders each), $C436 character points ("C" on the panel, NOT the experience; NPC code at
 * $0F73.. compares it with 32000; cap 64000, shown as the value
 * - 32000 with a sign: add $4AA1 / sub $4AAF); each add = `add hl,bc` then the cap ($494E) and the
 * panel digits. After a won battle ($039A) the game adds the enemy record: `ld bc,($C14D) ; ld b,0 ;
 * call $4A81` (fangs), `ld bc,($C14A) ; call $4A41` (guilders), `ld bc,($C148)`, `bit 7` of $C140
 * -> `call z,$4AA1` (gain) / `call nz,$4AAF` (loss). Observing [insn_hooks] at the three calls scale
 * BC (gains only; losses stay original), clamped so the game's 16-bit add cannot wrap past its cap.
 *
 * Experience = the level bar of each of the 4 party members (block $C500 + $40 n: +1 level (max 15),
 * +6 bar, +8 bar limit). On every hit ($45B9 / $4621 / $4689 / $46F1, from the battle code $1144 /
 * $1366) the gain = table $4769[level * 16 + enemy class $C14C] ($4759) is added to the bar ($495D:
 * `add hl,bc`, compared with the limit); at the limit the level goes up AT ONCE, mid-battle
 * ($4971 / $49A5 / $49D9 / $4A0D: level + 1, new stats, new limit), the rest stays in the bar (one
 * level per hit at most; the rest can level again on the next hit). EXP hooks at the four
 * `call $495D` ($45CA $4632 $469A $4702): BC = the gain, HL = the bar.
 *
 * Sound driver (bank 15; the same 32-byte channel slot scheme as Phantasy Star's Sega driver, found
 * 2026-11-15 with SMS_PSGLOG_IX): slots at $DD0E + $20 n; +0 bit 7 = playing ($C0 = the rhythm
 * slot), +1 = the output: PSG $80 / $A0 / $C0 / $E0 (tone 0-2, noise) or FM $10 + n (FM channel n).
 * Music: $DD0E..$DD8E = FM 0-4, $DDAE = rhythm; sound effects: the three slots $DDCE, $DDEE, $DE0E
 * (random play: effects in $DE0E on FM 5, sometimes 3 / 4). The game plays FM when the FM unit
 * answers on port $F2 (SOUND CHIP = FM). Every frame the outputs used by a playing
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
static int s_fm_i;                                    /* SOUND CHIP: 0 PSG (original), 1 FM */
#define N_MULT 101                                    /* multipliers x0 .. x100 (= index) */
static int s_exp_i = 1, s_gld_i = 1, s_fang_i = 1, s_pts_i = 1;
static char s_mult_buf[N_MULT][16];
static const char *k_mult[N_MULT];
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
        else if (!strcmp(key, "fm_sound")) s_fm_i = v != 0;
        else if (!strcmp(key, "exp_multiplier"))      s_exp_i  = v < 0 ? 1 : v > 100 ? 100 : (int)(v + 0.5);
        else if (!strcmp(key, "guilders_multiplier")) s_gld_i  = v < 0 ? 1 : v > 100 ? 100 : (int)(v + 0.5);
        else if (!strcmp(key, "char_points_multiplier")) s_pts_i  = v < 0 ? 1 : v > 100 ? 100 : (int)(v + 0.5);
        else if (!strcmp(key, "fangs_multiplier"))    s_fang_i = v < 0 ? 1 : v > 100 ? 100 : (int)(v + 0.5);
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
    fprintf(f, "fm_sound       = %d     ; sound chip: 0 = PSG (original: Western consoles have no FM unit), 1 = FM (Japanese FM Sound Unit)\n", s_fm_i);
    fprintf(f, "music_volume   = %-5d ; music volume in percent (0..100)\n", s_bgm_i);
    fprintf(f, "sfx_volume     = %-5d ; sound effects volume in percent (0..100)\n", s_sfx_i);
    fprintf(f, "exp_multiplier      = %-5d ; EXP per hit (the level bar) x N (0..100, 1 = original)\n", s_exp_i);
    fprintf(f, "guilders_multiplier = %-5d ; battle guilders reward x N (0..100, 1 = original)\n", s_gld_i);
    fprintf(f, "fangs_multiplier    = %-5d ; battle fangs reward x N (0..100, 1 = original)\n", s_fang_i);
    fprintf(f, "char_points_multiplier = %-2d ; character points gained in a battle x N (0..100, 1 = original; losses unchanged)\n", s_pts_i);
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
static const HudOption k_mult_opts[] = {
    { "EXP",      k_mult, N_MULT, &s_exp_i,  NULL },
    { "GUILDERS", k_mult, N_MULT, &s_gld_i,  NULL },
    { "FANGS",    k_mult, N_MULT, &s_fang_i, NULL },
    { "CHARACTER POINTS", k_mult, N_MULT, &s_pts_i, NULL },
};
static const char *k_chip[] = { "PSG (ORIGINAL)", "FM" };
static HudOption k_sound_opts[] = {
    { "SOUND CHIP",    k_chip, 2,   &s_fm_i,  NULL },               /* switched in mw_snd_update */
    { "MUSIC",         k_pct, 101, &s_bgm_i, on_volume_changed },
    { "SOUND EFFECTS", k_pct, 101, &s_sfx_i, on_volume_changed },
};

/* [insn_hooks] $03A0 / $03A7 / $03B3: BC = the reward, right before the game's own add (observe: 0) */
static void scale_bc(int mult, uint16_t stat, const char *what)
{
    unsigned bc = (unsigned)g_z80.b << 8 | g_z80.c, cur = sms_read16(stat), v = bc * (unsigned)mult;
    if (cur + v > 0xFFFF) v = 0xFFFF - cur;             /* the add must not wrap: the game caps it below */
    if (getenv("MW_LOG")) fprintf(stderr, "[mw] %s %u x%d -> %u (was %u)\n", what, bc, mult, v, cur);
    g_z80.b = (uint8_t)(v >> 8); g_z80.c = (uint8_t)v;
}
int mw_fangs(void)    { if (s_fang_i != 1) scale_bc(s_fang_i, 0xC434, "fangs");    return 0; }
int mw_guilders(void) { if (s_gld_i != 1)  scale_bc(s_gld_i,  0xC430, "guilders"); return 0; }
int mw_points(void)   { if (s_pts_i != 1 && !(sms_read8(0xC140) & 0x80)) scale_bc(s_pts_i, 0xC436, "character points"); return 0; }
/* [insn_hooks] $45CA / $4632 / $469A / $4702: BC = the bar gain of this hit, HL = the bar (observe: 0) */
int mw_exp_bar(void)
{
    if (s_exp_i == 1) return 0;
    unsigned bc = (unsigned)g_z80.b << 8 | g_z80.c, hl = (unsigned)g_z80.h << 8 | g_z80.l, v = bc * (unsigned)s_exp_i;
    if (hl + v > 0xFFFF) v = 0xFFFF - hl;               /* `add hl,bc` must not wrap */
    if (getenv("MW_LOG")) fprintf(stderr, "[mw] exp bar %u + %u x%d -> %u\n", hl, bc, s_exp_i, v);
    g_z80.b = (uint8_t)(v >> 8); g_z80.c = (uint8_t)v;
    return 0;
}

/* [insn_hooks] bank 15 $89AF (the sound driver's frame update; observe only) */
static uint8_t s_last_song;                           /* last song command seen in $DD02 */
static bool is_song(uint8_t c) { return c >= 0x81 && c <= 0x88; }   /* priority $80 in $8B2C ($A7 too: not a song) */
int mw_snd_update(void)
{
    uint8_t c = sms_read8(0xDD03);                           /* the game's request ($8A2E moves it to $DD02) */
    if (is_song(c)) s_last_song = c;
    else if (c >= 0xA8) s_last_song = 0;                     /* $8AC6: >= $A8 = stop all */
    {   /* test: MW_SND_LOG=2 prints every command and which channel slots play afterwards */
        static int on = -1; if (on < 0) { const char *e = getenv("MW_SND_LOG"); on = e && e[0] == '2'; }
        static uint8_t pend; static unsigned long fr; fr++;
        if (on && pend) {
            fprintf(stderr, "[snd2] %lu cmd %02X -> slots:", fr, pend);
            for (int k = 0; k < 9; k++) if (sms_read8((uint16_t)(0xDD0E + 0x20 * k)) & 0x80) fprintf(stderr, " %04X", 0xDD0E + 0x20 * k);
            fprintf(stderr, "\n"); pend = 0;
        }
        if (on && c != 0x80) pend = c;
    }
    bool want = s_fm_i != 0, cur = sms_read8(0xDD00) != 0;
    if (want == cur && fm_attached() == want) return 0;
    Z80State keep = g_z80;                                   /* our call must not disturb the driver */
    fm_set_attached(want);
    sms_write8(0xDD00, want ? 1 : 0);                        /* as the boot code's result */
    sms_io_out(0xF2, want ? 1 : 0);                          /* FM output on / off, as the boot code ($3D22) */
    g_z80.sp = (uint16_t)(g_z80.sp - 2); sms_write16(g_z80.sp, 0x0000);
    call_by_address(0x9294);                                 /* stop all (bank 15 is mapped) */
    uint64_t cyc = g_z80.cyc;
    g_z80 = keep;
    g_z80.cyc = cyc;
    if (s_last_song) sms_write8(0xDD02, s_last_song);        /* started by this update ($8AC6) */
    if (getenv("MW_LOG")) fprintf(stderr, "[mw] sound chip -> %s, song %02X restarted\n", want ? "FM" : "PSG", s_last_song);
    return 0;
}

void mw_on_post_reset(void)
{
    load_config();
    fm_set_attached(s_fm_i);                          /* before the boot code looks for the unit */
    hud_set_turbo_speed(k_ff_vals[s_ff_i]);
    hud_set_title("MIRACLE WARRIORS - OPTIONS");
    hud_set_options(k_opts, (int)(sizeof k_opts / sizeof k_opts[0]));
    hud_set_on_close(on_close);
    {   static bool pages;
        if (!pages) { pages = true;
            for (int i = 0; i <= 100; i++) { snprintf(s_pct_buf[i], sizeof s_pct_buf[i], "%d%%", i); k_pct[i] = s_pct_buf[i]; }
            for (int i = 0; i < N_MULT; i++) {
                snprintf(s_mult_buf[i], sizeof s_mult_buf[i], i == 0 ? "X0 (NONE)" : i == 1 ? "X1 (ORIGINAL)" : "X%d", i);
                k_mult[i] = s_mult_buf[i]; }
            hud_add_option_page("MULTIPLIERS", k_mult_opts, (int)(sizeof k_mult_opts / sizeof k_mult_opts[0]));
            hud_add_option_page("SOUND", k_sound_opts, (int)(sizeof k_sound_opts / sizeof k_sound_opts[0])); } }
    {   static const char *const order[] = { "OPTIONS", "MULTIPLIERS", "SOUND" };
        hud_set_page_order(order, 3); }
}
void mw_on_frame_post(uint64_t frame)
{
    {   /* test: MW_FM_SWITCH=<frame>:<0|1>,... sets SOUND CHIP at that frame (as the HUD would) */
        const char *e = getenv("MW_FM_SWITCH");
        for (const char *q = e; q && *q; ) {
            unsigned long at = strtoul(q, (char **)&q, 10); int v = *q == ':' ? atoi(q + 1) : 0;
            if (at == frame) s_fm_i = v != 0;
            q = strchr(q, ','); if (q) q++;
        } }
    {   /* test: MW_RAM_DUMP=<file> + MW_RAM_EVERY=n appends the 8 KB of RAM every n frames */
        static FILE *f; static int on = -1, every = 60;
        if (on < 0) { const char *e = getenv("MW_RAM_DUMP"), *n = getenv("MW_RAM_EVERY");
            on = e && (f = fopen(e, "wb")) != NULL; if (n) every = atoi(n); if (every < 1) every = 1; }
        if (on && frame % (unsigned)every == 0) {
            for (unsigned a = 0xC000; a < 0xE000; a++) fputc(sms_read8((uint16_t)a), f);
            fflush(f);
        } }
    sound_frame();
}
