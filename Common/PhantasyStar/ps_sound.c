/*
 * ps_sound.c - Phantasy Star: choose the sound driver / chip (SOUND tab "SOUND CHIP").
 *
 * The whole sound system of Phantasy Star is one self-contained 16 KB bank (driver + music data),
 * reached through three shims in bank 0 (`ld hl,$FFFF ; ld (hl),BANK ; jp ENTRY`: update, init,
 * mute) and the command byte $C004. Two driver families exist (checked by CRC32 of the bank):
 *   WEST  (CRC E3B31E57): Rev A, USA, and bank 12 of SEGA AGES En. PSG only.
 *         Entries: update $8043, init $8000, mute $801F.
 *   JP    (CRC CFACAEB8): bank 12 of Japan, Japan Rev 1, AGES Ja, Korea and the Japan-based
 *         patches, and bank 32 of SEGA AGES En (byte-identical: SEGA put the Japanese driver into
 *         the English game). FM or PSG, chosen by RAM $C000 (1 = FM unit present; the Japanese
 *         boot code detects the unit through port $F2 at $03A4 and stores the result there).
 *         Entries: update $8052, init $8000, mute $801F.
 * Modes: FM (JP driver, $C000 = 1, port $F2 = 1), PSG (JP driver, $C000 = 0, $F2 = 0),
 * PSG WESTERN (WEST driver, $F2 = 0). A mode is available when its driver bank is in the loaded
 * ROM or in another ROM in the same folder (or its subfolders): such a bank is appended to the
 * ROM image (glue_add_rom_bank, bank 32/33/...), where the engine runs it in the interpreter (it
 * is not recompiled in this build). Every ROM speaks the same command numbers (SEGA AGES En runs
 * Rev A's game code with the Japanese driver), so any game can use either driver.
 * The native mode (what the ROM does by itself: Western ROMs PSG WESTERN, Japanese ROMs and AGES En
 * FM) runs the original shims; other modes are done by the hooks ps_snd_update / _init / _mute at
 * the three shims (Tools/ps_find_hooks.py group "sound"): they map the chosen driver bank, run the
 * entry (its RET pops the shim's return address) and return 2 (= return from the shim).
 * Switching (HUD) takes effect at the next update (VBlank): the old driver is muted, the new one
 * initialised, and the last song ($81-$94 seen in $C004) is started again.
 * Test env: PS_SND_DEBUG=1 (drivers found, mode changes), PS_SOUND_CHIP=fm|psg|psg_western.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "sms_runtime.h"
#include "z80_ops.h"

extern uint8_t    *glue_rom_image(size_t *size);
extern int         glue_add_rom_bank(const uint8_t *bank16k);
extern const char *glue_rom_path(void);

#define CRC_WEST 0xE3B31E57u
#define CRC_JP   0xCFACAEB8u

enum { DRV_WEST, DRV_JP };
enum { MODE_FM, MODE_PSG, MODE_PSG_WEST, MODE_COUNT };
static const char *const k_mode_key[MODE_COUNT]  = { "fm", "psg", "psg_western" };
static const char *const k_mode_name[MODE_COUNT] = { "FM", "PSG", "PSG WEST" };

typedef struct { int bank; uint16_t update, init, mute; } Driver;
static Driver   s_drv[2] = { { -1, 0x8043, 0x8000, 0x801F }, { -1, 0x8052, 0x8000, 0x801F } };
static int      s_native_drv = -1;          /* driver the shims call */
static int      s_native_mode = MODE_PSG_WEST;
static int      s_cur = -1, s_want = -1;    /* mode running / chosen */
static uint8_t  s_last_music;               /* last song command seen ($81-$94), 0 = none */
static bool     s_ok;                       /* shims found */
static int      s_debug = -1;
static char     s_src[2][160];              /* where each driver came from (debug) */

static bool dbg(void) { if (s_debug < 0) s_debug = getenv("PS_SND_DEBUG") != NULL; return s_debug; }

static uint32_t crc32_buf(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) { c ^= p[i]; for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
static int driver_of(const uint8_t *bank)   /* DRV_* or -1 */
{
    uint32_t c = crc32_buf(bank, 0x4000);
    return c == CRC_WEST ? DRV_WEST : c == CRC_JP ? DRV_JP : -1;
}

/* the three shims: ld hl,$FFFF ; ld (hl),B ; jp E  (x3: update, init, mute) */
static long find_shims(const uint8_t *rom, size_t n)
{
    static const int pat[8] = { 0x21, 0xFF, 0xFF, 0x36, -1, 0xC3, -1, -1 };
    long hit = -1;
    for (size_t i = 0; i + 24 <= n && i < 0x8000; i++) {
        int k = 0;
        for (; k < 24; k++) { int p = pat[k % 8]; if (p >= 0 && rom[i + k] != p) break; }
        if (k == 24) { if (hit >= 0) return -1; hit = (long)i; }
    }
    return hit;
}

/* other ROMs in the folder of the loaded ROM (and its subfolders, one level) */
static bool donor_from_file(const char *path, int want, uint8_t *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    bool found = false;
    if (sz > 0 && sz <= 4 * 1024 * 1024 && sz % 0x4000 == 0) {
        uint8_t *d = (uint8_t *)malloc((size_t)sz);
        if (d && fread(d, 1, (size_t)sz, f) == (size_t)sz)
            for (long b = 0; b < sz / 0x4000 && !found; b++)
                if (driver_of(d + b * 0x4000) == want) { memcpy(out, d + b * 0x4000, 0x4000); found = true; }
        free(d);
    }
    fclose(f);
    return found;
}
static bool is_rom_name(const char *n)
{
    size_t l = strlen(n);
    return l > 4 && (!strcmp(n + l - 4, ".sms") || !strcmp(n + l - 4, ".SMS"));
}
static bool find_donor(const char *dir, int want, uint8_t *out, int depth, char *src, size_t srcn)
{
    DIR *d = opendir(dir);
    if (!d) return false;
    struct dirent *e; bool found = false;
    char sub[16][512]; int nsub = 0;
    while (!found && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (is_rom_name(e->d_name)) { if (donor_from_file(p, want, out)) { found = true; snprintf(src, srcn, "%s", e->d_name); } }
        else if (depth > 0 && nsub < 16 && strlen(p) < 512) {
            DIR *t = opendir(p); if (t) { closedir(t); snprintf(sub[nsub++], 512, "%s", p); }
        }
    }
    closedir(d);
    for (int i = 0; i < nsub && !found; i++) found = find_donor(sub[i], want, out, depth - 1, src, srcn);
    return found;
}

int ps_sound_mode_from_key(const char *k)
{
    for (int m = 0; m < MODE_COUNT; m++) if (!strcmp(k, k_mode_key[m])) return m;
    return -1;
}
const char *ps_sound_mode_key(int m)  { return m >= 0 && m < MODE_COUNT ? k_mode_key[m] : "native"; }
const char *ps_sound_mode_name(int m) { return m >= 0 && m < MODE_COUNT ? k_mode_name[m] : "?"; }
static int mode_drv(int m) { return m == MODE_PSG_WEST ? DRV_WEST : DRV_JP; }
bool ps_sound_mode_available(int m) { return s_ok && m >= 0 && m < MODE_COUNT && s_drv[mode_drv(m)].bank >= 0; }
int  ps_sound_native_mode(void) { return s_native_mode; }
int  ps_sound_mode(void) { return s_want; }
void ps_sound_select(int m)
{
    if (!ps_sound_mode_available(m)) m = s_native_mode;
    if (m != s_want && dbg()) fprintf(stderr, "[snd] chip -> %s\n", k_mode_name[m]);
    s_want = m;
}

/* Called first in ps_on_post_reset (before anything caches glue_rom_image: banks may be appended). */
void ps_sound_init(void)
{
    static bool done;
    if (done) return;
    done = true;
    size_t n = 0; uint8_t *rom = glue_rom_image(&n);
    if (!rom) return;
    long sh = find_shims(rom, n);
    if (sh < 0) { if (dbg()) fprintf(stderr, "[snd] sound shims not found: SOUND CHIP disabled\n"); return; }
    int nb = rom[sh + 4]; uint16_t ne = (uint16_t)(rom[sh + 6] | (rom[sh + 7] << 8));
    s_native_drv = ne == 0x8052 ? DRV_JP : ne == 0x8043 ? DRV_WEST : -1;
    if (s_native_drv < 0 || (size_t)(nb + 1) * 0x4000 > n) return;
    s_drv[s_native_drv].bank = nb;                    /* the ROM's own driver (also Brazil's variant) */
    snprintf(s_src[s_native_drv], sizeof s_src[0], "own bank %d", nb);
    s_native_mode = s_native_drv == DRV_JP ? MODE_FM : MODE_PSG_WEST;
    static uint8_t buf[0x4000];
    for (int b = 0; (size_t)(b + 1) * 0x4000 <= n; b++) {      /* the other family in this ROM (AGES En) */
        int k = driver_of(rom + (size_t)b * 0x4000);
        if (k >= 0 && s_drv[k].bank < 0) {
            /* the shims never call it, so this build did not recompile it: use a copy appended to the
               image, which the engine runs in the interpreter like a donor bank */
            memcpy(buf, rom + (size_t)b * 0x4000, 0x4000);
            int nbk = glue_add_rom_bank(buf);
            rom = glue_rom_image(&n);
            if (nbk >= 0) { s_drv[k].bank = nbk; snprintf(s_src[k], sizeof s_src[0], "own bank %d (copy -> bank %d)", b, nbk); }
        }
    }
    const char *rp = glue_rom_path();
    char dir[1024]; snprintf(dir, sizeof dir, "%s", rp && rp[0] ? rp : ".");
    char *slash = NULL; for (char *q = dir; *q; q++) if (*q == '/' || *q == '\\') slash = q;
    if (slash) *slash = 0; else snprintf(dir, sizeof dir, ".");
    for (int k = 0; k < 2; k++) {
        if (s_drv[k].bank >= 0) continue;
        char src[160] = "";
        if (find_donor(dir, k, buf, 1, src, sizeof src)) {
            int b = glue_add_rom_bank(buf);
            if (b >= 0) { s_drv[k].bank = b; snprintf(s_src[k], sizeof s_src[0], "%s -> bank %d", src, b); }
        }
    }
    s_ok = true;
    s_cur = s_native_mode;
    if (s_want < 0) s_want = s_native_mode;
    {   const char *e = getenv("PS_SOUND_CHIP");          /* test */
        if (e) { int m = ps_sound_mode_from_key(e); if (m >= 0) ps_sound_select(m); } }
    if (dbg()) fprintf(stderr, "[snd] shims at %04lX, native %s; WEST: %s; JP: %s\n", (unsigned long)sh,
                       k_mode_name[s_native_mode], s_drv[DRV_WEST].bank >= 0 ? s_src[DRV_WEST] : "none",
                       s_drv[DRV_JP].bank >= 0 ? s_src[DRV_JP] : "none");
}

/* run a driver entry in its bank; with `dummy` a return address is pushed first (our own calls) */
static void run_entry(int drv, uint16_t entry, bool dummy)
{
    if (dummy) { g_z80.sp = (uint16_t)(g_z80.sp - 2); sms_write16(g_z80.sp, 0x0000); }
    sms_write8(0xFFFF, (uint8_t)s_drv[drv].bank);
    call_by_address(entry);
}
static int s_f2 = -1;                         /* last value we wrote to port $F2 (-1 = unknown) */
static void set_f2(int v) { if (v != s_f2) { sms_io_out(0xF2, (uint8_t)v); s_f2 = v; } }
static void chip_regs(int m)
{
    if (m == MODE_FM)       { sms_write8(0xC000, 1); set_f2(1); }
    else if (m == MODE_PSG) { sms_write8(0xC000, 0); set_f2(0); }
    else                    { set_f2(0); }
}
static void switch_now(void)
{
    int from = s_cur, to = s_want;
    s_f2 = -1;
    run_entry(mode_drv(from), s_drv[mode_drv(from)].mute, true);
    chip_regs(to);
    run_entry(mode_drv(to), s_drv[mode_drv(to)].init, true);
    if (s_last_music) sms_write8(0xC004, s_last_music);
    s_cur = to;
    if (dbg()) fprintf(stderr, "[snd] switched %s -> %s, restart song %02X\n", k_mode_name[from], k_mode_name[to], s_last_music);
}
static void note_command(void)
{
    uint8_t c = sms_read8(0xC004);
    if (c >= 0x81 && c <= 0x94) s_last_music = c;
    else if (!(c & 0x80) || (c >= 0x95 && c <= 0x9F) || (c >= 0xD5 && c <= 0xD9)) { if (c != 0x80) s_last_music = 0; }
}

/* [insn_hooks] at the three shims: 0 = original shim, 2 = done (returned from the shim) */
int ps_snd_update(void)
{
    if (!s_ok) return 0;
    {   /* test: PS_SOUND_SWITCH=<n>:<mode>[,...] switches at the n-th sound update (VBlank) */
        static const char *e = (const char *)1; static unsigned long cnt;
        if (e == (const char *)1) e = getenv("PS_SOUND_SWITCH");
        cnt++;
        for (const char *q = e; q && *q; ) {
            unsigned long at = strtoul(q, (char **)&q, 10);
            char key[16] = ""; if (*q == ':') sscanf(q + 1, "%15[a-z_]", key);
            if (at == cnt) { int m = ps_sound_mode_from_key(key); if (m >= 0) ps_sound_select(m); }
            q = strchr(q, ','); if (q) q++;
        } }
    note_command();
    if (s_want != s_cur) {
        Z80State keep = g_z80;                     /* our own driver calls must not disturb the caller: */
        switch_now();
        uint64_t cyc = g_z80.cyc;                  /* registers back, but time goes on */
        g_z80 = keep;
        g_z80.cyc = cyc;
    }
    if (s_cur == s_native_mode) return 0;
    chip_regs(s_cur);
    run_entry(mode_drv(s_cur), s_drv[mode_drv(s_cur)].update, false);
    return 2;
}
int ps_snd_init(void)
{
    if (!s_ok || s_cur == s_native_mode) return 0;
    chip_regs(s_cur);
    run_entry(mode_drv(s_cur), s_drv[mode_drv(s_cur)].init, false);
    return 2;
}
int ps_snd_mute(void)
{
    if (!s_ok || s_cur == s_native_mode) return 0;
    run_entry(mode_drv(s_cur), s_drv[mode_drv(s_cur)].mute, false);
    return 2;
}
