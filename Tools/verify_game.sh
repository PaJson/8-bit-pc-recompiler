#!/bin/sh
# Build Games/<slug> and compare the recompiled runner against the pure interpreter (--interp) on the same
# scripted input at a few frames; also report dispatch misses.
#   sh Tools/verify_game.sh <slug> [press-file] [frames...]
# Default press file = Tools/templates/generic_press.txt (Start, then Right/button 1 presses).
cd "$(dirname "$0")/.." || exit 1
slug=$1; press=${2:-Tools/templates/generic_press.txt}
if [ $# -ge 2 ]; then shift 2; else shift $#; fi
frames=${*:-"250 1500 2900"}
g=Games/$slug
export PATH=/c/msys64/mingw64/bin:$PATH SMS_NO_SAVE=1
exe=$(grep -o 'sms_add_game([A-Za-z0-9]*' $g/CMakeLists.txt | sed 's/sms_add_game(//')
rom=$(grep '^rom' $g/game.toml | sed 's/.*"\(.*\)".*/\1/'); rom="$g/$rom"
powershell -NoProfile -File "$g/build.ps1" >/tmp/build_$slug.log 2>&1 || { echo "$slug: BUILD FAILED (see /tmp/build_$slug.log)"; exit 1; }
mkdir -p /tmp/verify
for n in $frames; do
  (cd $g/build && ./$exe.exe "../../../$rom" --frames $((n+1)) --press-file "../../../$press" --dump-frame $n --dump-out /tmp/verify/${slug}_rec_$n.png 2>&1 | grep -E "dispatch misses" | sed "s/^/$slug @$n: /")
  (cd $g/build && ./$exe.exe "../../../$rom" --interp --frames $((n+1)) --press-file "../../../$press" --dump-frame $n --dump-out /tmp/verify/${slug}_int_$n.png >/dev/null 2>&1)
  if cmp -s /tmp/verify/${slug}_rec_$n.png /tmp/verify/${slug}_int_$n.png; then echo "$slug frame $n: recompiled == interpreter"; else echo "$slug frame $n: DIFFERS"; fi
done
