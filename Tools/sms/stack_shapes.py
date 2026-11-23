"""Static scan v2: every path through each recompiled function (jp/jr/djnz/conditional targets inside the
function, fall-through; calls stack-neutral), tracking this function's own push depth. Reports every
instruction that removes or swaps a word it did not push itself (pop / ex (sp) / inc sp at depth 0) =
the function touches its caller's stack: drop-own-return (`pop hl ; ret`), callee-cleans-arguments,
inline-data-after-call, abort-N-frames. Prints the instructions from that point so they can be classified.
usage: python -I Tools/sms/stack_shapes.py <generated _full.c> ..."""
import sys, re
INS = re.compile(r'^    /\* ([0-9A-F]{4})  (.*) \*/$')
FUN = re.compile(r'^void (\w+)\(void\)\{')
ENT = re.compile(r'sms_enter\(0x([0-9A-F]{4})\)')
PUSH = re.compile(r'^push (bc|de|hl|af|ix|iy)$')
POP = re.compile(r'^pop (bc|de|hl|af|ix|iy)$')
TGT = re.compile(r'^(?:jp|jr|djnz)(?: (?:nz|z|nc|c|po|pe|p|m),)? ?\$([0-9A-F]{4})$')
UNCOND_END = re.compile(r'^(ret|reti|retn|jp \$[0-9A-F]{4}|jr \$[0-9A-F]{4}|jp \((hl|ix|iy)\))$')
for path in sys.argv[1:]:
    lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
    funcs = []; cur = None
    for l in lines:
        m = FUN.match(l)
        if m: cur = [m.group(1), None, {}]; funcs.append(cur); continue
        if cur is None: continue
        m = ENT.search(l)
        if m and cur[1] is None: cur[1] = int(m.group(1), 16)
        m = INS.match(l)
        if m: cur[2][int(m.group(1), 16)] = m.group(2)
    out = []; kinds = {}
    for name, entry, ins in funcs:
        if entry is None or entry not in ins: continue
        addrs = sorted(ins); nxt = {a: addrs[i + 1] for i, a in enumerate(addrs[:-1])}
        depth = {entry: 0}; work = [entry]; hits = []
        while work:
            a = work.pop(); d = depth[a]; t = ins[a]
            nd = d; hit = False
            if PUSH.match(t): nd = d + 1
            elif POP.match(t) or t.startswith('ex (sp)') or t == 'inc sp':
                if d == 0: hit = True
                else: nd = d - 1 if not t.startswith('ex') else d
            elif t.startswith('ld sp') or t == 'dec sp': nd = None
            if hit: hits.append(a); continue          # stop this path: shape unknown from here
            if nd is None: continue
            succ = []
            m = TGT.match(t)
            if m and int(m.group(1), 16) in ins: succ.append(int(m.group(1), 16))
            if not UNCOND_END.match(t) and a in nxt: succ.append(nxt[a])
            for s in succ:
                if s not in depth: depth[s] = nd; work.append(s)
        for h in sorted(set(hits)):
            seq = []; b = h
            for _ in range(7):
                seq.append(f"{b:04X} {ins[b]}")
                if UNCOND_END.match(ins[b]): break
                b = nxt.get(b)
                if b is None: break
            key = ' ; '.join(s.split(' ', 1)[1] for s in seq)
            kinds.setdefault(key, []).append(f"{name}@{h:04X}")
    tag = path.replace('\\', '/').split('/')[-3]
    print(f"== {tag}: {sum(len(v) for v in kinds.values())} sites, {len(kinds)} distinct shapes")
    for k, v in sorted(kinds.items(), key=lambda kv: -len(kv[1])):
        print(f"  [{len(v)}] {k}   <- {' '.join(v[:4])}{' ...' if len(v) > 4 else ''}")
