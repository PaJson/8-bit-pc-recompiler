/*
 * gv_patch.c - options that apply parts of the user's OWN IPS files at run time.
 *
 * Some of Alianger's Golvellius hacks are pure content (level edits, rewritten texts), not code we
 * could write ourselves. User decision 2026-11-15: such an option reads the IPS file from the
 * user's ROMs/Patches folder at start-up and, while the option is on, writes its bytes into the
 * in-memory ROM image (the ROM file itself is never touched; switching off restores the original
 * bytes). Nothing from the patches is stored in this project (credits: CREDITS.md). Without the
 * file the option shows "IPS FILE MISSING" and stays off.
 *
 * Only DATA records may be applied this way: the recompiled code was compiled from the original ROM,
 * so changed code bytes would have no effect (they would only reach the interpreter fallback).
 * Every record of the files used here was checked against the generated code (2026-11-15): text,
 * tables and cave layouts the game reads at run time.
 *
 * Finding the files: by CONTENT (CRC32 + size of the IPS file), so the folder layout and file names
 * do not matter. Searched: $GV_PATCH_DIR, else "Patches" next to the ROM or in up to three folders
 * above it (ROMs/Patches/ for ROMs/x.sms and ROMs/Patched/x.sms), up to 6 folder levels deep:
 * loose *.ips files and *.ips files INSIDE *.zip archives (the authors' official downloads, kept as
 * they are: user decision 2026-11-15). A zip's central directory lists every member's CRC32 and size,
 * so only the matching member is read and inflated (stored or deflate; stb_image's zlib decoder).
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
extern int stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);   /* runner/art.c (stb_image) */

#define MAX_PATCH (4 * 1024 * 1024)

static uint32_t crc32_of(const uint8_t *d, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) { c ^= d[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
static bool ends_with(const char *n, const char *ext)
{
    size_t l = strlen(n), e = strlen(ext);
    if (l <= e) return false;
    for (size_t i = 0; i < e; i++) if ((n[l - e + i] | 32) != ext[i]) return false;
    return true;
}
static unsigned rd16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* a loose file with this CRC32 + size -> its bytes (malloc) */
static uint8_t *file_data(const char *path, uint32_t crc, long size)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = NULL;
    if (sz == size && sz > 0 && sz <= MAX_PATCH) {
        b = (uint8_t *)malloc((size_t)sz);
        if (b && (fread(b, 1, (size_t)sz, f) != (size_t)sz || crc32_of(b, (size_t)sz) != crc)) { free(b); b = NULL; }
    }
    fclose(f);
    return b;
}
/* a member of a zip with this CRC32 + uncompressed size -> its bytes (malloc), name in *member */
static uint8_t *zip_data(const char *path, uint32_t crc, long size, char *member, size_t mn)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long zs = ftell(f);
    uint8_t *out = NULL, *cd = NULL, tail[65557];
    long tn = zs < (long)sizeof tail ? zs : (long)sizeof tail;
    if (tn < 22 || fseek(f, zs - tn, SEEK_SET) || fread(tail, 1, (size_t)tn, f) != (size_t)tn) goto done;
    long e = -1;                                     /* end of central directory record */
    for (long i = tn - 22; i >= 0; i--) if (rd32(tail + i) == 0x06054b50u) { e = i; break; }
    if (e < 0) goto done;
    uint32_t cd_size = rd32(tail + e + 12), cd_off = rd32(tail + e + 16);
    if (cd_size > 16u * 1024 * 1024 || (long)cd_off + (long)cd_size > zs) goto done;
    cd = (uint8_t *)malloc(cd_size ? cd_size : 1);
    if (!cd || fseek(f, (long)cd_off, SEEK_SET) || fread(cd, 1, cd_size, f) != cd_size) goto done;
    for (uint32_t p = 0; p + 46 <= cd_size && rd32(cd + p) == 0x02014b50u; ) {
        unsigned method = rd16(cd + p + 10), nl = rd16(cd + p + 28), xl = rd16(cd + p + 30), cl = rd16(cd + p + 32);
        uint32_t mcrc = rd32(cd + p + 16), csize = rd32(cd + p + 20), usize = rd32(cd + p + 24), lho = rd32(cd + p + 42);
        if (p + 46 + nl > cd_size) break;
        if (mcrc == crc && (long)usize == size && (method == 0 || method == 8) && csize <= MAX_PATCH && usize <= MAX_PATCH) {
            uint8_t lh[30];
            if (fseek(f, (long)lho, SEEK_SET) || fread(lh, 1, 30, f) != 30 || rd32(lh) != 0x04034b50u) break;
            if (fseek(f, (long)lho + 30 + rd16(lh + 26) + rd16(lh + 28), SEEK_SET)) break;
            uint8_t *c = (uint8_t *)malloc(csize ? csize : 1), *u = (uint8_t *)malloc(usize ? usize : 1);
            bool ok = c && u && fread(c, 1, csize, f) == csize;
            if (ok && method == 0) ok = csize == usize && (memcpy(u, c, usize), true);
            else if (ok) ok = stbi_zlib_decode_noheader_buffer((char *)u, (int)usize, (const char *)c, (int)csize) == (int)usize;
            free(c);
            if (ok && crc32_of(u, usize) == crc) {
                out = u;
                snprintf(member, mn, "%.*s", (int)nl, (const char *)cd + p + 46);
            } else free(u);
            break;
        }
        p += 46 + nl + xl + cl;
    }
done:
    free(cd); fclose(f);
    return out;
}
/* search dir (depth levels of subfolders) for the patch: loose *.ips or a member of a *.zip */
static uint8_t *find_in(const char *dir, const GvPatch *pt, char *where, size_t wn, int depth)
{
    DIR *d = opendir(dir); if (!d) return NULL;
    struct dirent *e; uint8_t *data = NULL;
    char (*sub)[512] = malloc(32 * 512); int nsub = 0;
    while (!data && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char p[1400]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (ends_with(e->d_name, ".ips")) { if ((data = file_data(p, pt->crc, pt->size)) != NULL) snprintf(where, wn, "%s", p); }
        else if (ends_with(e->d_name, ".zip")) {
            char m[300];
            if ((data = zip_data(p, pt->crc, pt->size, m, sizeof m)) != NULL) snprintf(where, wn, "%s :: %s", p, m);
        }
        else if (depth > 0 && sub && nsub < 32 && strlen(p) < 512) { DIR *t = opendir(p); if (t) { closedir(t); snprintf(sub[nsub++], 512, "%s", p); } }
    }
    closedir(d);
    for (int i = 0; i < nsub && !data; i++) data = find_in(sub[i], pt, where, wn, depth - 1);
    free(sub);
    return data;
}
static uint8_t *find_patch(const GvPatch *p, char *where, size_t wn)
{
    const char *env = getenv("GV_PATCH_DIR");
    if (env && env[0]) return find_in(env, p, where, wn, 6);
    char dir[1024]; const char *rp = glue_rom_path();
    snprintf(dir, sizeof dir, "%s", rp && rp[0] ? rp : ".");
    for (int up = 0; up < 4; up++) {                 /* the ROM's folder, then its parents */
        char *s = NULL; for (char *q = dir; *q; q++) if (*q == '/' || *q == '\\') s = q;
        if (s) *s = 0; else snprintf(dir, sizeof dir, ".");
        char base[1100]; snprintf(base, sizeof base, "%s/Patches", dir);
        uint8_t *d = find_in(base, p, where, wn, 6);
        if (d) return d;
        if (!s) break;
    }
    return NULL;
}

/* parse IPS bytes into records; false (and nothing kept) if damaged or leaving the ROM */
static bool parse_ips(GvPatch *p, const uint8_t *b, long sz, size_t rom_n)
{
    bool ok = sz > 8 && !memcmp(b, "PATCH", 5);
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
    char where[1800] = "";
    uint8_t *data = rom ? find_patch(p, where, sizeof where) : NULL;
    if (!data) {
        fprintf(stderr, "[gv_patch] \"%s\" (CRC32 %08X) not found under ROMs/Patches (loose or in a zip): option unavailable\n", p->file, (unsigned)p->crc);
        return false;
    }
    bool ok = parse_ips(p, data, p->size, n);
    free(data);
    if (!ok) { fprintf(stderr, "[gv_patch] %s: damaged or not for this ROM (option unavailable)\n", where); return false; }
    for (int k = 0; k < p->n; k++) memcpy(p->rec[k].old, rom + p->rec[k].off, p->rec[k].len);
    p->avail = true;
    if (getenv("GV_LOG")) fprintf(stderr, "[gv_patch] %s: %d records\n", where, p->n);
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
