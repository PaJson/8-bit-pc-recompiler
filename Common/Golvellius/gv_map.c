/*
 * gv_map.c - Golvellius: world map of the screens visited (HUD tab MAP).
 *
 * The overworld is 13 x 14 screens; RAM $C80D = screen index = row * 13 + column (scroll up -13,
 * down +13, left -1, right +1: $2198/$2234/$22CE/$235B). A password starts at 180 ($B4, $13DE /
 * $202A) = L14. Overworld = $C802 6 (after a password continue, $2025) or 7 (New Game / after a
 * cave, $1DD3). Index 182 ($B6) is NOT an overworld screen: it is the side-view vine area where a
 * New Game starts (the "first dungeon": the witch scene in mode 5, then you walk it in mode 7 with
 * the POWER bar at the top; $1309 / $13E9, $1F08 `sub $B6 ; jp z,$1F84` loads its graphics); the
 * screen loader's 183-byte screen -> room table ($1918) has it as the last entry. Not on the map.
 *
 * Capture: whenever the overworld picture has been still for 30 frames (no scroll: $C814 = 0, the
 * palette unchanged = no fade) on a screen, the BACKGROUND layer (no sprites: no hero, no enemies)
 * of the playfield (x 8..247, y 0..191 = 15 x 12 metatiles; the overworld has no status bar, the
 * left column is blanked, the last 8 px are black) is drawn from VRAM / CRAM with the VDP's rules
 * and stored at full size (240 x 192, one SMS colour byte per pixel), refreshed every 60 still
 * frames (animated tiles). "Overworld picture" = mode 6/7 AND reg0 bit 5 (left column blanked,
 * $26): the game sets the mode and the screen while the previous picture is still shown (password
 * continue: mode 6 + screen 180 on the password screen, reg0 $06; its palette animates the cursor).
 * The drawn picture must match the presented frame (>= 85 % of the pixels; sprites cover the rest)
 * and not be (nearly) black. The overview uses 1/4-size cells (60 x 48) made from it. Called from
 * the engine overlay callback (hud_set_overlay) = once per presented frame, window mode only.
 * File <save dir>/<rom>.gvmap: "GVM3" + 182 visited flags + 182 full-size flags + per visited
 * screen the 60 x 48 cell (ARGB) and, if full, a PackBits-packed 240 x 192 picture (u32 length
 * first). GVM1 / GVM2 (62 x 44 cells of the frame incl. sprites, without the top metatile row;
 * GVM2 also has index 182) still load, converted; their screens get the full picture on the next
 * visit. Written when the HUD closes and at exit (not with SMS_NO_SAVE).
 *
 * HUD tab MAP (own Left/Right; LB/RB, Q/E switch tabs): the arrows move a cursor (cyan) over the
 * map, the screen you are on blinks yellow; B / Enter shows the cursor's screen big (arrows = the
 * neighbouring screens), B / Enter again = back to the map. The cursor starts on your screen each
 * time the HUD opens.
 * Test env: GV_MAP_LOG=1 (captures), GV_MAP_DUMP=<ppm> (map image), GV_MAP_PICDUMP=<dir> (every
 * captured picture as picNN.ppm), GV_HUD_KEYS=<U D L R S ...> (keys for the MAP page at its first
 * draw; S = select).
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hud.h"
#include "safe_file.h"
#include "sms_runtime.h"
#include "video/sms_vdp.h"

extern const char *glue_save_path(void);

#define COLS  13
#define ROWS  14
#define NSCR  (COLS * ROWS)        /* 182 */
#define FW    240                  /* full-size playfield: 15 x 12 metatiles at x 8..247, y 0..191 */
#define FH    192
#define CW    60                   /* overview cell = 1/4 */
#define CH    48
#define OLD_CW 62                  /* GVM1/GVM2 cells: x 8..255 (the last 2 columns = the black edge), */
#define OLD_CH 44                  /* y 16..191 (the top metatile row was missing) */
#define MW    (COLS * CW)
#define MH    (ROWS * CH)

static uint32_t s_map[MW * MH];            /* overview (ARGB) */
static uint8_t  s_full[NSCR][FW * FH];     /* full-size pictures: SMS colour --BBGGRR per pixel */
static uint8_t  s_seen[NSCR], s_has_full[NSCR];
static uint32_t s_gen = 1;
static int      s_still, s_last_idx = -1;
static bool     s_dirty, s_loaded;
static int      s_cur = -1;                /* cursor (screen index); -1 = put it on the hero's screen */
static bool     s_zoom;

static bool overworld(void) { uint8_t m = sms_read8(0xC802); return m == 6 || m == 7; }
static bool in_game(void) { return sms_read8(0xC802) >= 5; }
static bool map_log(void) { static int on = -1; if (on < 0) on = getenv("GV_MAP_LOG") != NULL; return on; }
static uint32_t sms_argb(uint8_t c)
{
    return 0xFF000000u | (uint32_t)((c & 3) * 85) << 16 | (uint32_t)((c >> 2 & 3) * 85) << 8 | (uint32_t)((c >> 4 & 3) * 85);
}
static void screen_name(int idx, char *out, size_t n)
{
    snprintf(out, n, "%c%d", 'A' + idx % COLS, idx / COLS + 1);
}

/* ---- PackBits: n 0..127 = n+1 literal bytes, n 129..255 = 257-n copies of the next byte ---- */
static size_t pack(const uint8_t *src, size_t len, uint8_t *dst)
{
    size_t i = 0, o = 0;
    while (i < len) {
        size_t run = 1;
        while (i + run < len && run < 128 && src[i + run] == src[i]) run++;
        if (run >= 2) { dst[o++] = (uint8_t)(257 - run); dst[o++] = src[i]; i += run; continue; }
        size_t lit = 1;                                 /* literal until a run of 2+ starts */
        while (i + lit < len && lit < 128 && !(i + lit + 1 < len && src[i + lit] == src[i + lit + 1])) lit++;
        dst[o++] = (uint8_t)(lit - 1);
        memcpy(dst + o, src + i, lit); o += lit; i += lit;
    }
    return o;
}
static bool unpack(const uint8_t *src, size_t len, uint8_t *dst, size_t out_len)
{
    size_t i = 0, o = 0;
    while (i < len && o < out_len) {
        uint8_t n = src[i++];
        if (n < 128) { size_t c = (size_t)n + 1; if (i + c > len || o + c > out_len) return false; memcpy(dst + o, src + i, c); i += c; o += c; }
        else if (n > 128) { size_t c = 257u - n; if (i >= len || o + c > out_len) return false; memset(dst + o, src[i++], c); o += c; }
    }
    return o == out_len;
}

/* overview cell from the full picture (4 x 4 area average) */
static void make_cell(int idx)
{
    int cx = (idx % COLS) * CW, cy = (idx / COLS) * CH;
    const uint8_t *f = s_full[idx];
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++) {
            unsigned r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 4; dy++)
                for (int dx = 0; dx < 4; dx++) {
                    uint8_t c = f[(y * 4 + dy) * FW + x * 4 + dx];
                    r += (c & 3) * 85; g += (c >> 2 & 3) * 85; b += (c >> 4 & 3) * 85;
                }
            s_map[(size_t)(cy + y) * MW + cx + x] = 0xFF000000u | (r / 16) << 16 | (g / 16) << 8 | (b / 16);
        }
}

static void map_path(char *out, size_t n)
{
    const char *sp = glue_save_path();
    out[0] = 0;
    if (!sp || !sp[0]) return;
    snprintf(out, n, "%s", sp);
    size_t l = strlen(out);
    if (l > 4 && !strcmp(out + l - 4, ".sav")) out[l - 4] = 0;
    strncat(out, ".gvmap", n - strlen(out) - 1);
}
void gv_map_save(void)
{
    if (!s_dirty || getenv("SMS_NO_SAVE")) return;
    char p[1100]; map_path(p, sizeof p);
    if (!p[0]) return;
    FILE *f = safe_fopen_w(p, "wb"); if (!f) return;   /* "<file>.tmp", swapped in when complete */
    static uint8_t buf[FW * FH * 2];
    fwrite("GVM3", 1, 4, f);
    fwrite(s_seen, 1, NSCR, f);
    fwrite(s_has_full, 1, NSCR, f);
    for (int i = 0; i < NSCR; i++) {
        if (!s_seen[i]) continue;
        int cx = (i % COLS) * CW, cy = (i / COLS) * CH;
        for (int y = 0; y < CH; y++) fwrite(&s_map[(size_t)(cy + y) * MW + cx], 4, CW, f);
        if (!s_has_full[i]) continue;
        uint32_t n = (uint32_t)pack(s_full[i], FW * FH, buf);
        fwrite(&n, 4, 1, f); fwrite(buf, 1, n, f);
    }
    if (safe_fclose(f, p)) s_dirty = false;            /* failed: old file kept, written again next time */
}
static void map_load(void)
{
    if (s_loaded) return;
    s_loaded = true;
    for (int i = 0; i < MW * MH; i++) s_map[i] = 0xFF000000u;
    char p[1100]; map_path(p, sizeof p);
    FILE *f = p[0] ? fopen(p, "rb") : NULL;
    if (!f) return;
    char mg[4]; size_t ns = 0; int ver = 0;
    if (fread(mg, 1, 4, f) == 4) {
        if (!memcmp(mg, "GVM3", 4)) { ns = NSCR; ver = 3; }
        else if (!memcmp(mg, "GVM2", 4)) { ns = NSCR + 1; ver = 2; }   /* + index 182 (start area) */
        else if (!memcmp(mg, "GVM1", 4)) { ns = NSCR; ver = 1; }
    }
    uint8_t seen[NSCR + 1] = { 0 }, full[NSCR + 1] = { 0 };
    bool ok = ns && fread(seen, 1, ns, f) == ns && (ver < 3 || fread(full, 1, ns, f) == ns);
    static uint8_t buf[FW * FH * 2];
    for (int i = 0; ok && i < (int)ns; i++) {
        if (!seen[i]) continue;
        bool keep = i < NSCR;
        int cx = (i % COLS) * CW, cy = (i / COLS) * CH;
        if (ver >= 3) {
            for (int y = 0; y < CH && ok; y++) ok = fread(&s_map[(size_t)(cy + y) * MW + cx], 4, CW, f) == CW;
        } else {                                    /* old cell: 62 x 44 of y 16..191 -> rows 4..47 */
            for (int y = 0; y < OLD_CH && ok; y++) {
                uint32_t row[OLD_CW];
                ok = fread(row, 4, OLD_CW, f) == OLD_CW;
                if (ok && keep) memcpy(&s_map[(size_t)(cy + 4 + y) * MW + cx], row, CW * 4);
            }
            for (int y = 0; y < 4 && keep; y++)     /* the missing top row: repeat the first one */
                memcpy(&s_map[(size_t)(cy + y) * MW + cx], &s_map[(size_t)(cy + 4) * MW + cx], CW * 4);
        }
        if (keep) s_seen[i] = 1;
        if (!ok || !full[i]) continue;
        uint32_t n = 0;
        ok = fread(&n, 4, 1, f) == 1 && n <= sizeof buf && fread(buf, 1, n, f) == n && unpack(buf, n, s_full[i], FW * FH);
        s_has_full[i] = ok;
    }
    if (!ok) {                                     /* damaged file: start empty */
        memset(s_seen, 0, sizeof s_seen); memset(s_has_full, 0, sizeof s_has_full);
        for (int i = 0; i < MW * MH; i++) s_map[i] = 0xFF000000u;
    }
    else if (ver < 3) s_dirty = true;              /* write it in the new format */
    fclose(f);
    s_gen++;
}

/* the background layer of the playfield (x 8..247, y 0..191), as the VDP draws it (mode 4: scroll
   registers 8 / 9 and their locks: reg0 bit 6 = top 2 rows not scrolled horizontally, bit 7 = the
   right 8 columns not scrolled vertically) */
static void render_bg(uint8_t *out)
{
    const uint8_t r0 = g_vdp.reg[0];
    int nt = (g_vdp.reg[2] & 0x0E) << 10;
    for (int y = 0; y < FH; y++) {
        int Y = y;
        for (int x = 0; x < FW; x++) {
            int X = x + 8;
            int hs = (r0 & 0x40) && Y < 16 ? 0 : g_vdp.reg[8];
            int vs = (r0 & 0x80) && X >= 192 ? 0 : g_vdp.reg[9];
            int bx = (X - hs) & 0xFF, by = (Y + vs) % 224;
            int a = (nt + ((by >> 3) * 32 + (bx >> 3)) * 2) & 0x3FFF;
            int e = g_vdp.vram[a] | g_vdp.vram[(a + 1) & 0x3FFF] << 8;
            int px = bx & 7, py = by & 7;
            if (e & 0x200) px = 7 - px;
            if (e & 0x400) py = 7 - py;
            const uint8_t *t = &g_vdp.vram[(((e & 0x1FF) * 32) + py * 4) & 0x3FFF];
            int bit = 7 - px;
            int c = (t[0] >> bit & 1) | (t[1] >> bit & 1) << 1 | (t[2] >> bit & 1) << 2 | (t[3] >> bit & 1) << 3;
            out[y * FW + x] = g_vdp.cram[((e & 0x800) ? 16 : 0) + c] & 0x3F;
        }
    }
}

/* engine overlay callback: sees every presented frame (draws nothing) */
static void capture(uint32_t *fb, int w, int h)
{
    if (w != 256 || h < 192) return;
    int idx = sms_read8(0xC80D);
    if (map_log()) {                                   /* every change of screen / mode / scroll */
        static int last = -1; int now = idx | sms_read8(0xC802) << 8 | sms_read8(0xC814) << 16;
        if (now != last) { fprintf(stderr, "[gv_map] screen %d mode %02X scroll %02X\n", idx, sms_read8(0xC802), sms_read8(0xC814)); last = now; }
    }
    /* the palette: a fade changes it every few frames */
    static uint8_t cram[32]; static int cram_still;
    if (memcmp(cram, g_vdp.cram, sizeof cram)) { memcpy(cram, g_vdp.cram, sizeof cram); cram_still = 0; }
    else cram_still++;
    /* the overworld picture is on screen: mode 6/7 and its display setup (left column blanked);
       not the password screen (mode 6 already, reg0 $06) or the start area (index 182) */
    bool still = overworld() && (g_vdp.reg[0] & 0x20) && sms_read8(0xC814) == 0 && idx < NSCR;
    if (!still || idx != s_last_idx) { s_still = 0; s_last_idx = still ? idx : -1; return; }
    /* after 30 still frames with an unchanged palette, then every 60 while staying (animated
       tiles); a (nearly) black picture = still fading: skipped */
    ++s_still;
    if (s_still < 30 || cram_still < 30 || (s_still - 30) % 60) return;
    static uint8_t pic[FW * FH];
    render_bg(pic);
    int match = 0;                                     /* sanity: the drawn picture = the frame */
    for (int y = 0; y < FH; y++)
        for (int x = 0; x < FW; x++)
            match += sms_argb(pic[y * FW + x]) == (fb[(size_t)y * w + x + 8] | 0xFF000000u);
    if (match < FW * FH * 85 / 100) {
        if (map_log()) fprintf(stderr, "[gv_map] screen %d: background does not match the frame (%d %%)\n", idx, match * 100 / (FW * FH));
        return;
    }
    unsigned long long sum = 0;
    for (int i = 0; i < FW * FH; i++) sum += (pic[i] & 3) + (pic[i] >> 2 & 3) + (pic[i] >> 4 & 3);
    if (sum * 85 / (3ull * FW * FH) < 12) return;      /* mean brightness < 12 of 255 */
    if (s_seen[idx] && s_has_full[idx] && !memcmp(s_full[idx], pic, sizeof pic)) return;
    {   static int n; const char *dd = getenv("GV_MAP_PICDUMP");       /* test: every captured picture */
        if (dd) { char fn[600]; snprintf(fn, sizeof fn, "%s/pic%02d.ppm", dd, n++); FILE *pf = fopen(fn, "wb");
            if (pf) { fprintf(pf, "P6\n%d %d\n255\n", FW, FH);
                for (int i = 0; i < FW * FH; i++) { uint32_t c = sms_argb(pic[i]); uint8_t px[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c }; fwrite(px, 1, 3, pf); }
                fclose(pf); } } }
    int ndiff = 0;
    if (map_log() && s_has_full[idx]) for (int i = 0; i < FW * FH; i++) ndiff += s_full[idx][i] != pic[i];
    memcpy(s_full[idx], pic, sizeof pic);
    s_seen[idx] = s_has_full[idx] = 1;
    make_cell(idx);
    s_dirty = true; s_gen++;
    if (map_log()) { char nm[8]; screen_name(idx, nm, sizeof nm); fprintf(stderr, "[gv_map] captured screen %s (index %d, %d pixels changed, %d %% = the frame)\n",
                            nm, idx, ndiff, match * 100 / (FW * FH)); }
}

/* ---- HUD page ---- */
static int hero_screen(void) { int i = in_game() ? sms_read8(0xC80D) : NSCR; return i < NSCR ? i : -1; }

static void cursor_move(int key)
{
    if (s_cur < 0) return;
    int col = s_cur % COLS, row = s_cur / COLS, n = s_cur;
    if (key == HUD_KEY_UP && row > 0) n -= COLS;
    else if (key == HUD_KEY_DOWN && row < ROWS - 1) n += COLS;
    else if (key == HUD_KEY_LEFT && col > 0) n--;
    else if (key == HUD_KEY_RIGHT && col < COLS - 1) n++;
    s_cur = n;
}
/* MAP TELEPORT (gv_qol.c): in the zoomed view of an allowed screen B / Enter asks once more, the
   second press teleports (the HUD closes); an arrow while asking = no (back to the map). */
extern bool gv_tp_allowed(int idx);
extern void gv_tp_request(int idx);
extern bool gv_pw_cave(int idx);
static bool s_tp_ask;
bool gv_map_seen(int idx) { return idx >= 0 && idx < NSCR && s_seen[idx]; }
/* the overview of visited screens (13 x 14 cells of CW x CH, transparent = not visited), for the side panel */
const uint32_t *gv_map_overview(int *w, int *h, uint32_t *gen) { *w = MW; *h = MH; *gen = s_gen; return s_map; }
static void map_key(int key)
{
    if (s_cur < 0) { int h = hero_screen(); s_cur = h >= 0 ? h : 180; }      /* L14 = the password start */
    if (s_tp_ask) {
        s_tp_ask = false;
        if (key == HUD_KEY_SELECT && gv_tp_allowed(s_cur)) { gv_tp_request(s_cur); hud_key(HUD_KEY_CLOSE); }
        else s_zoom = false;
        return;
    }
    if (key == HUD_KEY_SELECT) {
        if (s_zoom && gv_tp_allowed(s_cur)) s_tp_ask = true;
        else s_zoom = !s_zoom;
    }
    else cursor_move(key);
}
void gv_map_hud_closed(void) { s_cur = -1; s_zoom = false; s_tp_ask = false; }

static void frame_rect(float x, float y, float w, float h, float lw, uint32_t col, uint32_t *fb, int fw, int fh)
{
    if (hud_hires()) {
        hud_rect(HUD_SPACE_GAME, x, y, w, lw, col); hud_rect(HUD_SPACE_GAME, x, y + h - lw, w, lw, col);
        hud_rect(HUD_SPACE_GAME, x, y, lw, h, col); hud_rect(HUD_SPACE_GAME, x + w - lw, y, lw, h, col);
    } else {
        hud_fill(fb, fw, fh, (int)x, (int)y, (int)(x + w), (int)y + 1, col, false);
        hud_fill(fb, fw, fh, (int)x, (int)(y + h) - 1, (int)(x + w), (int)(y + h), col, false);
        hud_fill(fb, fw, fh, (int)x, (int)y, (int)x + 1, (int)(y + h), col, false);
        hud_fill(fb, fw, fh, (int)(x + w) - 1, (int)y, (int)(x + w), (int)(y + h), col, false);
    }
}

static void draw_zoom(uint32_t *fb, int w, int h, float bx, float by, float bw, float bh, int idx)
{
    static uint32_t img[FW * FH];
    static int img_idx = -1; static uint32_t img_src_gen, img_gen = 1;
    float s = bw / FW < bh / FH ? bw / FW : bh / FH;
    float dw = FW * s, dh = FH * s, dx = bx + (bw - dw) / 2, dy = by + (bh - dh) / 2;
    hud_fill(fb, w, h, (int)dx - 1, (int)dy - 1, (int)(dx + dw) + 1, (int)(dy + dh) + 1, 0xFF000000u, false);
    if (!s_seen[idx]) {
        const char *t = "NOT VISITED";
        hud_text(fb, w, h, (int)(dx + dw / 2) - 3 * (int)strlen(t), (int)(dy + dh / 2) - 4, t, 0xFFA0A0B0);
        return;
    }
    if (img_idx != idx || img_src_gen != s_gen) {
        if (s_has_full[idx])
            for (int i = 0; i < FW * FH; i++) img[i] = sms_argb(s_full[idx][i]);
        else {                                          /* old file: only the small cell, enlarged */
            int cx = (idx % COLS) * CW, cy = (idx / COLS) * CH;
            for (int y = 0; y < FH; y++)
                for (int x = 0; x < FW; x++) img[y * FW + x] = s_map[(size_t)(cy + y / 4) * MW + cx + x / 4];
        }
        img_idx = idx; img_src_gen = s_gen; img_gen++;
    }
    if (hud_hires()) hud_image(HUD_SPACE_GAME, dx, dy, dw, dh, img, FW, FH, 0, 0, FW, FH, false, img_gen);
    else
        for (int y = 0; y < (int)dh; y++)
            for (int x = 0; x < (int)dw; x++) {
                int X = (int)dx + x, Y = (int)dy + y;
                if (X >= 0 && Y >= 0 && X < w && Y < h) fb[(size_t)Y * w + X] = img[(int)(y / s) * FW + (int)(x / s)];
            }
}

extern bool gv_cave_hud(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1);
static void map_draw(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1)
{
    const uint32_t CYAN = 0xFF60D0FF, GREY = 0xFFA0A0B0, YEL = 0xFFFFE040;
    if (gv_cave_hud(fb, w, h, x0, y0, x1, y1)) return;           /* inside a dungeon: its map (gv_cave.c) */
    {   static int dumped; const char *dp = getenv("GV_MAP_DUMP");     /* test: the map image as PPM */
        if (dp && !dumped) { dumped = 1; FILE *f = fopen(dp, "wb");
            if (f) { fprintf(f, "P6\n%d %d\n255\n", MW, MH);
                for (int i = 0; i < MW * MH; i++) { uint8_t c[3] = { (uint8_t)(s_map[i] >> 16), (uint8_t)(s_map[i] >> 8), (uint8_t)s_map[i] }; fwrite(c, 1, 3, f); }
                fclose(f); } } }
    int hero = hero_screen();
    if (s_cur < 0) s_cur = hero >= 0 ? hero : 180;
    {   static int sent; const char *k = getenv("GV_HUD_KEYS");        /* test: keys for this page */
        if (k && !sent) { sent = 1;
            for (; *k; k++) map_key(*k == 'U' ? HUD_KEY_UP : *k == 'D' ? HUD_KEY_DOWN : *k == 'L' ? HUD_KEY_LEFT :
                                    *k == 'R' ? HUD_KEY_RIGHT : *k == 'S' ? HUD_KEY_SELECT : -1); } }
    int seen = 0;
    for (int i = 0; i < NSCR; i++) seen += s_seen[i];
    char t[64], cn[8], hn[8];
    screen_name(s_cur, cn, sizeof cn);
    if (hero >= 0) screen_name(hero, hn, sizeof hn);
    if (s_zoom) snprintf(t, sizeof t, "SCREEN %s", cn);
    else snprintf(t, sizeof t, "WORLD MAP");
    hud_text(fb, w, h, x0 + 4, y0, t, CYAN);
    snprintf(t, sizeof t, "%d/%d", seen, NSCR);
    hud_text(fb, w, h, x1 - 4 - 6 * (int)strlen(t), y0, t, GREY);

    /* bottom line: cursor / hero screen and what B / Enter does */
    int ly = y1 - 9;
    if (s_zoom && s_tp_ask) {
        snprintf(t, sizeof t, "TELEPORT TO %s? B/ENTER=YES", cn);
        hud_text(fb, w, h, x0 + 4, ly, t, YEL);
    }
    else if (s_zoom) hud_text(fb, w, h, x0 + 4, ly, gv_tp_allowed(s_cur) ? "ARROWS NEXT  B/ENTER TELEPORT"
                                                                     : "ARROWS NEXT SCREEN   B/ENTER MAP", GREY);
    else {
        int x = x0 + 4;
        snprintf(t, sizeof t, "CURSOR %s", cn); hud_text(fb, w, h, x, ly, t, CYAN); x += 6 * ((int)strlen(t) + 2);
        if (hero >= 0) { snprintf(t, sizeof t, "YOU %s", hn); hud_text(fb, w, h, x, ly, t, YEL); }
        const char *z = "B/ENTER ZOOM";
        hud_text(fb, w, h, x1 - 4 - 6 * (int)strlen(z), ly, z, GREY);
    }

    int by0 = y0 + 11, bw = x1 - x0 - 8, bh = ly - 2 - by0 - 1;
    if (s_zoom) { draw_zoom(fb, w, h, x0 + 4, by0, bw, bh, s_cur); return; }

    float s = (float)bw / MW < (float)bh / MH ? (float)bw / MW : (float)bh / MH;
    float dw = MW * s, dh = MH * s, dx = x0 + 4 + (bw - dw) / 2, dy = by0 + (bh - dh) / 2;
    hud_fill(fb, w, h, (int)dx - 1, (int)dy - 1, (int)(dx + dw) + 1, (int)(dy + dh) + 1, 0xFF000000u, false);
    if (hud_hires()) {
        hud_image_alpha(HUD_SPACE_GAME, dx, dy, dw, dh, s_map, MW, MH, s_gen);
        for (int c = 1; c < COLS; c++) hud_rect(HUD_SPACE_GAME, dx + c * CW * s, dy, 0.25f, dh, 0x40FFFFFFu);
        for (int r = 1; r < ROWS; r++) hud_rect(HUD_SPACE_GAME, dx, dy + r * CH * s, dw, 0.25f, 0x40FFFFFFu);
    } else {                                            /* software: nearest into the frame */
        for (int y = 0; y < (int)dh; y++)
            for (int x = 0; x < (int)dw; x++) {
                int X = (int)dx + x, Y = (int)dy + y;
                uint32_t p = s_map[(size_t)(int)(y / s) * MW + (int)(x / s)];
                if ((p >> 24) && X >= 0 && Y >= 0 && X < w && Y < h) fb[(size_t)Y * w + X] = p;
            }
    }
    float cw = CW * s, ch = CH * s;
    for (int i = 0; i < NSCR; i++)                               /* visited password caves: a magenta corner */
        if (s_seen[i] && gv_pw_cave(i)) {
            float px = dx + (i % COLS) * cw, py = dy + (i / COLS) * ch, m = cw * 0.3f;
            if (hud_hires()) hud_rect(HUD_SPACE_GAME, px + cw - m - 0.5f, py + 0.5f, m, m, 0xFFFF40FFu);
            else hud_fill(fb, w, h, (int)(px + cw - m), (int)py + 1, (int)(px + cw) - 1, (int)(py + m), 0xFFFF40FFu, false);
        }
    static unsigned blink; blink++;
    if (hero >= 0 && (blink / 18) % 2 == 0)                      /* the hero's screen, blinking */
        frame_rect(dx + (hero % COLS) * cw + 0.75f, dy + (hero / COLS) * ch + 0.75f, cw - 1.5f, ch - 1.5f, 0.75f, YEL, fb, w, h);
    frame_rect(dx + (s_cur % COLS) * cw - 0.5f, dy + (s_cur / COLS) * ch - 0.5f, cw + 1, ch + 1, 0.75f, CYAN, fb, w, h);
}

void gv_map_init(void)
{
    static bool pages;
    map_load();
    hud_set_overlay(capture);
    if (!pages) { hud_add_page_ex("MAP", map_draw, map_key, true); pages = true; atexit(gv_map_save); }
}
