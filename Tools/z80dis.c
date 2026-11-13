/* z80dis - tiny SMS ROM disassembler on top of the recompiler's own decoder.
 *
 *   gcc -O1 -I Recomp/smsggrecomp/recompiler/src Tools/z80dis.c Recomp/smsggrecomp/recompiler/src/z80_decoder.c -o z80dis.exe
 *   z80dis <rom> <addr> [count=40] [bank=-1]
 *
 * addr < $8000 is read from banks 0/1 (fixed in most games); addr >= $8000 needs bank (slot 2).
 * Also prints the raw bytes, and decodes the few opcodes the decoder only names "op $xx".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "z80_decoder.h"

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: z80dis <rom> <addr> [count] [bank]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *rom = malloc((size_t)n + 8); memset(rom + n, 0xFF, 8);
    if (fread(rom, 1, (size_t)n, f) != (size_t)n) return 1;
    fclose(f);
    unsigned addr = (unsigned)strtoul(argv[2], NULL, 16);
    int count = argc > 3 ? atoi(argv[3]) : 40;
    int bank = argc > 4 ? atoi(argv[4]) : -1;
    for (int i = 0; i < count; i++) {
        long off = addr < 0x8000 ? (long)addr : (bank >= 0 ? (long)bank * 0x4000 + (addr - 0x8000) : -1);
        if (off < 0 || off >= n) { printf("%04X  (no bank for slot 2)\n", addr); break; }
        Z80Insn in; z80_decode(rom + off, (size_t)(n - off + 4), (uint16_t)addr, &in);
        char raw[16] = ""; for (int k = 0; k < in.length; k++) sprintf(raw + 3 * k, "%02X ", in.raw[k]);
        char txt[64]; snprintf(txt, sizeof txt, "%s", in.text);
        if (!strncmp(txt, "op ", 3) && in.prefix == 0) {          /* fill in the decoder's unnamed ones */
            switch (in.opcode) {
                case 0x32: snprintf(txt, sizeof txt, "ld ($%04X),a", in.imm); break;
                case 0x3A: snprintf(txt, sizeof txt, "ld a,($%04X)", in.imm); break;
                case 0x22: snprintf(txt, sizeof txt, "ld ($%04X),hl", in.imm); break;
                case 0x2A: snprintf(txt, sizeof txt, "ld hl,($%04X)", in.imm); break;
                default: break;
            }
        }
        printf("%04X  %-12s %s\n", addr, raw, txt);
        addr = (addr + in.length) & 0xFFFF;
    }
    return 0;
}
