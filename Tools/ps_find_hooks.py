#!/usr/bin/env python3
"""Find the Phantasy Star QoL hook sites in any PS ROM and print the [insn_hooks] block.

  python -I Tools/ps_find_hooks.py "<ROM file in ROMs/ or a path>"
  python -I Tools/ps_find_hooks.py --update Games/<slug>/game.toml   # replace the block in place

The sites (Rev A addresses, see Common/PhantasyStar/ps_qol.c) are located by byte patterns taken from
the original code; '..' bytes are wildcards (absolute jump targets, which move between versions).
Hooks come in GROUPS; a group is used only if every one of its patterns matches exactly once in
$0000-$7FFF (a game is better left without a feature than with a wrong hook). Expected: the 2.5
retranslation rewrote the walking code (it has its own walk-speed option), so it has no walk group.
"""
import sys, os, re

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
# group -> [(func, pattern (hex, '..' = any byte), hook offset in pattern, resume offset (relative to hook))]
GROUPS = [
    ("walk speed", [
        ("ps_walk_frames",   "3a 0e c3 b7 3e 0f 28 02 3e 07 32 65 c2 11 01 00 3a 0e c3 b7 28 01 1c", 0, 13),
        ("ps_walk_pixels",   "3a 0e c3 b7 3e 0f 28 02 3e 07 32 65 c2 11 01 00 3a 0e c3 b7 28 01 1c", 13, 10),
        ("ps_walk_animcp",   "fe 0f c2 .. .. 79 b7 c2 .. .. fd 7e 12 fd 77 13", 0, 2),
        ("ps_walk_animtmr",  "fd 36 0e 07 fd 7e 12 87 87 47", 0, 4),
        ("ps_walk_follow_x", "b7 20 01 3d fd 86 02 fd 77 02 c9 d6 02 20 01 3d fd 86 04", 0, 10),
        ("ps_walk_follow_y", "b7 20 01 3d fd 86 02 fd 77 02 c9 d6 02 20 01 3d fd 86 04", 11, 11),
    ]),
    # battle damage: `add a,(ix+1)` with A = 256 - damage, ix = the target's stats
    # LABEL_125E (party attacks + party magic on enemies): push af ; ld a,$AD ; ld ($C004),a ;
    #   call .. ; pop af ; add a,(ix+1) ; jr c,+ ; xor a ; + ld (ix+1),a ; ret nz
    # LABEL_15C2 (enemy attacks + enemy magic on the party): rrca ; and $7F ; cpl ; add a,(ix+1) ;
    #   jr c,+ ; xor a ; + ld (ix+1),a ; jr nz
    # EXP multiplier, per character (ps_qol.c ps_exp_add): CalculateExp
    #   ld hl,(CurrentBattle_EXPReward) ; add hl,de ; jr nc,+ ; ld hl,$FFFF ; + ld (iy+exp),l ;
    #   ld (iy+exp+1),h ; ret c  -> hook the add (saturate without carry), resume at the store.
    ("exp", [
        ("ps_exp_add", "2a d0 c2 19 30 03 21 ff ff fd 75 .. fd 74 .. d8", 3, 6),
    ]),
    # SOUND CHIP (ps_sound.c): the three sound shims in bank 0, `ld hl,$FFFF ; ld (hl),BANK ; jp ENTRY`
    #   x3 (update, init, mute) -> hook each `ld hl,$FFFF` (0 = original shim, 2 = return; resume unused)
    ("sound", [
        ("ps_snd_update", "21 ff ff 36 .. c3 .. .. 21 ff ff 36 .. c3 .. .. 21 ff ff 36 .. c3 .. ..", 0, 5),
        ("ps_snd_init",   "21 ff ff 36 .. c3 .. .. 21 ff ff 36 .. c3 .. .. 21 ff ff 36 .. c3 .. ..", 8, 5),
        ("ps_snd_mute",   "21 ff ff 36 .. c3 .. .. 21 ff ff 36 .. c3 .. .. 21 ff ff 36 .. c3 .. ..", 16, 5),
    ]),
    ("damage", [
        ("ps_damage", "f5 3e ad 32 04 c0 cd .. .. f1 dd 86 01 38 01 af dd 77 01 c0", 10, 3),
        ("ps_damage", "0f e6 7f 2f dd 86 01 38 01 af dd 77 01 20", 4, 3),
    ]),
    # ITEMS page (ps_items.c): observe when the walking handlers wait for the next frame.
    # GameMode_Map: ld a,(Game_is_paused) ; or a ; call nz,PauseLoop ; ld a,$0E ; call WaitForVInt ;
    #   call LABEL_576A ; call LABEL_77AC ; ld a,($C265)       GameMode_Dungeon: ... ld a,$08 ;
    #   call WaitForVInt ; call LABEL_65EE ; ld a,($C2D2) ; or a ; ret z
    # WaitForVInt = the target of the map handler's call; it starts with ld ($C208),a (all versions;
    #   the 2.5 retranslation adds a halt after it). "call@N" = hook the call target read at match+N.
    ("items", [
        ("ps_items_walk_mark", "3a 12 c2 b7 c4 .. .. 3e 0e cd .. .. cd .. .. cd .. .. 3a 65 c2", 7, 2),
        ("ps_items_walk_mark", "3a 12 c2 b7 c4 .. .. 3e 08 cd .. .. cd .. .. 3a d2 c2 b7 c8", 7, 2),
        ("ps_items_vwait", "call@10 3a 12 c2 b7 c4 .. .. 3e 0e cd .. .. cd .. .. cd .. .. 3a 65 c2", 0, 3),
    ]),
    # Hovercraft also drives on land (ps_qol.c ps_hover_*). LABEL_74E4 (can the party / vehicle move?):
    #   hovercraft branch cp $08 ; jr nz ; push de ; ld b,0 ; ld hl,LABEL_7A2B ; add hl,bc ; ex de,hl ;
    #   ld a,(de) ; call LABEL_7764 ; and $0A ; jr nz ; inc de ; ld a,(de) ; call LABEL_7764 ; and $0A
    #   (tile attribute: Land Rover / on foot blocked by bit 0, hovercraft by bits 1|3) -> hook both ANDs.
    # ItemUse_Hovercraft: ld a,($C308) ; cp 4 ; ld hl,.. ; jr nc ; ld a,(Interaction_Type) ; or a ;
    #   jr z ; push bc ; push de ; call LABEL_76C1 (find water) ; pop de ; pop bc ; ld hl,.. ; jr nz ;
    #   ld a,$08 ; ld ($C30E),a  -> hook the Interaction_Type load, resume = board in place.
    ("hovercraft", [
        ("ps_hover_pass", "fe 08 20 .. d5 06 00 21 .. .. 09 eb 1a cd .. .. e6 0a 20 .. 13 1a cd .. .. e6 0a", 16, 2),
        ("ps_hover_pass", "fe 08 20 .. d5 06 00 21 .. .. 09 eb 1a cd .. .. e6 0a 20 .. 13 1a cd .. .. e6 0a", 25, 2),
        ("ps_hover_board", "3a 08 c3 fe 04 21 .. .. 30 .. 3a .. .. b7 28 .. c5 d5 cd .. .. d1 c1 21 .. .. 20 .. 3e 08 32 0e c3", 10, 18),
    ]),
    # FLY page (ps_fly.c): the end of GameMode_Map, where the game opens the player menu:
    #   ld a,(Ctrl_1_held) ; and $30 ; ret z ; ld a,($C265) ; or a ; ret nz ; xor a ;
    #   LABEL_B41: ld ($C29D),a ; ld hl,Game_mode ; ld (hl),$0C   -> hook the load, resume = `ret z`
    ("fly", [
        ("ps_fly_go", "3a .. .. e6 30 c8 3a 65 c2 b7 c0 af 32 9d c2 21 02 c2 36 0c", 0, 5),
    ]),
    # FLY from inside a dungeon (FLY COST = FREE): GameMode_Dungeon: ld a,(Game_is_paused) ; or a ;
    #   call nz,PauseLoop ; ld a,$08 ; call WaitForVInt ; call LABEL_65EE (movement) ; ld a,($C2D2) ;
    #   or a ; ret z   -> hook the LABEL_65EE call, resume = `ret z`
    ("fly dungeon", [
        ("ps_fly_dungeon_go", "3a 12 c2 b7 c4 .. .. 3e 08 cd .. .. cd .. .. 3a d2 c2 b7 c8", 12, 7),
    ]),
    # Battle auto attack (ps_qol.c ps_battle_*). LABEL_100F (battle) starts `ld a,($C2E6) ; cp $48 ;
    #   ld c,$92 ; jr z ; cp $49` -> ps_battle_begin (observe only). Per character (LABEL_108A):
    #   ld de,$0C ; add hl,de ; ld a,(hl) ; or a ; jr nz,LABEL_1080 ; call LABEL_3105 ; call LABEL_30D5 ;
    #   call LABEL_2EAC ; ld hl,$7882 ; ld (Cursor_pos),hl ; ld a,4 ; ld (Option_total_num),a ;
    #   call CheckOptionSelect ; bit 4,c ; jp nz,LABEL_1121 ; ld hl,BattleMenu_OptionTbl ;
    #   call GetPtrAndJump ($00E6; Japanese ROMs $0112) ; call LABEL_2ECD ; LABEL_10B7: ld a,($C267) ; cp 4
    #   -> ps_battle_cmd at the first window call, resume = LABEL_10B7 (no menu drawn at all).
    # CheckOptionSelect's button read (LABEL_2D8B: ld a,(Ctrl_1_pressed) ; and $30 ; jp z,loop ;
    #   ld c,a ; xor a ; ld ($C26D),a) -> ps_battle_select (observe; answers an open battle menu).
    ("battle", [
        ("ps_battle_begin", "3a e6 c2 fe 48 0e 92 28 .. fe 49", 0, 3),
        ("ps_battle_cmd", "11 0c 00 19 7e b7 20 .. cd .. .. cd .. .. cd .. .. 21 82 78 22 69 c2 3e 04 32 6e c2 cd .. .. cb 61 c2 .. .. 21 .. .. cd .. .. cd .. .. 3a 67 c2 fe 04", 8, 37),
        ("ps_battle_select", "3a 05 c2 e6 30 ca .. .. 4f af 32 6d c2", 0, 3),
    ]),
    # The 2.5 retranslation reads the buttons through its own `call $3F09 ; nop ; nop ; jp z,loop`
    # (mask by game mode): same hook on that call (it only reads Ctrl_1_pressed).
    ("battle (2.5)", [
        ("ps_battle_begin", "3a e6 c2 fe 48 0e 92 28 .. fe 49", 0, 3),
        ("ps_battle_cmd", "11 0c 00 19 7e b7 20 .. cd .. .. cd .. .. cd .. .. 21 82 78 22 69 c2 3e 04 32 6e c2 cd .. .. cb 61 c2 .. .. 21 .. .. cd .. .. cd .. .. 3a 67 c2 fe 04", 8, 37),
        ("ps_battle_select", "cd .. .. 00 00 ca .. .. 4f af 32 6d c2", 0, 3),
    ]),
]

def find(rom, pat):
    rx = re.compile(b"".join(b"." if t == ".." else re.escape(bytes([int(t, 16)])) for t in pat.split()), re.S)
    return [m.start() for m in rx.finditer(rom[:0x8000])]

def hooks_for(rom, log=print):
    rows = []
    for gname, sites in GROUPS:
        g = []
        for name, pat, off, res in sites:
            follow = None
            if pat.startswith("call@"):
                follow, pat = int(pat[5:pat.index(" ")]), pat[pat.index(" ") + 1:]
            hits = find(rom, pat)
            if len(hits) != 1:
                log("# %s: pattern for %s found %d times - group not used for this ROM" % (gname, name, len(hits)))
                g = None; break
            at = hits[0] + off
            if follow is not None:
                at = rom[hits[0] + follow] | (rom[hits[0] + follow + 1] << 8)
                if at >= 0x8000 or rom[at:at + 3] != bytes((0x32, 0x08, 0xC2)):
                    log("# %s: %s target %04X is not ld ($C208),a - group not used" % (gname, name, at))
                    g = None; break
            g.append((at, name, at + res))
        if g: rows += g
    return rows

def block(rows):
    if not rows: return ""
    return ("[insn_hooks]   # QoL hooks (Tools/ps_find_hooks.py; each returns 0 = original code unless enabled)\n"
            "addr   = [%s]\nfunc   = [%s]\nresume = [%s]\n" % (
                ", ".join("0x%04X" % r[0] for r in rows), ", ".join('"%s"' % r[1] for r in rows),
                ", ".join("0x%04X" % r[2] for r in rows)))

def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--update":
        toml_path = sys.argv[2]
        t = open(toml_path, encoding="utf-8").read()
        m = re.search(r'^rom\s*=\s*"([^"]+)"', t, re.M)
        rom = open(os.path.join(os.path.dirname(os.path.abspath(toml_path)), m.group(1)), "rb").read()
        rows = hooks_for(rom)
        # drop an existing [insn_hooks] section (and the comment lines right above it)
        t = re.sub(r'(?:^#[^\n]*\n)*^\[insn_hooks\][^\n]*\n(?:^(?!\[)[^\n]*\n?)*', "", t, flags=re.M)
        b = block(rows)
        if b:
            gen = re.search(r'^# generated by:', t, re.M)
            t = (t[:gen.start()] + b + "\n" + t[gen.start():]) if gen else (t.rstrip("\n") + "\n\n" + b)
        open(toml_path, "w", encoding="utf-8", newline="\n").write(t)
        print("%s: %d hooks" % (toml_path, len(rows)))
        return 0
    p = sys.argv[1]
    if not os.path.isfile(p): p = os.path.join(ROOT, "ROMs", p)
    sys.stdout.write(block(hooks_for(open(p, "rb").read())))
    return 0

if __name__ == "__main__":
    sys.exit(main())
