/*
 * gv_info.c - Golvellius: area names and what each screen's cave holds (side panel + HUD MAP tab).
 *
 * Areas: the per-screen area id (asset $5D = file $15E48 + screen, low nibble, 0 = the previous screen's)
 * splits the overworld into 7 regions; names = the game's own boss names (gv_cave.c, bank 7 $1D33C) in
 * title case + a landscape word (the regions of https://shrines.rpgclassics.com/sms/golvellius/maps.shtml,
 * matched by shape): id 1 rows 12-14 Despa Valley, id 1 rows 4-11 Rolick Desert, 3 Bachular Graveyard,
 * 4 + 5 Fosbus Swamp, 6 Warlic Lake, 7 Crawky Forest, 8 Haidee Forest.
 *
 * Caves (bank 7 $8E17, entering a cave): the screen's cave id = room record byte 3 ($CAD3);
 *   < $10      : dungeon 1..8 (0 = the start house L14);
 *   $10-$4F    : content $8CDA[id >> 4] (1 Randar, 2 Enny, 4 Dina, 3 Winkle = password);
 *   $50-$AF    : content $8C8F[id] (people with advice, text $967C[content]);
 *   $B0-$F7    : content $8C8F[id] = an offer $28-$32 (potion, bible, crystal, sword, shield, pendant,
 *                boots, ring, gift ring, herb, mirror) or the old woman $33-$35; price $8D87 + (id-$B0)*2
 *                = BCD digits [hi & 15][lo >> 4][lo & 15] x 100 gold ($9155; hi >> 4 = the crystal
 *                number); bought = $9456: bit (n' & 7) of $C86F + n' / 8, n = id - $B0 minus the herb
 *                caves before it (list $944A; herbs are never marked) - the password's progress bytes.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "sms_runtime.h"

extern uint8_t *glue_rom_image(size_t *size);
extern const char *gv_boss_name(int id);            /* gv_cave.c: the game's names, upper case */
extern bool gv_cave_entered(int scr);
int gv_cave_info_all;                               /* option CAVE INFO: 0 entered caves, 1 all (spoiler) */
bool gv_cave_known(int scr) { return gv_cave_info_all || gv_cave_entered(scr); }

#define COLS 13
#define NSCR 182
static const uint8_t *s_rom; static size_t s_n;
static bool rom(void) { if (!s_rom) s_rom = glue_rom_image(&s_n); return s_rom && s_n >= 0x40000; }
static uint8_t b7(int a) { return s_rom[7 * 0x4000 + (a - 0x8000)]; }

static int area_id(int scr)
{
    int v = s_rom[0x15E48 + scr] & 15;
    return v || scr == 0 ? v : s_rom[0x15E48 + scr - 1] & 15;
}
/* "Despa Valley" etc.; "" if unknown */
const char *gv_area_name(int scr)
{
    static char out[32];
    out[0] = 0;
    if (!rom() || scr < 0 || scr >= NSCR) return out;
    int id = area_id(scr), boss; const char *what;
    switch (id) {
    case 1: if (scr / COLS >= 11) { boss = 1; what = "Valley"; } else { boss = 2; what = "Desert"; } break;
    case 3: boss = 3; what = "Graveyard"; break;
    case 4: case 5: boss = 4; what = "Swamp"; break;
    case 6: boss = 5; what = "Lake"; break;
    case 7: boss = 6; what = "Forest"; break;
    case 8: boss = 7; what = "Forest"; break;
    default: return out;
    }
    const char *b = gv_boss_name(boss);
    size_t k = 0;
    for (; b[k] && k < 16; k++) out[k] = (char)(k ? (b[k] >= 'A' && b[k] <= 'Z' ? b[k] + 32 : b[k]) : b[k]);
    snprintf(out + k, sizeof out - k, " %s", what);
    return out;
}

static int cave_id(int scr) { return s_rom[0x15947 + s_rom[0x15890 + scr] * 7 + 3]; }
static int content(int cid) { return cid < 0x10 ? -1 : cid < 0x50 ? b7(0x8CDA + (cid >> 4)) : b7(0x8C8F + cid); }
/* bought / taken flag of offer cave cid ($B0+), -1 if it has none (herbs) */
static int bought(int cid)
{
    int n = cid - 0xB0, e = 0;
    for (int i = 0; i < 12; i++) { int t = b7(0x944A + i); if (n == t) return -1; if (n > t) e++; else break; }
    n -= e;
    return (sms_read8((uint16_t)(0xC86F + (n >> 3))) >> (n & 7)) & 1;
}
static const char *offer(int code)
{
    switch (code) {
    case 0x28: return "POTION OF LIFE";  case 0x29: return "BIBLE";        case 0x2A: return "CRYSTAL";
    case 0x2B: return "SWORD";           case 0x2C: return "SHIELD";       case 0x2D: return "PENDANT";
    case 0x2E: return "BOOTS";           case 0x2F: return "RING (BREAKS ROCK)";
    case 0x30: return "RING FOR THE OLD WOMAN";  case 0x31: return "HERB MEA";  case 0x32: return "MAGIC MIRROR";
    }
    return code >= 0x33 && code <= 0x35 ? "OLD WOMAN" : NULL;
}
/* one line: what the screen's cave holds; *done = 1 bought, 2 taken (the gift ring) */
bool gv_cave_label(int scr, char *out, size_t n, int *done)
{
    *done = 0; out[0] = 0;
    if (!rom() || scr < 0 || scr >= NSCR) return false;
    int cid = cave_id(scr);
    if (cid == 0) { snprintf(out, n, "START HOUSE"); return true; }
    if (cid < 0x10) { snprintf(out, n, "DUNGEON: %s", gv_boss_name(cid)); return true; }
    int c = content(cid);
    if (cid < 0x50) {
        switch (c) {
        case 1: snprintf(out, n, "RANDAR: POWER FOR GOLD"); return true;
        case 2: snprintf(out, n, "ENNY: POWER FOR GOLD"); return true;
        case 3: snprintf(out, n, "WINKLE: PASSWORD"); return true;
        case 4: snprintf(out, n, "DINA: GOLD FOR POWER"); return true;
        }
    }
    if (cid < 0xB0) { snprintf(out, n, "ADVICE"); return true; }
    const char *o = offer(c);
    if (!o) { snprintf(out, n, "CAVE"); return true; }
    if (c >= 0x33) { snprintf(out, n, "%s", o); return true; }
    int lo = b7(0x8D87 + (cid - 0xB0) * 2), hi = b7(0x8D87 + (cid - 0xB0) * 2 + 1);
    long price = (hi & 15) * 10000L + (lo >> 4) * 1000L + (lo & 15) * 100L;
    char what[40];
    if (c == 0x2A) snprintf(what, sizeof what, "CRYSTAL %d", (hi >> 4) + 1); else snprintf(what, sizeof what, "%s", o);
    if (c == 0x30 || price == 0) snprintf(out, n, "%s: FREE", what);
    else snprintf(out, n, "%s: %ld GOLD", what, price);
    *done = bought(cid) == 1 ? (c == 0x30 ? 2 : 1) : 0;
    return true;
}
/* the advice text of a person's cave: up to max lines; 0 if the cave has none */
int gv_cave_text(int scr, char lines[][32], int max)
{
    if (!rom() || scr < 0 || scr >= NSCR) return 0;
    int cid = cave_id(scr), c = content(cid);
    if (cid < 0x50 || cid >= 0xB0 || c < 0) return 0;
    int p = b7(0x967C + 2 * c) | b7(0x967C + 2 * c + 1) << 8;
    size_t o = (size_t)7 * 0x4000 + (size_t)(p - 0x8000) + 2;    /* 2 header bytes */
    int nl = 0, k = 0;
    for (int i = 0; i < 300 && nl < max && o + i < s_n; i++) {
        int ch = s_rom[o + i];
        if (ch == 0xFF || k >= 31) { lines[nl][k] = 0; if (k) nl++; k = 0; if (ch == 0xFF) continue; }
        if (ch >= 0xF0) break;
        char out = 0;
        if (ch >= 'A' && ch <= 'Z') out = (char)ch;
        else if (ch >= 0xDF && ch <= 0xE8) out = (char)('0' + (ch - 0xDF));
        else switch (ch) {
            case 0x00: out = ' '; break;  case 0x5B: case 0x2C: out = '.'; break;  case 0x5E: out = ','; break;
            case 0x5D: out = '!'; break;  case 0x5C: out = '\''; break;  case 0x5F: out = '?'; break;
            case 0xCD: out = '-'; break;  case 0x6C: out = ';'; break;  case 0x6F: case 0x70: out = '"'; break;
        }
        if (out && nl < max) lines[nl][k++] = out;
    }
    if (k && nl < max) { lines[nl][k] = 0; nl++; }
    return nl;
}

/* test: GV_INFO_DUMP=<frame> prints every screen's area, cave label and advice text */
#include <stdlib.h>
void gv_info_test(uint64_t frame)
{
    static long at = -2; if (at == -2) { const char *e = getenv("GV_INFO_DUMP"); at = e ? atol(e) : -1; }
    if (at < 0 || frame != (uint64_t)at) return;
    for (int s = 0; s < NSCR; s++) {
        char l[48], t[6][32]; int done = 0;
        gv_cave_label(s, l, sizeof l, &done);
        fprintf(stderr, "[info] %c%-2d %-20s %s%s\n", 'A' + s % COLS, s / COLS + 1, gv_area_name(s), l, done == 1 ? " BOUGHT" : done == 2 ? " TAKEN" : "");
        int n = gv_cave_text(s, t, 6);
        for (int i = 0; i < n; i++) fprintf(stderr, "[info]        | %s\n", t[i]);
    }
}
