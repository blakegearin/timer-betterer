#!/usr/bin/env bash
#
# Refresh assets/screenshots/ -- the scenes and their quirks live here rather
# than in the Makefile, where shell quoting and make 3.81's pattern-rule
# behaviour made a mess of them.
#
# Each platform gets one tour scene, written straight into the assets folder
# (that is screenshots.sh's --output, which it wipes first). The colour
# platforms shoot four screens -- list, detail, settings, and the accent
# colour picker, all in Picton Blue; diorite shoots the same tour without
# the colour leg; aplite's tour ends on its inline settings rows.
#
# Usage:
#   tools/shots.sh                 all six platforms, concurrently
#   tools/shots.sh basalt          just these platforms
#   tools/shots.sh -p basalt       ditto (repeatable)
#   tools/shots.sh -s              one platform at a time (keeps stdout plain)
#   tools/shots.sh check           diff the empty menu against the upstream
#                                  baselines (aplite, basalt, chalk)
#
# Concurrent runs write their console output to tmp/shots/run-<plat>.log and
# print a one-line ok/FAILED verdict per platform at the end.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ALL=(aplite basalt chalk diorite emery gabbro)
COLOUR=(basalt chalk emery gabbro)
BASELINED=(aplite basalt chalk)
TOUR="$ROOT/tools/scenes/readme-tour.scene"
TOUR_DIO="$ROOT/tools/scenes/readme-diorite.scene"
TOUR_BW="$ROOT/tools/scenes/readme-aplite.scene"
EMPTY="$ROOT/tools/scenes/menu-empty.scene"
SHOOTER="$ROOT/tools/screenshots.sh"
ASSETS="$ROOT/assets/screenshots"
BASEDIR="$ROOT/tmp/baselines/upstream-master"

plats=()
check=0
sequential=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    check) check=1; shift ;;
    -s|--sequential) sequential=1; shift ;;
    -p|--platform) plats+=("$2"); shift 2 ;;
    -h|--help) sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) echo "unknown option $1" >&2; exit 1 ;;
    *) plats+=("$1"); shift ;;
  esac
done

has() { local x="$1"; shift; [[ " $* " == *" $x "* ]]; }

run_plat() {
  local plat="$1"
  local scene=$TOUR
  has "$plat" diorite && scene=$TOUR_DIO
  has "$plat" aplite && scene=$TOUR_BW
  "$SHOOTER" -B -p "$plat" -w -t 12:34:00 -o "$ASSETS/$plat" "$scene"
}

if [[ $check -eq 1 ]]; then
  pebble build >/dev/null
  for plat in "${BASELINED[@]}"; do
    "$SHOOTER" -B -p "$plat" -w -b "$BASEDIR/$plat" -o "$ROOT/tmp/shots/check/$plat" "$EMPTY" || exit 1
  done
  exit 0
fi

[[ ${#plats[@]} -gt 0 ]] || plats=("${ALL[@]}")
for plat in "${plats[@]}"; do
  has "$plat" "${ALL[@]}" || { echo "unknown platform '$plat' -- try: ${ALL[*]}" >&2; exit 1; }
done

# One build for every platform -- `pebble build` compiles all of them anyway,
# and concurrent builds would collide in build/. The runs themselves go in
# parallel: each platform owns its own flash file and its own pypkjs, and
# screenshots.sh only ever kills processes it can match to its platform.
echo "building all platforms"
pebble build >/dev/null

mkdir -p "$ROOT/tmp/shots"
if [[ $sequential -eq 1 ]]; then
  for plat in "${plats[@]}"; do run_plat "$plat"; done
  exit 0
fi

pids=()
for plat in "${plats[@]}"; do
  run_plat "$plat" > "$ROOT/tmp/shots/run-$plat.log" 2>&1 &
  pids+=("$!")
done

# `wait -n` wants bash 4.3; macOS ships 3.2, so wait on each pid by name and
# report in submission order -- the interleaved logs are the cost of speed.
fail=0
for i in "${!pids[@]}"; do
  if wait "${pids[$i]}"; then
    echo "${plats[$i]}: ok"
  else
    echo "${plats[$i]}: FAILED -- see tmp/shots/run-${plats[$i]}.log" >&2
    tail -3 "$ROOT/tmp/shots/run-${plats[$i]}.log" >&2 | sed 's/^/  /'
    fail=1
  fi
done
exit $fail
