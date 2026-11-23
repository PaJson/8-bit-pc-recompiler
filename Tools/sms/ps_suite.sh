#!/bin/sh
# Phantasy Star scenario suite (56 runs): all 24 maps / towns, 13 dungeons (FLY table entrances), 4 battle
# runs (encounters 200 %), all 182 NPC scripts (7 runs), all 64 items (4 runs), magic of all 4 characters.
#
#   sh Tools/sms/ps_suite.sh lockstep <outdir> [slug=ps-reva]
#       recompiled vs --interp (Tools/lockstep.sh) for every scenario; QoL options off (both builds).
#   sh Tools/sms/ps_suite.sh qol <outdir> [slug=ps-reva] [ini]
#       recompiled build only, with QoL options on (default ini below: multipliers, walk x4, diagonal,
#       hovercraft on land, FLY free, sound chip FM), vehicle buttons every 400 frames and the map
#       renderer's self-test: looks for dispatch misses, [stack] warnings and early stops.
#
# Boot frame 4500 = Rev A / USA / Brazil / AGES En (the scenarios poke the party in at Camineet). The AGES
# En lockstep needs PS_SOUND_CHIP=fm (its own mode): in the default PSG mode the sound driver uses less CPU
# time, so the game paces differently from the interpreter (not a bug). Prints one line per scenario and a total.
P="$(cd "$(dirname "$0")/../.." && pwd)"
mode="$1"; out="$2"; g="${3:-ps-reva}"; ini="$4"
[ -n "$mode" ] && [ -n "$out" ] || { sed -n '2,16p' "$0"; exit 2; }
mkdir -p "$out"; out="$(cd "$out" && pwd)"; J="$out/jobs.txt"; : > "$J"
add() { n=$1; f=$2; shift 2; python -I "$P/Tools/sms/ps_scenario.py" "$out/$n" "$@" > /dev/null || echo "GEN FAIL $n"; echo "$n $f" >> "$J"; }
for k in $(seq 0 23); do add map$k 15000 --map $k --seed $((k+1)) --frames 15000; done
i=0; for d in 00:DC 01:D2 04:D8 13:13 15:D1 17:D7 1E:69 21:61 28:23 2B:D1 34:D3 3A:DD 39:D6; do
  i=$((i+1)); add dng$i 15000 --dungeon 0x${d%%:*}:0x${d##*:} --seed $((100+i)) --level 30 --frames 15000; done
add bat1 15000 --map 0 --keys battle --seed 201 --level 3 --frames 15000
add bat2 15000 --map 1 --keys battle --seed 202 --level 20 --frames 15000
add bat3 15000 --dungeon 0x00:0xDC --keys battle --seed 203 --level 10 --frames 15000
add bat4 15000 --map 2 --keys battle --seed 204 --level 25 --frames 15000
for a in 0 26 52 78 104 130 156; do add npc$a 16000 --npc $a:$((a+26)) --seed $((300+a)) --frames 16000; done
for a in 0 16 32 48; do add item$a 16000 --menu 2:$a:$((a+16)) --seed $((400+a)) --frames 16000; done
for c in 0 1 2 3; do add mag$c 12000 --menu 1:0:8:600:$c --seed $((500+c)) --level 30 --frames 12000; done
printf 'encounter_percent = 200\n' > "$out/enc.ini"
if [ -z "$ini" ]; then ini="$out/qol.ini"
  printf 'exp_multiplier = 50\nmeseta_multiplier = 50\ndamage_dealt = 2\ndamage_received = 0.5\nencounter_percent = 150\nwalk_speed = 4\ndiagonal_walking = 1\nhovercraft_land = 1\nfly_free = 1\nfly_list_all = 1\nsound_chip = fm\nmap_reveal = 1\n' > "$ini"; fi
G="$(ls -d "$P/Games"/*/"$g" | head -1)"
rel="$(grep '^rom' "$G/game.toml" | sed 's/^[^"]*"\(.*\)".*/\1/')"
rom="$(cd "$G/$(dirname "$rel")" && pwd)/$(basename "$rel")"
exe="$(ls "$G/build/"*.exe | head -1)"
VP=$(seq 5000 400 15800 | awk '{printf "%s%d:%d", (NR>1?",":""), $1, (NR%2)+1}')
cat > "$out/one.sh" <<EOF
n=\$1; f=\$2
if [ "$mode" = lockstep ]; then
  case \$n in bat*) export PS_QOL_INI="$out/enc.ini";; esac
  sh "$P/Tools/lockstep.sh" "$g" "$out/run_\$n" \$f "$out/\$n.press.txt" "$out/\$n.poke.txt" 2>&1 | tail -1 | sed 's/^[^ ]* run_//'
else
  mkdir -p "$out/run_\$n"; cd "$out/run_\$n"
  SMS_NO_SAVE=1 PS_QOL_INI="$ini" PS_WORLDMAP_SELFTEST=1 PS_VEH_PRESS="$VP" "$exe" "$rom" --frames \$f \
    --press-file "$out/\$n.press.txt" --poke-file "$out/\$n.poke.txt" > out.txt 2>&1
  echo "\$n: \$(grep -E 'stopped' out.txt | sed 's/.*after //') stack=\$(grep -c '\[stack\]' out.txt)"
fi
EOF
xargs -P 4 -L 1 sh "$out/one.sh" < "$J" | sort -V | tee "$out/results.txt"
if [ "$mode" = lockstep ]; then echo "total $(wc -l < "$out/results.txt"), final SAME $(grep -c 'final SAME' "$out/results.txt"), misses / stack $(grep -v 'misses 0, stack 0' "$out/results.txt" | wc -l)"
else echo "total $(wc -l < "$out/results.txt"), clean $(grep -c "frames; dispatch misses: 0 stack=0" "$out/results.txt")"; fi
