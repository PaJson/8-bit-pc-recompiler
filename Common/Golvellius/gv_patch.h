/* gv_patch.h - options that apply the user's own IPS files to the in-memory ROM (see gv_patch.c) */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct { uint32_t off, len; uint8_t *neu, *old; } GvPatchRec;
typedef struct {
    const char *file;                 /* the author's file name (messages only) */
    uint32_t    crc; long size;       /* the IPS file is found by CRC32 + size (any name, ROMs/Patches) */
    GvPatchRec *rec; int n;
    bool avail, on;
} GvPatch;

bool gv_patch_init(GvPatch *p);       /* find + parse the file, remember the original bytes */
void gv_patch_set(GvPatch *p, bool on);   /* write the patch's bytes / the original bytes */
