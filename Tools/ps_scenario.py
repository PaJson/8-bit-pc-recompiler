#!/usr/bin/env python3
"""Phantasy Star test scenarios: press + poke files that boot into the game, give the party a
chosen level / inventory / money, warp to a map or dungeon and then play seeded random input.

  python -I Tools/ps_scenario.py <out-prefix> --seed N [--map K | --dungeon IDX:POS | --mode M]
                                 [--level L] [--frames F] [--boot B]

Writes <out-prefix>.press.txt and <out-prefix>.poke.txt for the runner (--press-file / --poke-file).
The point is code coverage for the recompiled-vs-interpreter lockstep test (Tools/lockstep.sh),
not a sensible play-through: odd states are fine as long as both builds see the same input.

RAM (ps1disasm-master/ps1.constants.asm): Game_mode $C202 (8 = LoadMap, 9 = Map, $A = LoadDungeon,
4 = LoadShip, $E = LoadRoad), map index $C309 (0-3 planet overworlds, 4-23 towns), dungeon index
$C30D / position $C30C, party stats $C400 + $10*n, inventory $C4C0 (24), money $C4E0, item count
$C4E2, party size-1 $C4F0. Level tables: bank03.asm B03_<Name>LevelTable (8 bytes per level:
maxHP, attack, defense, maxMP, EXP word, battle-magic count, map-magic count).
"""
import sys, os, re, random, argparse

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
BANK03 = os.path.join(ROOT, "Disasm", "ps1disasm-master", "banks", "bank03.asm")

def level_tables():
    src = open(BANK03, encoding="utf-8", errors="replace").read()
    t = {}
    for name in ("Alis", "Myau", "Odin", "Noah"):
        m = re.search(r"B03_%sLevelTable:\s*\n((?:\.db[^\n]*\n)+)" % name, src)
        rows = []
        for ln in m.group(1).splitlines():
            vals = [int(x.strip().lstrip("$"), 16) for x in ln.split(";")[0].replace(".db", "").split(",") if x.strip()]
            rows.append(vals)
        t[name] = rows
    return t

ITEM_SETS = [   # 24-item inventories (item IDs from ps1.constants.asm), rotated by seed
    [0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
     0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B],
    [0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x21, 0x22, 0x23, 0x24, 0x25, 0x27, 0x28,
     0x01, 0x05, 0x09, 0x0C, 0x0E, 0x10, 0x14, 0x17, 0x19, 0x1E, 0x20, 0x2B],
    [0x02, 0x03, 0x04, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0D, 0x0F, 0x11, 0x12,
     0x13, 0x15, 0x16, 0x18, 0x1A, 0x1B, 0x1C, 0x1D, 0x1F, 0x24, 0x25, 0x29],
]
EQUIP = {"Alis": (0x0C, 0x16, 0x1E), "Myau": (0x0A, 0x18, 0x00), "Odin": (0x0F, 0x17, 0x20), "Noah": (0x04, 0x11, 0x1F)}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out"); ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--map", type=int); ap.add_argument("--dungeon"); ap.add_argument("--mode", type=lambda x: int(x, 0))
    ap.add_argument("--level", type=int, default=20); ap.add_argument("--frames", type=int, default=20000)
    ap.add_argument("--boot", type=int, default=4500, help="frame at which the game is on the Camineet map")
    ap.add_argument("--items", type=int, help="item set index (default: seed mod sets)")
    ap.add_argument("--no-party", action="store_true", help="do not change party/items/money")
    ap.add_argument("--npc", help="FROM:TO[:FRAMES] run NPC/event scripts FROM..TO-1 (table $4773 via $C2DB, "
                    "Game_mode $0C LoadInteraction), FRAMES each (default 400), pressing buttons through the dialogue")
    ap.add_argument("--keys", choices=["default", "battle"], default="default", help="random-play key mix")
    ap.add_argument("--start", type=int, help="frame where --npc/--menu/random play begin (default: boot + 120)")
    ap.add_argument("--menu", help="K:FROM:TO[:FRAMES[:CHAR]] drive player-menu option K over items/spells FROM..TO-1 (see code)")
    a = ap.parse_args()
    rnd = random.Random(a.seed)
    press, poke = [], []
    # boot: press button 1 briefly every 120 frames through the intro (reaches Camineet ~frame 4400)
    for f in range(400, a.boot - 60, 120):
        press += ["%d:A" % f, "%d:" % (f + 8)]
    W = a.boot
    if not a.no_party:
        lt = level_tables()
        for n, name in enumerate(("Alis", "Myau", "Odin", "Noah")):
            row = lt[name][max(0, min(a.level, len(lt[name])) - 1)]
            base = 0xC400 + 0x10 * n
            w, ar, sh = EQUIP[name]
            vals = {0: 1, 1: row[0], 2: row[3], 3: row[4], 4: row[5], 5: a.level, 6: row[0], 7: row[3],
                    8: row[1], 9: row[2], 0xA: w, 0xB: ar, 0xC: sh, 0xE: row[6], 0xF: row[7]}
            for off, v in vals.items():
                poke.append("%d:%04X=%02X" % (W, base + off, v & 0xFF))
        items = ITEM_SETS[(a.items if a.items is not None else a.seed) % len(ITEM_SETS)]
        for i, it in enumerate(items):
            poke.append("%d:%04X=%02X" % (W, 0xC4C0 + i, it))
        poke += ["%d:C4E2=%02X" % (W, len(items)), "%d:C4E0=0F" % W, "%d:C4E1=27" % W, "%d:C4F0=03" % W]
    if a.map is not None:
        # $C308 picks the tile set in GameMode_LoadMap (< 4 = overworld, else town): keep it = map
        poke += ["%d:C308=%02X" % (W + 2, a.map), "%d:C309=%02X" % (W + 2, a.map), "%d:C202=08" % (W + 2)]
    elif a.dungeon:
        idx, pos = (int(x, 0) for x in a.dungeon.split(":"))
        poke += ["%d:C30D=%02X" % (W + 2, idx), "%d:C30C=%02X" % (W + 2, pos), "%d:C202=0A" % (W + 2)]
    elif a.mode is not None:
        poke += ["%d:C202=%02X" % (W + 2, a.mode)]
    f = a.start if a.start is not None else W + 120
    if a.npc:
        p = [int(x, 0) for x in a.npc.split(":")]
        lo, hi, step = p[0], p[1], (p[2] if len(p) > 2 else 400)
        for k in range(lo, hi):
            poke += ["%d:C29D=00" % f, "%d:C2DB=%02X" % (f, k), "%d:C2DC=00" % f, "%d:C202=0C" % f]
            g = f + 30
            while g < f + step - 10:            # page through the dialogue; sometimes answer "no"
                press += ["%d:%s" % (g, rnd.choice(["A", "A", "A", "B", "D", "U"])), "%d:" % (g + 6)]
                g += rnd.randint(14, 30)
            f += step
        press.sort(key=lambda s: int(s.split(":")[0]))
    if a.menu:
        # menu: K:FROM:TO[:FRAMES] - option K of the player menu (0 Stats 1 Magic 2 Item 3 Search 4 Save);
        # for Item, item IDs FROM..TO-1 are put in inventory slot 0 one after the other; for Magic,
        # FROM..TO-1 is the spell-list cursor position (Noah = 4th character). Button 2 (B) confirms.
        p = [int(x, 0) for x in a.menu.split(":")]
        opt, lo, hi, step = p[0], p[1], p[2], (p[3] if len(p) > 3 else 600)
        who = p[4] if len(p) > 4 else 3          # Magic: character 0 Alis .. 3 Noah
        for k in range(lo, hi):
            if opt == 2: poke.append("%d:C4C0=%02X" % (f, k))
            # the menu is up ~100 frames after the button press
            seq = [(20, "B")] + [(190 + 15 * j, "D") for j in range(opt)] + [(250, "B")]
            if opt == 1:     # character `who` then spell k
                seq += [(290 + 12 * j, "D") for j in range(who)] + [(340, "B")] + [(380 + 12 * j, "D") for j in range(k)] + [(480, "B"), (520, "B")]
                t0 = 560
            elif opt == 2:   # item list starts on "NEXT": down to the first item, USE, first character
                seq += [(300, "D"), (330, "B"), (375, "B"), (420, "B")]
                t0 = 460
            elif opt == 5:   # 2.5 retranslation "Options" menu: wander and toggle
                seq += [(t, rnd.choice(["U", "D", "D", "L", "R", "R", "B"])) for t in range(300, step - 80, 18)]
                t0 = step - 70
            else:
                seq += [(300, "B"), (345, "B"), (390, "B")]
                t0 = 430
            seq += [(t, rnd.choice(["B", "A", "B"])) for t in range(t0, step - 60, 22)]
            seq += [(step - 50, "A"), (step - 30, "A"), (step - 12, "A")]
            for t, key in seq:
                press += ["%d:%s" % (f + t, key), "%d:" % (f + t + 5)]
            f += step
        press.sort(key=lambda s: int(s.split(":")[0]))
    # random play: held keys for 4-60 frames; directions dominate, buttons open menus/talk/fight
    keys = ["U", "D", "L", "R"] * 5 + ["A"] * 5 + ["B"] * 2 + [""] * 2 + ["UA", "DA", "S"]
    if a.keys == "battle":   # walk a lot (encounters), then pick battle-menu entries with B / Up / Down
        keys = ["U", "D", "L", "R"] * 4 + ["B"] * 8 + ["D", "U"] * 3 + ["A"] * 2 + [""]
    while f < a.frames:
        press.append("%d:%s" % (f, rnd.choice(keys)))
        f += rnd.randint(4, 60)
    open(a.out + ".press.txt", "w").write("\n".join(press) + "\n")
    open(a.out + ".poke.txt", "w").write("\n".join(poke) + "\n")
    print("%s: %d presses, %d pokes" % (a.out, len(press), len(poke)))

main()
