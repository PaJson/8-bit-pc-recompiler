/* ps_fixes.c - Phantasy Star: ORIGINAL BUG FIXES (one option, default off = the original game).
 *
 * Bugs of the 1987 game itself; found by the PSRP retranslation (technical.md, issues #48 / #84),
 * checked and re-done here in our own code. Rev A addresses (ps1disasm); the hooks are found by
 * byte pattern in every PS ROM (Tools/sms/ps_find_hooks.py groups "text box", "text box (Japan)",
 * "castle chest", "ship church").
 *
 * 1. TEXT BOX LINE COUNT. The text engine keeps the box state in registers while the box is open
 *    (C != 0 = the cursor is on the second line, so the next new line scrolls first). Showing more
 *    text in a box that is still open (`$C2D3` set) continues at LABEL_31CB with whatever C holds,
 *    and a menu shown in between (save slots, hospital, church, both shops, the "drop something?"
 *    item list) has replaced it with the pressed button. In Rev A this is never visible (every text
 *    before such a menu ends on its second line and a button value is never 0); texts ending on the
 *    first line would get a blank line. Fix: ps_fix_text_char records C each time the engine reads a
 *    character (LABEL_31FA), ps_fix_text_reopen puts it back on that re-entry.
 * 2. AIR CASTLE SOFT LOCK. Leaving the Air Castle (map $17) needs the Laerma nuts with Myau alive
 *    (ItemUse_Nuts). With Myau dead, and neither Alis's Fly (her 2nd map spell, 8 MP) nor Noah's Rise
 *    (his 5th, 12 MP) castable and no TranCarpet (item $29), the game cannot go on after Lassic
 *    (no inn up there). Fix: the treasure chest of the Lassic battle (encounter $C2E6 = $48) - or,
 *    if that chest was left or the item could not be kept, of any later battle at the castle - holds
 *    a TranCarpet: ps_fix_battle_chest sets the chest item $C2DF before the game's own check
 *    (LABEL_1688), so the game's texts, chest and full-inventory "drop something?" prompt are used.
 * 3. SHIP FINISHED. When the Luveno is finished ($C504 = 7, LABEL_4DFB) the spaceports close
 *    (LABEL_4A8C); with Paseo (church 4, Motavia) as the last church Fly / TranCarpet strand the
 *    party on Motavia away from the ship. Fix: the last church becomes Camineet's (1).
 * 4. MYAU AT LEVEL 30 (only the original Japan and the Korea ROMs): his level-30 attack in
 *    B03_MyauLevelTable is $24 (36) after $55 at level 29; every later version has $56. Fix: that
 *    byte in the in-memory ROM copy (UpdateCharStats reads the table after every battle). */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sms_runtime.h"

extern uint8_t *glue_rom_image(size_t *size);

static bool s_on;                                  /* the option */
static int  s_log = -1;
static bool fix_log(void) { if (s_log < 0) s_log = getenv("PS_FIX_LOG") != NULL; return s_log > 0; }

/* ---- 4. Myau's level-30 attack (ROM data) ---- */
static int s_myau_at = -2;                         /* file offset of the byte, -1 = not this ROM */
static void myau_apply(void)
{
    size_t n = 0; uint8_t *rom = glue_rom_image(&n);
    if (!rom) return;
    if (s_myau_at == -2) {
        static const uint8_t anchor[] = { 0x16, 0x12, 0x16, 0, 0, 0, 0, 0, 0x1E, 0x15, 0x1A, 0, 0x32, 0, 0, 0 };
        s_myau_at = -1;
        for (size_t i = 0; i + 30 * 8 <= n; i++)
            if (!memcmp(rom + i, anchor, sizeof anchor)) {
                if (rom[i + 28 * 8 + 1] == 0x55 && rom[i + 29 * 8 + 1] == 0x24) s_myau_at = (int)(i + 29 * 8 + 1);
                break;
            }
    }
    if (s_myau_at >= 0) rom[s_myau_at] = s_on ? 0x56 : 0x24;
}

void ps_fixes_set(bool on)
{
    s_on = on;
    myau_apply();
}

/* ---- 1. text box ---- */
static uint8_t s_tb_c;
static bool    s_tb_seen;

int ps_fix_text_char(void)          /* LABEL_31FA: ld a,(hl) = the engine reads its next character */
{
    s_tb_c = g_z80.c; s_tb_seen = true;
    return 0;
}

int ps_fix_text_reopen(void)        /* LABEL_31CB: dec hl ; jp LABEL_3235 = more text in the open box */
{
    if (s_on && s_tb_seen && g_z80.c != s_tb_c) {
        if (fix_log()) fprintf(stderr, "[fix] text box: C %02X -> %02X\n", g_z80.c, s_tb_c);
        g_z80.c = s_tb_c;
    }
    return 0;
}

/* ---- 2. Air Castle ---- */
static bool alive(uint16_t ch) { return sms_read8(ch) & 1; }
static bool castle_stuck(void)
{
    if (alive(0xC410)) return false;                                                     /* Myau: the nuts */
    if (alive(0xC400) && sms_read8(0xC40F) >= 2 && sms_read8(0xC402) >= 8) return false;  /* Alis: Fly */
    if (alive(0xC430) && sms_read8(0xC43F) >= 5 && sms_read8(0xC432) >= 12) return false; /* Noah: Rise */
    int n = sms_read8(0xC4E2); if (n > 24) n = 24;
    for (int i = 0; i < n; i++) if (sms_read8((uint16_t)(0xC4C0 + i)) == 0x29) return false;  /* TranCarpet */
    return true;
}

int ps_fix_battle_chest(void)       /* LABEL_1688: ld hl,($C2DD) ; ld a,($C2DF) ; or l ; or h ; ret z */
{
    if (!s_on) return 0;
    bool lassic = sms_read8(0xC2E6) == 0x48;
    uint8_t fl = sms_read8(0xC30D);
    bool castle = sms_read8(0xC516) && (sms_read8(0xC309) == 0x17 || (fl >= 0x35 && fl <= 0x39));
    if ((lassic || castle) && castle_stuck()) {
        if (fix_log()) fprintf(stderr, "[fix] Air Castle: chest item %02X -> TranCarpet\n", sms_read8(0xC2DF));
        sms_write8(0xC2DF, 0x29);
    }
    return 0;
}

/* ---- 3. ship finished ---- */
int ps_fix_ship_church(void)        /* LABEL_4DFB: ld a,$07 ; ld ($C504),a ; ld hl,$0294 */
{
    if (s_on && sms_read8(0xC317) == 4) {
        if (fix_log()) fprintf(stderr, "[fix] ship finished: last church Paseo -> Camineet\n");
        sms_write8(0xC317, 1);
    }
    return 0;
}
