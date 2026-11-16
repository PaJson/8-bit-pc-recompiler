/*
 * gv_text.c - Golvellius text options built from Alianger's IPS files at run time (user decision
 * 2026-11-16: three separate toggles):
 *   FREE CRYSTALS : the crystal seller's text + the 7 crystal prices ("free crystals.ips", or the
 *                   combined "free crystals+NPC text+typos.ips")
 *   TEXT FIXES    : typo / fact fixes of the combined file: texts $03 $0E $1F $20 $24 $25 $42 + the
 *                   credits' "DESGINED" (bank 15 $BC5B/$BC5C)
 *   NEW HINTS     : Alianger's rewritten hints: texts $0B $19 $1C $43 $45 $46 $49 $4A $4C $50 $5B $5C $62
 * Nothing of the files is stored in the project; they are read from the user's ROMs/Patches (gv_patch.c).
 *
 * The cave texts (bank 7): pointer table $967C ($66 entries, 0 = none), texts $9748..$B7BF, completely
 * full in the original. Interpreter $8EC8 (entered at $8EA0 with A = $C168 = the text code): bytes
 * < $F0 = characters, $FF = new line, commands $F0 new page, $F1 wait, $F2 a b yes/no, $F4 w,
 * $F5 end, $F6 w call (return address -> $C169), $F7 return, $F8 gold, $F9 w if NO goto, $FA w the
 * cave's deal / else goto, $FB boss name, $FC price, $FD w / $FE w conditional gotos. Operands of
 * $F6 $F9 $FA $FD $FE are text addresses (some point outside the area, e.g. Winkle's `$FA $0000`:
 * left as they are). No code reads the texts other than through the table (checked: no `ld hl/de`
 * into the area).
 * Because the texts do not fit with every combination, they are re-packed: every text is taken from
 * the original (O) or the patched image (P), its units (parse ranges, plus the units its jumps reach)
 * are copied to a new layout and every address operand is relocated. Two layouts: A = all texts but
 * the crystal seller's ($2A), B = the seller's alone (it is only shown in crystal caves, where no
 * other text appears). Hook bank 7 $8EA4 (gv_text_start, right after the code is read) writes B
 * before text $2A and A before every other text. Checked (Python prototype, all 8 combinations):
 * every text renders exactly like its source, following its jumps.
 * Test env: GV_TEXT_DUMP=<dir> writes textA.bin / textB.bin (bank 7, 16 KB) after every build.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sms_runtime.h"
#include "gv_patch.h"

extern uint8_t *glue_rom_image(size_t *size);

#define B7    (7 * 0x4000)
#define TBL   0x967C
#define NT    0x66
#define LO    0x9748
#define HI    0xB7C0
#define SELLER 0x2A
#define PRICE_LO 0x1CD87
#define PRICE_HI 0x1CE10
#define CREDIT0 0x3FC5B

static GvPatch s_all = { "Golvellius free crystals+NPC text+typos.ips", 0xFFD04942u, 4689 };
static GvPatch s_cry = { "Golvellius free crystals.ips", 0x0C3E537Bu, 288 };
static uint8_t s_o[0x4000], s_p[0x4000];         /* bank 7: original, patched */
static uint8_t s_cred_o[2], s_cred_p[2];
static bool    s_init, s_have_p;
static uint8_t s_a[0x4000], s_b[0x4000];          /* bank 7 with layout A / B */
static bool    s_built;                           /* A / B valid (some option on) */
static int     s_cur = -1;                        /* layout in the ROM image: -1 original, 0 A, 1 B */

static const uint8_t k_fix[]  = { 0x03, 0x0E, 0x1F, 0x20, 0x24, 0x25, 0x42 };
static const uint8_t k_hint[] = { 0x0B, 0x19, 0x1C, 0x43, 0x45, 0x46, 0x49, 0x4A, 0x4C, 0x50, 0x5B, 0x5C, 0x62 };

static bool tlog(void) { static int on = -1; if (on < 0) on = getenv("GV_LOG") != NULL; return on; }
static unsigned rd(const uint8_t *b, unsigned a) { return b[a - 0x8000] | b[a - 0x8000 + 1] << 8; }
static int opn(int c) { return c == 0xF2 || c == 0xF4 || c == 0xF6 || c == 0xF9 || c == 0xFA || c == 0xFD || c == 0xFE ? 2 : 0; }
static bool is_addr(int c) { return c == 0xF6 || c == 0xF9 || c == 0xFA || c == 0xFD || c == 0xFE; }

/* walk one text from a: calls f(operand address, target) for every address operand inside the area;
   returns the end (after $F5 / $F7) */
typedef void (*RefFn)(void *ctx, unsigned at, unsigned target);
static unsigned walk(const uint8_t *b, unsigned a, RefFn f, void *ctx)
{
    while (a < HI) {
        int c = b[a - 0x8000]; a++;
        if (c >= 0xF0 && c != 0xFF) {
            if (is_addr(c)) { unsigned t = rd(b, a); if (t >= LO && t < HI && f) f(ctx, a, t); }
            a += (unsigned)opn(c);
            if (c == 0xF5 || c == 0xF7) break;
        }
    }
    return a > HI ? HI : a;
}

/* ---- layout builder ---- */
typedef struct { int img; unsigned s, e; } Unit;
typedef struct { int img; unsigned s, e, pos; } Range;
#define MAXU 600
static Unit  s_u[MAXU]; static int s_nu;
static Range s_r[MAXU]; static int s_nr;
static const uint8_t *img_b(int img) { return img ? s_p : s_o; }

static void add_unit(int img, unsigned s);
static void ref_add(void *ctx, unsigned at, unsigned t) { (void)at; add_unit(*(int *)ctx, t); }
static void add_unit(int img, unsigned s)
{
    if (s < LO || s >= HI) return;
    for (int i = 0; i < s_nu; i++) if (s_u[i].img == img && s_u[i].s == s) return;
    if (s_nu >= MAXU) return;
    int k = s_nu++;
    s_u[k].img = img; s_u[k].s = s; s_u[k].e = s;
    s_u[k].e = walk(img_b(img), s, NULL, NULL);
    walk(img_b(img), s, ref_add, &img);
}
static int cmp_unit(const void *a, const void *b)
{
    const Unit *x = a, *y = b;
    if (x->img != y->img) return x->img - y->img;
    return x->s < y->s ? -1 : x->s > y->s;
}
static int cmp_range(const void *a, const void *b)
{
    const Range *x = a, *y = b;
    if (x->s != y->s) return x->s < y->s ? -1 : 1;
    return x->img - y->img;
}
static long map_addr(int img, unsigned a)
{
    for (int i = 0; i < s_nr; i++) if (s_r[i].img == img && a >= s_r[i].s && a < s_r[i].e) return (long)(s_r[i].pos + a - s_r[i].s);
    return -1;
}
typedef struct { int img; uint8_t *out; bool ok; } FixCtx;
static void ref_fix(void *ctx, unsigned at, unsigned t)
{
    FixCtx *c = ctx;
    long na = map_addr(c->img, at), nt = map_addr(c->img, t);
    if (na < 0 || nt < 0) { c->ok = false; return; }
    c->out[na - 0x8000] = (uint8_t)nt; c->out[na - 0x8000 + 1] = (uint8_t)(nt >> 8);
}
/* out = bank 7 with the texts `roots` (src[k] 0 = original, 1 = patched) packed into the area and their
   table entries set; the other entries keep the values in out */
static bool build(uint8_t *out, const int *src, const int *roots, int nroots)
{
    s_nu = 0; s_nr = 0;
    for (int i = 0; i < nroots; i++) { int k = roots[i]; add_unit(src[k], rd(img_b(src[k]), TBL + 2 * k)); }
    qsort(s_u, (size_t)s_nu, sizeof s_u[0], cmp_unit);
    for (int i = 0; i < s_nu; i++) {
        if (s_nr && s_r[s_nr - 1].img == s_u[i].img && s_u[i].s <= s_r[s_nr - 1].e) {
            if (s_u[i].e > s_r[s_nr - 1].e) s_r[s_nr - 1].e = s_u[i].e;
        } else { s_r[s_nr].img = s_u[i].img; s_r[s_nr].s = s_u[i].s; s_r[s_nr].e = s_u[i].e; s_nr++; }
    }
    qsort(s_r, (size_t)s_nr, sizeof s_r[0], cmp_range);
    unsigned pos = LO;
    for (int i = 0; i < s_nr; i++) { s_r[i].pos = pos; pos += s_r[i].e - s_r[i].s; }
    if (pos > HI) { fprintf(stderr, "[gv_text] texts do not fit (%u of %u bytes)\n", pos - LO, HI - LO); return false; }
    memset(out + (LO - 0x8000), 0xFF, HI - LO);
    for (int i = 0; i < s_nr; i++)
        memcpy(out + (s_r[i].pos - 0x8000), img_b(s_r[i].img) + (s_r[i].s - 0x8000), s_r[i].e - s_r[i].s);
    FixCtx c = { 0, out, true };
    for (int i = 0; i < s_nu; i++) { c.img = s_u[i].img; walk(img_b(s_u[i].img), s_u[i].s, ref_fix, &c); }
    for (int i = 0; i < nroots && c.ok; i++) {
        int k = roots[i]; long v = map_addr(src[k], rd(img_b(src[k]), TBL + 2 * k));
        if (v < 0) { c.ok = false; break; }
        out[TBL - 0x8000 + 2 * k] = (uint8_t)v; out[TBL - 0x8000 + 2 * k + 1] = (uint8_t)(v >> 8);
    }
    if (!c.ok) fprintf(stderr, "[gv_text] relocation failed\n");
    if (tlog()) fprintf(stderr, "[gv_text] %d texts, %d units, %u of %u bytes\n", nroots, s_nu, pos - LO, HI - LO);
    return c.ok;
}

/* ---- options ---- */
void gv_text_init(void)
{
    if (s_init) return;
    size_t n = 0; uint8_t *rom = glue_rom_image(&n);
    if (!rom || n < 0x40000) return;
    s_init = true;
    memcpy(s_o, rom + B7, 0x4000);                 /* before any option touched it */
    s_cred_o[0] = rom[CREDIT0]; s_cred_o[1] = rom[CREDIT0 + 1];
    gv_patch_init(&s_all); gv_patch_init(&s_cry);
    GvPatch *p = s_all.avail ? &s_all : s_cry.avail ? &s_cry : NULL;
    memcpy(s_p, s_o, 0x4000); memcpy(s_cred_p, s_cred_o, 2);
    if (p) {
        for (int k = 0; k < p->n; k++) {
            GvPatchRec *r = &p->rec[k];
            for (uint32_t i = 0; i < r->len; i++) {
                uint32_t o = r->off + i;
                if (o >= B7 && o < B7 + 0x4000) s_p[o - B7] = r->neu[i];
                else if (o == CREDIT0 || o == CREDIT0 + 1) s_cred_p[o - CREDIT0] = r->neu[i];
            }
        }
        s_have_p = true;
    }
}
/* 0 = FREE CRYSTALS (either file), 1 = TEXT FIXES, 2 = NEW HINTS (the combined file) */
bool gv_text_avail(int which) { return which == 0 ? (s_all.avail || s_cry.avail) : s_all.avail; }

static void put_layout(int which)
{
    size_t n = 0; uint8_t *rom = glue_rom_image(&n);
    if (!rom || n < B7 + 0x4000) return;
    const uint8_t *src = which < 0 ? s_o : which == 0 ? s_a : s_b;
    memcpy(rom + B7 + (TBL - 0x8000), src + (TBL - 0x8000), HI - TBL);   /* table + texts */
    s_cur = which;
}
static void dump(void)
{
    const char *d = getenv("GV_TEXT_DUMP"); if (!d) return;
    char p[1024]; FILE *f;
    snprintf(p, sizeof p, "%s/textA.bin", d); if ((f = fopen(p, "wb"))) { fwrite(s_a, 1, 0x4000, f); fclose(f); }
    snprintf(p, sizeof p, "%s/textB.bin", d); if ((f = fopen(p, "wb"))) { fwrite(s_b, 1, 0x4000, f); fclose(f); }
}

void gv_text_set(bool cry, bool fix, bool hint)
{
    size_t n = 0; uint8_t *rom = glue_rom_image(&n);
    if (!s_init || !rom) return;
    if (!s_all.avail) fix = hint = false;
    if (!s_have_p) cry = false;
    /* crystal prices (bank 7 $8D87..) and the credits' typo */
    for (unsigned o = PRICE_LO; o < PRICE_HI; o++) rom[o] = cry ? s_p[o - B7] : s_o[o - B7];
    rom[CREDIT0] = fix ? s_cred_p[0] : s_cred_o[0]; rom[CREDIT0 + 1] = fix ? s_cred_p[1] : s_cred_o[1];
    if (!cry && !fix && !hint) { s_built = false; put_layout(-1); if (tlog()) fprintf(stderr, "[gv_text] original texts\n"); return; }
    int src[NT] = { 0 }, roots[NT], nr = 0;
    for (unsigned i = 0; i < sizeof k_fix; i++) src[k_fix[i]] = fix;
    for (unsigned i = 0; i < sizeof k_hint; i++) src[k_hint[i]] = hint;
    src[SELLER] = cry;
    memcpy(s_a, s_o, 0x4000); memcpy(s_a + (PRICE_LO - B7), rom + PRICE_LO, PRICE_HI - PRICE_LO);
    for (int k = 1; k < NT; k++) if (k != SELLER) roots[nr++] = k;
    bool ok = build(s_a, src, roots, nr);
    memcpy(s_b, s_a, 0x4000);
    int r2[1] = { SELLER };
    ok = ok && build(s_b, src, r2, 1);
    if (!ok) { s_built = false; put_layout(-1); fprintf(stderr, "[gv_text] options could not be applied: original texts\n"); return; }
    s_built = true;
    put_layout(0);
    dump();
    if (tlog()) fprintf(stderr, "[gv_text] free crystals %d, text fixes %d, new hints %d\n", cry, fix, hint);
}

/* [insn_hooks] bank 7 $8EA4 (`ld de,$967C`, after `$8EA0 add a,a` of the code in $C168): the layout for
   this text. Observe only. */
int gv_text_start(void)
{
    if (s_built) {
        int want = sms_read8(0xC168) == SELLER ? 1 : 0;
        if (want != s_cur) { put_layout(want); if (tlog()) fprintf(stderr, "[gv_text] layout %c\n", want ? 'B' : 'A'); }
    }
    return 0;
}
/* bank 7 as it is outside crystal caves (layout A), or NULL = the ROM's own */
const uint8_t *gv_text_bank7(void) { return s_built ? s_a : NULL; }
/* every frame: outside a crystal cave the ROM holds layout A (the HUD reads advice texts from it) */
void gv_text_frame(void)
{
    if (s_built && s_cur == 1 && !(sms_read8(0xC802) == 0x11 && sms_read8(0xC168) == SELLER)) put_layout(0);
}
