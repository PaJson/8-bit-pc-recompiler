#!/bin/sh
# Tools/regen_all.sh <outdir> [slug...]: regenerate every game's C with the current SmsRecomp into <outdir>/<slug>/generated
# Compare two recompilers: run it before and after a change into two folders, then `diff -r a/<slug>/generated b/<slug>/generated`.
# Copies game.toml + the dispatch manifests; the game folders are not touched.
P="$(cd "$(dirname "$0")/.." && pwd)"
EXE="$P/Recomp/smsggrecomp/recompiler/build/SmsRecomp.exe"
out="$1"; shift
mkdir -p "$out"
slugs="$*"; [ -z "$slugs" ] && slugs=$(ls "$P/Games")
one() {
  g=$1; d="$out/$g"; rm -rf "$d"; mkdir -p "$d/build"
  [ -f "$P/Games/$g/game.toml" ] || return
  cp "$P/Games/$g/game.toml" "$d/"
  [ -f "$P/Games/$g/dispatch_manifest.txt" ] && cp "$P/Games/$g/dispatch_manifest.txt" "$d/"
  [ -f "$P/Games/$g/build/dispatch_manifest.txt" ] && cp "$P/Games/$g/build/dispatch_manifest.txt" "$d/build/"
  rel=$(grep '^rom' "$P/Games/$g/game.toml" | sed 's/^[^"]*"\(.*\)".*/\1/')
  rom=$(cygpath -m "$P/Games/$g/$rel")   # a Windows path: MSYS does not convert /d/... args with [ ] (the patched ROMs)
  ( cd "$d" && "$EXE" "$rom" --game game.toml > recomp.log 2>&1 )
  echo "$g: $(grep -c . "$d/recomp.log") log lines, $(grep -h 'cg_emit' "$d/recomp.log" | tail -1)"
}
for g in $slugs; do one $g &
  while [ $(jobs -r | wc -l) -ge 4 ]; do sleep 1; done
done
wait
