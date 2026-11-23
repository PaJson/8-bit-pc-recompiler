#!/bin/sh
# Lockstep test: run the recompiled build and the pure interpreter (--interp) of one game on the same
# scripted input, hash RAM/VRAM/CRAM every N frames in both (runner env SMS_STATE_LOG) and compare.
#
#   sh Tools/lockstep.sh <slug> <outdir> <frames> <press-file|-> [poke-file|-] [every=60] [skip=CA00-CAFF]
#
# Prints one summary line: dispatch misses, [stack] warnings, how many checkpoints differ, the longest
# run of consecutive differing checkpoints and whether the final checkpoint matches. Isolated single
# differences that reconverge are interrupt-timing noise (a mid-frame update seen at a slightly
# different moment); a long run / final mismatch is a real divergence -> bisect with --dump-frame.
# Also writes <outdir>/coverage.txt (recompiled functions executed, see Tools/coverage.py).
P="$(cd "$(dirname "$0")/.." && pwd)"
export SMS_FIXED_R="${SMS_FIXED_R:-1}"   # LD A,R from an execution counter (see runner/glue.c sms_test_r)
g="$1"; out="$2"; n="$3"; pf="$4"; kf="${5:--}"; every="${6:-60}"; skip="${7:-CA00-CAFF}"
G=$(ls -d "$P/Games"/*/"$g" 2>/dev/null | head -1); [ -n "$G" ] || { echo "no game folder Games/*/$g"; exit 2; }
rel=$(grep "^rom" "$G/game.toml" | sed 's/^[^"]*"\(.*\)".*/\1/')
rom="$(cd "$G/$(dirname "$rel")" && pwd)/$(basename "$rel")"
exe=$(ls "$G/build/"*.exe | head -1)
mkdir -p "$out/rec" "$out/int"
abspath() { case "$1" in -|/*|?:*) echo "$1";; *) echo "$(pwd)/$1";; esac; }
pf=$(abspath "$pf"); kf=$(abspath "$kf"); out=$(abspath "$out")
w() { cygpath -m "$1" 2>/dev/null || echo "$1"; }
set --
[ "$pf" != "-" ] && set -- "$@" --press-file "$(w "$pf")"
[ "$kf" != "-" ] && set -- "$@" --poke-file "$(w "$kf")"
( cd "$out/rec" && rm -f dispatch_misses.log && SMS_NO_SAVE=1 SMS_STATE_EVERY=$every SMS_STATE_SKIP=$skip \
    SMS_STATE_LOG="$(w "$out/rec/state.log")" SMS_COVERAGE="$(w "$out/coverage.txt")" \
    "$exe" "$(w "$rom")" --frames "$n" "$@" > out.txt 2>&1 ) &
( cd "$out/int" && SMS_NO_SAVE=1 SMS_STATE_EVERY=$every SMS_STATE_SKIP=$skip SMS_STATE_LOG="$(w "$out/int/state.log")" \
    "$exe" "$(w "$rom")" --frames "$n" --interp "$@" > out.txt 2>&1 ) &
wait
python -I - "$out" "$g" <<'EOF'
import sys, re
out, g = sys.argv[1], sys.argv[2]
r = [l.split() for l in open(out + "/rec/state.log")]
i = [l.split() for l in open(out + "/int/state.log")]
n = min(len(r), len(i)); diff = [k for k in range(n) if r[k][1:] != i[k][1:]]
run = best = 0; best_end = None; prev = -2
for k in diff:
    run = run + 1 if k == prev + 1 else 1; prev = k
    if run > best: best, best_end = run, k
txt = open(out + "/rec/out.txt", errors="replace").read()
m = re.search(r"dispatch misses: (\d+)", txt); stopped = re.search(r"stopped after (\d+) frames", txt)
stk = txt.count("[stack]")
fin = "final SAME" if n and r[n-1][1:] == i[n-1][1:] else "final DIFF"
print("%s %s: frames %s, misses %s, stack %d, checkpoints %d, differ %d, longest run %d%s, %s"
      % (g, out.replace("\\", "/").split("/")[-1], stopped.group(1) if stopped else "?", m.group(1) if m else "?", stk, n,
         len(diff), best, (" ending frame " + r[best_end][0]) if best_end is not None else "", fin))
EOF
