/*
 * ps_items.c - Phantasy Star (SMS): HUD page ITEMS - sort and rearrange the inventory.
 *
 * Only the ORDER of the 24 inventory slots changes (RAM $C4C0-$C4D7, count $C4E2), never which items
 * there are, so a save stays a normal save for the original game. Equipped items are not in the
 * inventory (EQUIP swaps them into the character's weapon/armour/shield bytes, ps1.asm ~5540).
 *
 * When is it safe? The game keeps a pointer to an inventory slot while its item list is open
 * ($C29B, LABEL_34D5) and the dungeon handler runs battles and the player menu inside mode $0B, so
 * the game mode alone does not tell. Three observing [insn_hooks] (Tools/ps_find_hooks.py group
 * "items"; they never change the game) mark the frame wait of the walking handlers:
 *   ps_items_walk_mark  at GameMode_Map `ld a,$0E` and GameMode_Dungeon `ld a,$08` (right before
 *                       their `call WaitForVInt`)
 *   ps_items_vwait      at WaitForVInt (`ld ($C208),a`): "this wait comes from a walking handler"
 * Editing is allowed when the HUD opens during such a wait. The new order is applied by the
 * WaitForVInt hook at the next walking wait (a point where no game code uses the inventory), and
 * only if the inventory still holds the same items as when the HUD opened.
 *
 * Names: the game's own (ROM table ItemNames, found by its first entries; English ROMs) or our
 * names from ps_item_names.txt (game folder, else Common/PhantasyStar/, or $PS_ITEM_NAMES); toggled
 * on the page, ini key item_names. HUD only - the game's menus keep the ROM names.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "hud.h"

extern uint8_t  sms_read8(uint16_t addr);
extern void     sms_write8(uint16_t addr, uint8_t val);
extern uint8_t *glue_rom_image(size_t *size);

#define RAM_INV      0xC4C0
#define RAM_INV_N    0xC4E2
#define INV_MAX      24
#define N_ITEMS      0x41

int  g_ps_item_custom_names;                    /* option (ps_qol.c ini item_names) */

/* ---- names ---- */
static char  s_rom_name[N_ITEMS][9];
static bool  s_rom_ok;
static char *s_custom[N_ITEMS];

static void load_rom_names(void)
{
    s_rom_ok = false;
    size_t n = 0; const uint8_t *rom = glue_rom_image(&n);
    static const char sig[] = "        WOODCANE";
    for (size_t i = 0; rom && i + 16 + N_ITEMS * 8 <= n; i++)
        if (!memcmp(rom + i, sig, 16)) {
            for (int k = 0; k < N_ITEMS; k++) {
                const uint8_t *e = rom + i + k * 8; int m = 0;
                while (m < 8 && e[m] != 0x65 && e[m] >= 0x20 && e[m] < 0x7F) { s_rom_name[k][m] = (char)e[m]; m++; }
                while (m > 0 && s_rom_name[k][m - 1] == ' ') m--;
                s_rom_name[k][m] = 0;
            }
            s_rom_ok = true;
            return;
        }
}

static void load_custom_names(void)
{
    for (int k = 0; k < N_ITEMS; k++) { free(s_custom[k]); s_custom[k] = NULL; }
    const char *paths[3] = { getenv("PS_ITEM_NAMES"), "ps_item_names.txt", "../../Common/PhantasyStar/ps_item_names.txt" };
    FILE *f = NULL;
    for (int i = 0; i < 3 && !f; i++) if (paths[i]) f = fopen(paths[i], "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof line, f)) {
        char *p = line; while (*p == ' ' || *p == '\t') p++;
        if (*p == ';' || *p == '#' || !*p || *p == '\n') continue;
        if (*p == '$') p++; else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
        char *e; long id = strtol(p, &e, 16);
        if (e == p || id <= 0 || id >= N_ITEMS) continue;
        p = e; while (*p == ' ' || *p == '\t' || *p == '=' || *p == ':') p++;
        size_t l = strcspn(p, "\r\n;"); while (l && (p[l - 1] == ' ' || p[l - 1] == '\t')) l--;
        if (!l) continue;
        if (l > 24) l = 24;
        s_custom[id] = malloc(l + 1);
        if (s_custom[id]) { memcpy(s_custom[id], p, l); s_custom[id][l] = 0; }
    }
    fclose(f);
}

static const char *item_name(uint8_t id)
{
    static char buf[16];
    if (id < N_ITEMS) {
        if (g_ps_item_custom_names && s_custom[id]) return s_custom[id];
        if (s_rom_ok && s_rom_name[id][0]) return s_rom_name[id];
        if (s_custom[id]) return s_custom[id];
    }
    snprintf(buf, sizeof buf, "Item $%02X", id);
    return buf;
}

/* type for colours / the type sort: ids are grouped by type in the ROM (ps1.constants.asm ItemID_*) */
static int item_type(uint8_t id)
{
    if (id >= 0x01 && id <= 0x0F) return 0;   /* weapons */
    if (id >= 0x10 && id <= 0x18) return 1;   /* armour */
    if (id >= 0x19 && id <= 0x20) return 2;   /* shields */
    if (id >= 0x21 && id <= 0x23) return 3;   /* vehicles */
    if (id >= 0x24 && id <= 0x2C) return 4;   /* items to use */
    return 5;                                 /* keys / quest items */
}
static const uint32_t k_type_col[6] = { 0xFFF0A060u, 0xFF80B8F0u, 0xFF80E090u, 0xFFC8A0F0u, 0xFFFFFFFFu, 0xFFF0E070u };

/* ---- safety + staged apply (hooks) ---- */
static int     s_mark, s_safe;                  /* see the header */
static uint8_t s_base[INV_MAX], s_work[INV_MAX];
static int     s_n;                             /* items in s_base / s_work (0 = page not loaded) */
static bool    s_staged, s_editable;
static int     s_applied, s_dropped;            /* counters (test output) */

static bool same_items(const uint8_t *a, const uint8_t *b, int n)
{
    int ca[256] = { 0 };
    for (int i = 0; i < n; i++) { ca[a[i]]++; ca[b[i]]--; }
    for (int i = 0; i < 256; i++) if (ca[i]) return false;
    return true;
}

static unsigned s_nmark, s_nwait, s_nsafe;   /* counters (PS_ITEMS_LOG) */
extern void ps_battle_walking(void);           /* ps_qol.c: walking again = the battle is over */
int ps_items_walk_mark(void) { s_mark = 1; s_nmark++; ps_battle_walking(); return 0; }
int ps_items_vwait(void)
{
    s_safe = s_mark; s_mark = 0; s_nwait++; s_nsafe += s_safe;
    if (s_staged && s_safe) {
        s_staged = false;
        uint8_t cur[INV_MAX]; int n = sms_read8(RAM_INV_N);
        for (int i = 0; i < INV_MAX; i++) cur[i] = sms_read8((uint16_t)(RAM_INV + i));
        if (n == s_n && n <= INV_MAX && !memcmp(cur, s_base, (size_t)n)) {
            for (int i = 0; i < n; i++) sms_write8((uint16_t)(RAM_INV + i), s_work[i]);
            s_applied++;
            if (getenv("PS_ITEMS_LOG")) fprintf(stderr, "[items] new order applied (%d items)\n", n);
        } else {
            s_dropped++;
            fprintf(stderr, "[items] inventory changed before the new order could be applied: not applied\n");
        }
    }
    return 0;
}

/* ---- page ---- */
enum { ACT_SORT_TYPE = INV_MAX, ACT_SORT_NAME, ACT_NAMES, N_POS };
static int  s_cur;                              /* 0..23 slot, or an ACT_ */
static int  s_held = -1;                        /* slot being moved */
static void (*s_names_changed)(void);

void ps_items_set_names_changed(void (*f)(void)) { s_names_changed = f; }

/* (re)load the page state when the HUD opens */
void ps_items_hud_opened(void)
{
    s_editable = s_safe != 0;
    if (getenv("PS_ITEMS_LOG")) fprintf(stderr, "[items] HUD opened: %s (marks %u, waits %u, walking waits %u)\n",
                                        s_editable ? "walking (editable)" : "view only", s_nmark, s_nwait, s_nsafe);
    s_held = -1;
    if (s_staged) return;                       /* edits from the last opening are still pending */
    s_n = sms_read8(RAM_INV_N); if (s_n > INV_MAX) s_n = INV_MAX;
    for (int i = 0; i < INV_MAX; i++) s_base[i] = s_work[i] = sms_read8((uint16_t)(RAM_INV + i));
    if (s_cur < INV_MAX && s_cur >= s_n) s_cur = s_n ? s_n - 1 : ACT_SORT_TYPE;
}

static void changed(void)
{
    s_staged = memcmp(s_work, s_base, (size_t)s_n) != 0;
}

static int cmp_type(const void *a, const void *b) { return (int)*(const uint8_t *)a - (int)*(const uint8_t *)b; }
static int cmp_name(const void *a, const void *b)
{
    char x[32], y[32]; uint8_t ia = *(const uint8_t *)a, ib = *(const uint8_t *)b;
    snprintf(x, sizeof x, "%s", item_name(ia)); snprintf(y, sizeof y, "%s", item_name(ib));
    for (char *p = x; *p; p++) *p = (char)tolower((unsigned char)*p);
    for (char *p = y; *p; p++) *p = (char)tolower((unsigned char)*p);
    int c = strcmp(x, y);
    return c ? c : (int)ia - (int)ib;
}

static void move_held(int to)
{
    if (s_held < 0 || to < 0 || to >= s_n || to == s_held) return;
    uint8_t v = s_work[s_held];
    if (to > s_held) memmove(s_work + s_held, s_work + s_held + 1, (size_t)(to - s_held));
    else memmove(s_work + to + 1, s_work + to, (size_t)(s_held - to));
    s_work[to] = v; s_held = to; changed();
}

/* cursor movement: two columns of 12 slots (left 0-11, right 12-23), the action row below */
static int step(int cur, int key)
{
    int n = s_n;
    if (cur >= INV_MAX) {                        /* action row */
        if (key == HUD_KEY_LEFT)  return cur > ACT_SORT_TYPE ? cur - 1 : cur;
        if (key == HUD_KEY_RIGHT) return cur < ACT_NAMES ? cur + 1 : cur;
        if (key == HUD_KEY_UP)    return n ? (n > 12 ? 11 : n - 1) : cur;
        return cur;
    }
    int col = cur / 12, row = cur % 12;
    if (key == HUD_KEY_UP)   return row > 0 ? cur - 1 : cur;
    if (key == HUD_KEY_DOWN) { if (cur + 1 < n && row < 11) return cur + 1; return s_held >= 0 ? cur : ACT_SORT_TYPE; }
    if (key == HUD_KEY_LEFT  && col == 1) return cur - 12;
    if (key == HUD_KEY_RIGHT && col == 0) { int t = cur + 12; return t < n ? t : (n > 12 ? n - 1 : cur); }
    return cur;
}

void ps_items_hud_key(int key)
{
    if (key == HUD_KEY_SELECT) {
        if (s_cur == ACT_NAMES) { g_ps_item_custom_names ^= 1; if (s_names_changed) s_names_changed(); return; }
        if (!s_editable) return;
        if (s_cur == ACT_SORT_TYPE || s_cur == ACT_SORT_NAME) {
            qsort(s_work, (size_t)s_n, 1, s_cur == ACT_SORT_TYPE ? cmp_type : cmp_name);
            changed(); return;
        }
        if (s_cur < s_n) s_held = s_held >= 0 ? -1 : s_cur;
        return;
    }
    int to = step(s_cur, key);
    if (s_held >= 0) { if (to < INV_MAX) { move_held(to); s_cur = s_held; } return; }
    s_cur = to;
}

void ps_items_hud_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1)
{
    const uint32_t GREY = 0xFFA0A0B0, CYAN = 0xFF60D0FF, YEL = 0xFFFFE040, DIM = 0xFF606878, WHITE = 0xFFFFFFFF;
    char buf[48];
    snprintf(buf, sizeof buf, "INVENTORY %d/%d", s_n, INV_MAX);
    hud_text(fb, w, h, x0 + 4, y0, buf, CYAN);
    /* status / hint, right of the title */
    const char *st = !s_editable ? "VIEW ONLY: WALK FIRST" : s_held >= 0 ? "MOVE, CONFIRM = DROP"
                   : s_staged ? "CHANGED" : "CONFIRM = PICK UP";
    hud_text(fb, w, h, x1 - 4 - 6 * (int)strlen(st), y0, st, (s_staged && s_held < 0) || s_held >= 0 ? YEL : GREY);
    int cw = (x1 - x0 - 8) / 2, ty0 = y0 + 12;
    for (int i = 0; i < INV_MAX; i++) {
        int x = x0 + 4 + (i / 12) * cw, y = ty0 + (i % 12) * 9;
        bool sel = i == s_cur, held = i == s_held;
        if (sel) hud_fill(fb, w, h, x - 2, y - 1, x + cw - 4, y + 8, held ? 0xFF805010u : 0xFF304060u, false);
        if (i >= s_n) { hud_text(fb, w, h, x + 2, y, "-", DIM); continue; }
        uint8_t id = s_work[i];
        snprintf(buf, sizeof buf, "%.*s", (cw - 6) / 6, item_name(id));
        hud_fill(fb, w, h, x, y + 1, x + 2, y + 6, k_type_col[item_type(id)], false);   /* type mark */
        hud_text(fb, w, h, x + 5, y, buf, held ? YEL : s_editable ? WHITE : GREY);
    }
    int ay = ty0 + 12 * 9 + 3, ax = x0 + 4;
    static const char *const acts[3] = { "SORT: TYPE", "SORT: NAME", NULL };
    for (int a = 0; a < 3; a++) {
        const char *t = acts[a] ? acts[a] : g_ps_item_custom_names ? "CUSTOM NAMES" : "GAME NAMES";
        int tw = 6 * (int)strlen(t);
        bool sel = s_cur == ACT_SORT_TYPE + a;
        if (sel) hud_fill(fb, w, h, ax - 2, ay - 1, ax + tw + 2, ay + 8, 0xFF304060u, false);
        hud_text(fb, w, h, ax, ay, t, (a < 2 && !s_editable) ? DIM : sel ? YEL : WHITE);
        ax += tw + 12;
    }
    (void)y1;
}

void ps_items_init(void)
{
    load_rom_names();
    load_custom_names();
    s_staged = false; s_n = 0; s_cur = 0; s_held = -1; s_mark = s_safe = 0;
}
