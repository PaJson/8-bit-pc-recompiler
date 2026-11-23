#!/bin/sh
# Tools/sms/regen_all.sh <outdir> [slug...]: regenerate every game's C with the current SmsRecomp into <outdir>/<slug>/generated
# Compare two recompilers: run it before and after a change into two folders, then `diff -r a/<slug>/generated b/<slug>/generated`.
# Copies game.toml + the dispatch manifests; the game folders are not touched.
P="$(cd "$(dirname "$0")/../.." && pwd)"
EXE="$P/Recomp/smsggrecomp/recompiler/build/SmsRecomp.exe"
out="$1"; shift
mkdir -p "$out"
slugs="$*"; [ -z "$slugs" ] && slugs=$(ls -d "$P/Games"/*/*/ | xargs -n1 basename)
one() {
  g=$1; G=$(ls -d "$P/Games"/*/"$g" | head -1); d="$out/$g"; rm -rf "$d"; mkdir -p "$d/build"
  [ -f "$G/game.toml" ] || return
  cp "$G/game.toml" "$d/"
  [ -f "$G/dispatch_manifest.txt" ] && cp "$G/dispatch_manifest.txt" "$d/"
  [ -f "$G/build/dispatch_manifest.txt" ] && cp "$G/build/dispatch_manifest.txt" "$d/build/"
  rel=$(grep '^rom' "$G/game.toml" | sed 's/^[^"]*"\(.*\)".*/\1/')
  rom=$(cygpath -m "$G/$rel")   # a Windows path: MSYS does not convert /d/... args with [ ] (the patched ROMs)
  ( cd "$d" && "$EXE" "$rom" --game game.toml > recomp.log 2>&1 )
  echo "$g: $(grep -c . "$d/recomp.log") log lines, $(grep -h 'cg_emit' "$d/recomp.log" | tail -1)"
}
for g in $slugs; do one $g &
  while [ $(jobs -r | wc -l) -ge 4 ]; do sleep 1; done
done
wait
