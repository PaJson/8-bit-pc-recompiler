/*
 * gv_patch.c - options that apply parts of the user's OWN IPS files at run time.
 *
 * Some of Alianger's Golvellius hacks are pure content (level edits, rewritten texts), not code we
 * could write ourselves. User decision 2026-11-15: such an option reads the IPS file from the
 * user's ROMPatches folder at start-up and, while the option is on, writes its bytes into the
 * in-memory ROM image (the ROM file itself is never touched; switching off restores the original
 * bytes). Nothing from the patches is stored in this project. Without the file the option shows
 * "FILE MISSING" and stays off.
 *
 * Only DATA records may be applied this way: the recompiled code was compiled from the original ROM,
 * so changed code bytes would have no effect (they would only reach the interpreter fallback).
 * Every record of the files used here was checked against the generated code (Tools ... scratchpad
 * ips_vs_code.py, 2026-11-15): text, tables and cave layouts the game reads at run time.
 *
 * Finding the files: by CONTENT (CRC32 + size of the IPS file), so renamed files still work; the
 * authors' original names are kept in the folders (credits: CREDITS.md). Searched: $GV_PATCH_DIR,
 * else "Patches" next to the ROM or in up to three folders above it (ROMs/Patches/ for ROMs/x.sms
 * and ROMs/Patched/x.sms), every *.ips up to three folder levels deep.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "gv_patch.h"

extern uint8_t    *glue_rom_image(size_t *size);
extern const char *glue_rom_path(void);

static uint32_t crc32_of(const uint8_t *d, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) { c ^= d[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
static bool file_matches(const char *path, uint32_t crc, long size)
{
    FILE *f = fopen(path, "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    bool ok = false;
    if (sz == size && sz > 0) {
        uint8_t *b = (uint8_t *)malloc((size_t)sz);
        ok = b && fread(b, 1, (size_t)sz, f) == (size_t)sz && crc32_of(b, (size_t)sz) == crc;
        free(b);
    }
    fclose(f);
    return ok;
}
static bool is_ips_name(const char *n)
{
    size_t l = strlen(n);
    return l > 4 && n[l - 4] == '.' && (n[l - 3] | 32) == 'i' && (n[l - 2] | 32) == 'p' && (n[l - 1] | 32) == 's';
}
/* any *.ips with this CRC32 + size in dir or below (depth levels) */
static bool find_in(const char *dir, uint32_t crc, long size, char *out, size_t n, int depth)
{
    DIR *d = opendir(dir); if (!d) return false;
    struct dirent *e; bool ok = false;
    char sub[24][512]; int nsub = 0;
    while (!ok && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char p[1400]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (is_ips_name(e->d_name)) { if (file_matches(p, crc, size)) { snprintf(out, n, "%s", p); ok = true; } }
        else if (depth > 0 && nsub < 24 && strlen(p) < 512) { DIR *t = opendir(p); if (t) { closedir(t); snprintf(sub[nsub++], 512, "%s", p); } }
    }
    closedir(d);
    for (int i = 0; i < nsub && !ok; i++) ok = find_in(sub[i], crc, size, out, n, depth - 1);
    return ok;
}
static bool find_patch(const GvPatch *p, char *out, size_t n)
{
    const char *env = getenv("GV_PATCH_DIR");
    if (env && env[0]) return find_in(env, p->crc, p->size, out, n, 3);
    char dir[1024]; const char *rp = glue_rom_path();
    snprintf(dir, sizeof dir, "%s", rp && rp[0] ? rp : ".");
    for (int up = 0; up < 4; up++) {                 /* the ROM's folder, then its parents */
        char *s = NULL; for (char *q = dir; *q; q++) if (*q == '/' || *q == '\\') s = q;
        if (s) *s = 0; else snprintf(dir, sizeof dir, ".");
        char base[1100]; snprintf(base, sizeof base, "%s/Patches", dir);
        if (find_in(base, p->crc, p->size, out, n, 3)) return true;
        if (!s) break;
    }
    return false;
}

/* parse an IPS file into records; false (and nothing kept) if it is damaged or leaves the ROM */
static bool load_ips(GvPatch *p, const char *path, size_t rom_n)
{
    FILE *f = fopen(path, "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = (sz > 8 && sz < 4 * 1024 * 1024) ? (uint8_t *)malloc((size_t)sz) : NULL;
    bool ok = b && fread(b, 1, (size_t)sz, f) == (size_t)sz && !memcmp(b, "PATCH", 5);
    fclose(f);
    int cap = 0; p->n = 0; p->rec = NULL;
    long i = 5;
    while (ok) {
        if (i + 3 > sz) { ok = false; break; }
        if (!memcmp(b + i, "EOF", 3)) break;
        if (i + 5 > sz) { ok = false; break; }
        uint32_t off = (uint32_t)b[i] << 16 | b[i + 1] << 8 | b[i + 2];
        uint32_t len = (uint32_t)b[i + 3] << 8 | b[i + 4]; i += 5;
        const uint8_t *src = b + i; uint8_t fill = 0; bool rle = len == 0;
        if (rle) { if (i + 3 > sz) { ok = false; break; } len = (uint32_t)b[i] << 8 | b[i + 1]; fill = b[i + 2]; i += 3; }
        else { if (i + (long)len > sz) { ok = false; break; } i += len; }
        if (!len || off + len > rom_n) { ok = false; break; }
        if (p->n == cap) { cap = cap ? cap * 2 : 16; GvPatchRec *r = (GvPatchRec *)realloc(p->rec, sizeof *r * (size_t)cap); if (!r) { ok = false; break; } p->rec = r; }
        GvPatchRec *r = &p->rec[p->n++];
        r->off = off; r->len = len;
        r->neu = (uint8_t *)malloc(len); r->old = (uint8_t *)malloc(len);
        if (!r->neu || !r->old) { ok = false; break; }
        if (rle) memset(r->neu, fill, len); else memcpy(r->neu, src, len);
    }
    free(b);
    if (!ok) {
        for (int k = 0; k < p->n; k++) { free(p->rec[k].neu); free(p->rec[k].old); }
        free(p->rec); p->rec = NULL; p->n = 0;
    }
    return ok;
}

bool gv_patch_init(GvPatch *p)
{
    size_t n = 0; uint8_t *rom = glue_rom_image(&n);
    p->avail = false; p->on = false;
    char path[1400];
    if (!rom || !find_patch(p, path, sizeof path)) {
        fprintf(stderr, "[gv_patch] \"%s\" (CRC32 %08X) not found under ROMs/Patches: option unavailable\n", p->file, (unsigned)p->crc);
        return false;
    }
    if (!load_ips(p, path, n)) { fprintf(stderr, "[gv_patch] %s: damaged or not for this ROM (option unavailable)\n", path); return false; }
    for (int k = 0; k < p->n; k++) memcpy(p->rec[k].old, rom + p->rec[k].off, p->rec[k].len);
    p->avail = true;
    if (getenv("GV_LOG")) fprintf(stderr, "[gv_patch] %s: %d records\n", path, p->n);
    return true;
}

void gv_patch_set(GvPatch *p, bool on)
{
    size_t n = 0; uint8_t *rom = glue_rom_image(&n);
    if (!p->avail || !rom || p->on == on) return;
    for (int k = 0; k < p->n; k++)
        if (p->rec[k].off + p->rec[k].len <= n) memcpy(rom + p->rec[k].off, on ? p->rec[k].neu : p->rec[k].old, p->rec[k].len);
    p->on = on;
    if (getenv("GV_LOG")) fprintf(stderr, "[gv_patch] %s %s\n", p->file, on ? "applied" : "removed (original bytes back)");
}
