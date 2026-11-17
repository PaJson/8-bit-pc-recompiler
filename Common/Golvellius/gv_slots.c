/*
 * gv_slots.c - Golvellius SAVE SLOTS (part of the REMEMBER PASSWORD option): a password and the map
 * data that belongs to it (world map .gvmap, dungeon maps .gvcave) are kept together.
 *
 *  Slot 0 = AUTOSAVE = the current game: the working files <save>.password / .gvmap / .gvcave, written
 *           as before (every password the game makes, the maps whenever the HUD closes and at exit).
 *  Slots 1-5 = <save>.slot<n>.password / .gvmap / .gvcave / .info (screen + time of the save).
 *
 *  Winkle's cave (cave room mode $C802 = $11, content $C168 = 3): once he has told the password (his
 *  text's $FA command runs the generator, bank 7 $8336 -> $83D6 -> $852B; gv_pw_made), a line at the
 *  top shows the slot it goes to; LB / RB (keys C / V) pick another one. Leaving the cave copies the
 *  autosave (password + maps) into that slot. Default = the slot this game was loaded from (0 after
 *  a New Game: then nothing is copied unless a slot is picked).
 *  Password screen (title -> CONTINUE -> PASSWORD CONTINUE): pre-filled with the slot used last;
 *  LB / RB step through the slots that have a password (the line at the bottom names it). When the
 *  game accepts it (mode 6 starts) and the entered password is that slot's, the slot's maps become the
 *  autosave (the old autosave files are kept as <save>.bak.*). A typed password keeps the maps.
 *  New Game (the game's new-game routine $12C8, also LAST GAME CONTINUE after a fresh start): the
 *  autosave starts empty (old files kept as <save>.bak.*). LAST GAME CONTINUE with a game in RAM
 *  ($131B: $C817 != 0 -> screen L14) keeps everything.
 * Nothing is written with SMS_NO_SAVE. Test env: GV_SLOT_LOG=1, GV_SLOT_KEYS=<frame>:<1|2>,... (1 = LB, 2 = RB).
 *
 * Golvellius Plus (our ROM patch) has its own save slots in battery RAM: AUTO (Winkle) + 1-5 (Pause /
 * Game Over -> SAVE GAME), loaded from title -> CONTINUE -> SAVE. Those replace the slots above (the
 * option is off there); the maps follow the ROM's slots instead, as <save>.save<n>.gvmap / .gvcave
 * (n = 0 AUTO, 1-5), seen from what the ROM does:
 *   save   = a slot record (battery RAM $8100 + 64 n) changes and is valid (+0 $A5, +63 = sum of +0..+62
 *            xor $5A): the current maps are copied to that slot;
 *   delete = a slot record becomes invalid: its map files are removed;
 *   load   = LoadPending ($C0A5) goes 0 -> 1 (the SAVE page copied a slot into RAM; LastSlot $C0AA =
 *            which): that slot's maps become the current ones (old files kept as <save>.bak.*); a slot
 *            saved before this existed has no map files: the current maps are kept.
 * New Game ($12C8) empties the current maps as before; a password continue keeps them.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hud.h"
#include "sms_runtime.h"
#include "safe_file.h"

#define NSLOT 6                                       /* 0 = autosave, 1..5 */
extern const char *glue_save_path(void);
extern void gv_map_save(void);
extern void gv_cave_save(void);
extern void gv_map_reset(bool load);
extern void gv_cave_reset(bool load);
extern void gv_pw_reload(void);                       /* gv_qol.c: read slot 0's .password again */
extern void gv_pw_redraw(void);                       /* gv_qol.c: show $C170.. on the password screen */
extern bool gv_slots_enabled(void);                   /* gv_qol.c: option REMEMBER PASSWORD */

static const char *const k_ext[3] = { ".password", ".gvmap", ".gvcave" };
static int  s_active, s_last;                         /* slot of the current game, slot used last */
static int  s_sel = -1;                               /* the slot picked in Winkle's cave / on the password screen */
static bool s_cave_made;                              /* Winkle told the password in this visit */
static bool s_pw_screen, s_pw_loop_seen, s_pw_loop_ran, s_pw_match;
static uint8_t s_last_mode;

static bool slog(void) { static int on = -1; if (on < 0) on = getenv("GV_SLOT_LOG") != NULL; return on; }
static bool can_save(void) { return !getenv("SMS_NO_SAVE") && glue_save_path() && glue_save_path()[0]; }

static void base(char *out, size_t n)
{
    snprintf(out, n, "%s", glue_save_path() ? glue_save_path() : "");
    size_t l = strlen(out);
    if (l > 4 && !strcmp(out + l - 4, ".sav")) out[l - 4] = 0;
}
static void slot_file(int slot, const char *ext, char *out, size_t n)
{
    char b[1024]; base(b, sizeof b);
    if (slot == 0) snprintf(out, n, "%s%s", b, ext);
    else snprintf(out, n, "%s.slot%d%s", b, slot, ext);
}
static uint8_t *read_all(const char *p, size_t *len)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f); *len = b ? (size_t)n : 0;
    return b;
}
/* dst = a copy of src (safe write); src missing = dst removed */
static bool copy_file(const char *src, const char *dst)
{
    size_t n = 0; uint8_t *b = read_all(src, &n);
    if (!b) { remove(dst); return true; }
    FILE *f = safe_fopen_w(dst, "wb");
    bool ok = f && fwrite(b, 1, n, f) == n;
    if (f) ok = safe_fclose(f, dst) && ok;
    free(b);
    if (!ok) fprintf(stderr, "[gv] save slots: could not write %s\n", dst);
    return ok;
}
static bool slot_password(int slot, uint8_t pw[32])
{
    char p[1100]; slot_file(slot, ".password", p, sizeof p);
    FILE *f = fopen(p, "rb"); if (!f) return false;
    bool ok = fread(pw, 1, 32, f) == 32; fclose(f);
    for (int i = 0; i < 32 && ok; i++) ok = pw[i] < 32;
    return ok;
}
/* .info: "screen <index>\ntime <unix time>\n" */
static bool slot_info(int slot, int *scr, long long *t)
{
    char p[1100]; slot_file(slot, ".info", p, sizeof p);
    FILE *f = fopen(p, "r"); if (!f) return false;
    *scr = -1; *t = 0;
    bool ok = fscanf(f, " screen %d time %lld", scr, t) == 2;
    fclose(f);
    return ok;
}
static void write_last(void)
{
    if (!can_save()) return;
    char p[1100]; slot_file(0, ".slot", p, sizeof p);
    FILE *f = safe_fopen_w(p, "w");
    if (f) { fprintf(f, "last %d\nactive %d\n", s_last, s_active); safe_fclose(f, p); }
}
static void read_last(void)
{
    char p[1100]; slot_file(0, ".slot", p, sizeof p);
    FILE *f = fopen(p, "r"); if (!f) return;
    int l = 0, a = 0;
    if (fscanf(f, " last %d active %d", &l, &a) == 2) {
        s_last = l >= 0 && l < NSLOT ? l : 0; s_active = a >= 0 && a < NSLOT ? a : 0;
    }
    fclose(f);
}
static void backup_autosave(void)
{
    char b[1024]; base(b, sizeof b);
    for (int k = 0; k < 3; k++) {
        char s[1100], d[1100];
        slot_file(0, k_ext[k], s, sizeof s);
        snprintf(d, sizeof d, "%s.bak%s", b, k_ext[k]);
        copy_file(s, d);
    }
}

/* the current game -> slot n */
static void save_to(int n)
{
    if (n <= 0 || n >= NSLOT || !can_save()) return;
    gv_map_save(); gv_cave_save();                     /* the autosave files are current */
    bool ok = true;
    for (int k = 0; k < 3; k++) {
        char s[1100], d[1100];
        slot_file(0, k_ext[k], s, sizeof s); slot_file(n, k_ext[k], d, sizeof d);
        ok = copy_file(s, d) && ok;
    }
    char p[1100]; slot_file(n, ".info", p, sizeof p);
    FILE *f = safe_fopen_w(p, "w");
    if (f) { fprintf(f, "screen %d\ntime %lld\n", sms_read8(0xC80D), (long long)time(NULL)); ok = safe_fclose(f, p) && ok; }
    s_active = s_last = n; write_last();
    if (slog()) fprintf(stderr, "[slot] saved to slot %d%s\n", n, ok ? "" : " (with errors)");
}
/* slot n -> the current game (the maps; the password is already in the game) */
static void load_from(int n)
{
    if (n > 0 && n < NSLOT && can_save()) {
        backup_autosave();
        for (int k = 0; k < 3; k++) {
            char s[1100], d[1100];
            slot_file(n, k_ext[k], s, sizeof s); slot_file(0, k_ext[k], d, sizeof d);
            copy_file(s, d);
        }
        gv_map_reset(true); gv_cave_reset(true); gv_pw_reload();
    }
    s_active = s_last = n; write_last();
    if (slog()) fprintf(stderr, "[slot] game continued from slot %d\n", n);
}

/* ---- Golvellius Plus: the maps follow the ROM's save slots (see the header) ---- */
extern const uint8_t *glue_sram_image(size_t *size);
#define PLUS_SLOTS   6
#define PLUS_REC     0x100                            /* battery RAM $8100 */
static bool s_plus, s_rec_init;
static uint8_t s_rec[PLUS_SLOTS][64], s_last_pending;
void gv_slots_set_plus(bool on) { s_plus = on; }
static bool rec_valid(const uint8_t *r)
{
    uint8_t s = 0;
    for (int i = 0; i < 63; i++) s = (uint8_t)(s + r[i]);
    return r[0] == 0xA5 && (uint8_t)(s ^ 0x5A) == r[63];
}
static void plus_file(int n, const char *ext, char *out, size_t sz)
{
    char b[1024]; base(b, sizeof b);
    snprintf(out, sz, "%s.save%d%s", b, n, ext);
}
static void plus_saved(int n)                         /* the ROM wrote slot n: the current maps go with it */
{
    if (!can_save()) return;
    gv_map_save(); gv_cave_save();
    for (int k = 1; k < 3; k++) {
        char s[1100], d[1100];
        slot_file(0, k_ext[k], s, sizeof s); plus_file(n, k_ext[k], d, sizeof d);
        copy_file(s, d);
    }
    if (slog()) fprintf(stderr, "[slot] Plus: slot %d saved -> maps copied\n", n);
}
static void plus_deleted(int n)
{
    if (!can_save()) return;
    for (int k = 1; k < 3; k++) { char d[1100]; plus_file(n, k_ext[k], d, sizeof d); remove(d); }
    if (slog()) fprintf(stderr, "[slot] Plus: slot %d deleted -> its maps removed\n", n);
}
static void plus_loaded(int n)
{
    if (!can_save() || n < 0 || n >= PLUS_SLOTS) return;
    char p[2][1100]; FILE *f; bool any = false;
    for (int k = 1; k < 3; k++) { plus_file(n, k_ext[k], p[k - 1], sizeof p[k - 1]); if ((f = fopen(p[k - 1], "rb"))) { fclose(f); any = true; } }
    if (!any) { if (slog()) fprintf(stderr, "[slot] Plus: slot %d loaded, it has no maps: current maps kept\n", n); return; }
    gv_map_save(); gv_cave_save();
    backup_autosave();
    for (int k = 1; k < 3; k++) { char d[1100]; slot_file(0, k_ext[k], d, sizeof d); copy_file(p[k - 1], d); }
    gv_map_reset(true); gv_cave_reset(true);
    if (slog()) fprintf(stderr, "[slot] Plus: slot %d loaded -> its maps\n", n);
}
static void plus_frame(void)
{
    size_t sz = 0; const uint8_t *sr = glue_sram_image(&sz);
    if (!sr || sz < PLUS_REC + sizeof s_rec) return;
    uint8_t pending = sms_read8(0xC0A5);
    if (!s_rec_init) { memcpy(s_rec, sr + PLUS_REC, sizeof s_rec); s_last_pending = pending; s_rec_init = true; return; }
    for (int n = 0; n < PLUS_SLOTS; n++) {
        const uint8_t *r = sr + PLUS_REC + 64 * n;
        if (!memcmp(r, s_rec[n], 64)) continue;
        bool was = rec_valid(s_rec[n]), now = rec_valid(r);
        memcpy(s_rec[n], r, 64);
        if (now) plus_saved(n);
        else if (was) plus_deleted(n);
    }
    if (pending == 1 && s_last_pending == 0) plus_loaded(sms_read8(0xC0AA));
    s_last_pending = pending;
}

void gv_slots_init(void)
{
    static bool once; if (once) return; once = true;
    read_last();
}

/* hook $12C8 (the new-game routine: progress cleared, mode 5, screen $B6): observe only */
int gv_new_game(void)
{
    if (slog()) fprintf(stderr, "[slot] new game: the autosave starts empty\n");
    if (can_save()) {
        gv_map_save(); gv_cave_save();
        backup_autosave();
        char p[1100]; slot_file(0, ".password", p, sizeof p); remove(p);
    }
    gv_map_reset(false); gv_cave_reset(false); gv_pw_reload();
    gv_map_save(); gv_cave_save();
    s_active = 0; write_last();
    return 0;
}

/* gv_qol.c gv_pw_made: the game made a password (already written to the autosave) */
void gv_slots_pw_made(void)
{
    if (sms_read8(0xC802) == 0x11 && sms_read8(0xC168) == 3) {
        s_cave_made = true; s_sel = s_active;
        if (slog()) fprintf(stderr, "[slot] Winkle told the password (screen %d), slot %d\n", sms_read8(0xC80D), s_sel);
    }
}

/* gv_qol.c gv_pw_entry: the password to pre-fill (the slot used last, else any); false = none */
bool gv_slots_pw_fill(uint8_t pw[32])
{
    s_pw_screen = true; s_pw_loop_ran = false; s_pw_match = false;
    int order[NSLOT] = { s_last }, k = 1;
    for (int i = 0; i < NSLOT; i++) if (i != s_last) order[k++] = i;
    for (int i = 0; i < NSLOT; i++)
        if (slot_password(order[i], pw)) { s_sel = order[i]; return true; }
    s_sel = -1;
    return false;
}
/* gv_qol.c gv_pw_loop: the password screen's input loop runs (once per frame) */
void gv_slots_pw_loop(void)
{
    s_pw_loop_seen = s_pw_loop_ran = true;
    uint8_t pw[32];
    s_pw_match = s_sel >= 0 && slot_password(s_sel, pw);
    for (int i = 0; i < 32 && s_pw_match; i++) s_pw_match = sms_read8((uint16_t)(0xC170 + i)) == pw[i];
    static int last = -1;
    if (slog() && s_pw_match != last) {
        fprintf(stderr, "[slot] pw loop: match %d (sel %d):", s_pw_match, s_sel);
        for (int i = 0; i < 32; i++) fprintf(stderr, " %02X/%02X", sms_read8((uint16_t)(0xC170 + i)), pw[i]);
        fprintf(stderr, "\n"); last = s_pw_match;
    }
}

static uint8_t extra_buttons(uint64_t frame)
{
    uint8_t m = hud_extra_buttons();
    static const char *e = (const char *)1; if (e == (const char *)1) e = getenv("GV_SLOT_KEYS");   /* test */
    for (const char *p = e; p && *p; ) {
        unsigned long long f = strtoull(p, (char **)&p, 10);
        int k = *p == ':' ? atoi(p + 1) : 0;
        if (f == frame && (k == 1 || k == 2)) m |= (uint8_t)k;
        p = strchr(p, ','); if (p) p++;
    }
    return m;
}
static bool in_winkle(void) { return sms_read8(0xC802) == 0x11 && sms_read8(0xC168) == 3; }

void gv_slots_frame(uint64_t frame)
{
    static uint8_t last_btn;
    uint8_t m = extra_buttons(frame), rise = (uint8_t)(m & ~last_btn); last_btn = m;
    uint8_t mode = sms_read8(0xC802);
    bool on = gv_slots_enabled();
    if (s_plus) plus_frame();

    /* Winkle's cave: pick the slot; leaving = save */
    if (s_cave_made && !in_winkle()) {
        if (slog()) fprintf(stderr, "[slot] frame %llu: mode %02X, content %02X\n", (unsigned long long)frame, mode, sms_read8(0xC168));
        s_cave_made = false;
        if (on && s_sel > 0) save_to(s_sel);
        else if (slog()) fprintf(stderr, "[slot] left Winkle's cave: autosave only\n");
    }
    if (s_cave_made && on && (rise & 3)) {
        s_sel = (s_sel + ((rise & HUD_EXTRA_2) ? 1 : NSLOT - 1)) % NSLOT;
        if (slog()) fprintf(stderr, "[slot] Winkle: slot %d\n", s_sel);
    }

    /* password screen: LB / RB = the previous / next slot with a password */
    bool pw_active = s_pw_loop_seen; s_pw_loop_seen = false;
    if (pw_active && on && (rise & 3) && s_sel >= 0) {
        int d = (rise & HUD_EXTRA_2) ? 1 : NSLOT - 1;
        uint8_t pw[32];
        for (int i = 1; i < NSLOT; i++) {
            int n = (s_sel + d * i) % NSLOT;
            if (!slot_password(n, pw)) continue;
            s_sel = n;
            for (int j = 0; j < 32; j++) sms_write8((uint16_t)(0xC170 + j), pw[j]);
            gv_pw_redraw();
            if (slog()) fprintf(stderr, "[slot] password screen: slot %d\n", n);
            break;
        }
    }
    /* the game started from the password screen: its input loop has stopped and the password start
       ($2028) has set screen L14 (the title keeps $C80D = 0; $C802 is already 6 on the title) */
    static int gone;
    gone = s_pw_screen && s_pw_loop_ran && !pw_active ? gone + 1 : 0;
    if (gone >= 2 && sms_read8(0xC80D) == 0xB4) {
        s_pw_screen = false; gone = 0;
        if (on && s_pw_match && s_sel >= 0) load_from(s_sel);
        else { s_active = 0; if (slog()) fprintf(stderr, "[slot] typed password: maps kept\n"); }
    }
    s_last_mode = mode;
}
void gv_slots_exit(void)
{
    if (s_cave_made && gv_slots_enabled() && s_sel > 0) { s_cave_made = false; save_to(s_sel); }
}

/* one line over the game picture (engine overlay, via gv_map.c) */
static void line(uint32_t *fb, int w, int h, int y, const char *t)
{
    int tw = 6 * (int)strlen(t), x = (256 - tw - 8) / 2;
    if (x < 0) x = 0;
    hud_fill(fb, w, h, x, y, x + tw + 7, y + 13, 0xFF101830u, true);
    hud_fill(fb, w, h, x, y, x + tw + 7, y + 1, 0xFF60D0FFu, false);
    hud_fill(fb, w, h, x, y + 12, x + tw + 7, y + 13, 0xFF60D0FFu, false);
    hud_text(fb, w, h, x + 4, y + 3, t, 0xFFFFE040u);
}
static void describe(int n, char *out, size_t sz)
{
    static const char *const mon[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    int scr; long long t; uint8_t pw[32];
    if (n == 0) { snprintf(out, sz, "AUTOSAVE"); return; }
    if (!slot_password(n, pw)) { snprintf(out, sz, "EMPTY"); return; }
    if (!slot_info(n, &scr, &t)) { snprintf(out, sz, "USED"); return; }
    time_t tt = (time_t)t; struct tm *lt = localtime(&tt);
    char sc[8] = "?";
    if (scr >= 0 && scr < 182) snprintf(sc, sizeof sc, "%c%d", 'A' + scr % 13, scr / 13 + 1);
    if (lt) snprintf(out, sz, "%s  %d %s %02d:%02d", sc, lt->tm_mday, mon[lt->tm_mon], lt->tm_hour, lt->tm_min);
    else snprintf(out, sz, "%s", sc);
}
void gv_slots_overlay(uint32_t *fb, int w, int h)
{
    if (!gv_slots_enabled() || w != 256 || h < 192) return;
    char d[48], t[96];
    if (s_cave_made && in_winkle()) {
        describe(s_sel, d, sizeof d);
        if (s_sel == 0) snprintf(t, sizeof t, "SAVE: AUTOSAVE ONLY  LB/RB = SLOT");
        else snprintf(t, sizeof t, "SAVE TO SLOT %d: %s  LB/RB", s_sel, d);
        line(fb, w, h, 98, t);                               /* between the picture and the text */
    } else if (s_pw_screen && s_pw_loop_ran && s_sel >= 0) {
        describe(s_sel, d, sizeof d);
        snprintf(t, sizeof t, "SLOT %d: %s  LB/RB", s_sel, d);
        line(fb, w, h, 179, t);                              /* below the password */
    }
}
