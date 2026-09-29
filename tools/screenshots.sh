#!/usr/bin/env bash
#
# Drive the Pebble emulator through a scripted scene and capture a screenshot at
# each named step. Two uses:
#
#   1. Look at the app on a platform you do not own (round chalk, 1-bit aplite,
#      big-font emery) without pressing buttons by hand.
#   2. Visual regression: re-run a scene after a change and diff every shot
#      against a committed baseline.
#
# Usage:
#   tools/screenshots.sh [options] <scene-file>
#
#   -p, --platform PLAT   aplite|basalt|chalk|diorite|emery|gabbro (default basalt)
#   -o, --out DIR         where shots land (default tmp/shots/<scene>/<platform>)
#   -b, --baseline DIR    compare each shot against DIR/<name>.png and report drift
#   -w, --wipe            wipe emulator data first, so the scene starts from a
#                         known empty state (no saved timers, no saved settings)
#   -k, --keep-running    leave the emulator up when the scene ends
#   -n, --no-install      do not build or install this app; drive whatever is
#                         already on screen. Use it to capture the firmware's
#                         own UI (the system settings, say) for reference.
#   -B, --no-build        still install, but skip `pebble build` -- for when
#                         the caller built once for all platforms (shots.sh).
#   -t, --time HH:MM:SS   pin the emulated clock after the app is up (default
#                         10:09:00; pass "none" to leave the clock alone). The
#                         status bar ticks by a minute across long scenes, so
#                         --baseline diffs also chop the top 20px status band.
#
# Scene file: one command per line, '#' starts a comment.
#
#   press <back|up|select|down> [count]   press a button, count times
#   hold  <button> [ms]                   long press (default 1000ms)
#   wait  <seconds>                       let an animation settle
#   fasttick <pin> <button> <name>        pin, press, hold, capture -- one
#                                         session, for running countdowns
#   shot  <name>                          capture <name>.png
#   note  <text>                          print a line to the console
#
# Requires: the pebble tool on PATH, and ImageMagick for the contact sheet and
# the baseline diff (both are skipped with a warning if magick is missing).
#
set -euo pipefail

PLATFORM=basalt
OUT=""
BASELINE=""
WIPE=0
KEEP=0
NO_INSTALL=0
NO_BUILD=0
FIXED_TIME=10:09:00   # the clock every Pebble marketing shot uses
SETTLE=1.3        # seconds after a press before the display is worth capturing
STATUS_BAND=20    # px of status bar ignored when diffing against a baseline

die() { echo "error: $*" >&2; exit 1; }

# pypkjs -- the phone-sim that delivers buttons and screenshots -- dies with
# its websocket whenever the firmware panics, and a dead pypkjs turns every
# later emu-button into "connection refused". Worse, the tool's own idea of
# which emulators are alive can drift: zombies from a crashed run happily
# share a flash file with the next one. So teardown kills by process table --
# and only *this* platform's processes, matched on the SDK data paths in
# their args, so runs can go in parallel; `pebble kill` has no --emulator
# flag and stops every emulator on the machine.
kill_emus() {
  pkill -f "qemu-pebble.*/$PLATFORM/" 2>/dev/null || true
  pkill -f "m pypkjs.*/$PLATFORM" 2>/dev/null || true
  for _ in $(seq 40); do
    pgrep -f "qemu-pebble.*/$PLATFORM/" >/dev/null \
      || pgrep -f "m pypkjs.*/$PLATFORM" >/dev/null || break
    sleep 0.5
  done
  pkill -9 -f "qemu-pebble.*/$PLATFORM/" 2>/dev/null || true
  pkill -9 -f "m pypkjs.*/$PLATFORM" 2>/dev/null || true
}

# Run an emu command quietly, but print the tool's complaint if it fails:
# bare set -e used to exit the script mid-scene without a word. Redirect to
# files, never command substitution -- `pebble install` daemonises pypkjs as
# its child, and an inherited broken pipe kills pypkjs the moment it next
# writes anything. EMU_RETRIES retries the command with a wait between tries
# (a firmware still formatting a wiped flash refuses the first install).
try_emu() {
  local tries=${EMU_RETRIES:-2} nap=${EMU_RETRY_SLEEP:-2} n=0
  until emu "$@" >"$TMPDIR_/emu.out" 2>"$TMPDIR_/emu.err"; do
    n=$((n+1))
    if [[ $n -ge $tries ]]; then
      printf 'error: pebble %s --emulator %s failed\n' "$*" "$PLATFORM" >&2
      tail -3 "$TMPDIR_/emu.err" "$TMPDIR_/emu.out" 2>/dev/null | grep . | sed 's/^/  /' >&2
      exit 1
    fi
    sleep "$nap"
  done
}

# The tool's running-emulator registry (pb-emulator.json) is rewritten
# non-atomically, so a concurrent pebble command can read it mid-write and
# die of a JSON decode error. Cross that bridge one party at a time: boots
# and installs happen under a mkdir lock, and a crashed run releases it via
# the EXIT trap. mkdir/rmdir is the only lock BSD gives us without flock.
BOOT_LOCK="${TMPDIR:-/tmp}/pb-shots-boot.lock"
acquire_boot_lock() {
  local waited=0
  while ! mkdir "$BOOT_LOCK" 2>/dev/null; do
    [[ $waited -ge 300 ]] && die "another screenshots.sh has held the boot lock for 5 minutes"
    sleep 1; waited=$((waited+1))
  done
  BOOT_LOCK_HELD=1
}
release_boot_lock() { [[ ${BOOT_LOCK_HELD:-0} -eq 1 ]] && rmdir "$BOOT_LOCK" 2>/dev/null; BOOT_LOCK_HELD=0; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--platform) PLATFORM="$2"; shift 2 ;;
    -o|--out) OUT="$2"; shift 2 ;;
    -b|--baseline) BASELINE="$2"; shift 2 ;;
    -w|--wipe) WIPE=1; shift ;;
    -k|--keep-running) KEEP=1; shift ;;
    -n|--no-install) NO_INSTALL=1; shift ;;
    -B|--no-build) NO_BUILD=1; shift ;;
    -t|--time) FIXED_TIME="$2"; shift 2 ;;
    -h|--help) sed -n '2,34p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) die "unknown option $1" ;;
    *) SCENE="$1"; shift ;;
  esac
done

[[ -n "${SCENE:-}" ]] || die "no scene file given (try --help)"
[[ -f "$SCENE" ]] || die "no such scene file: $SCENE"
command -v pebble >/dev/null || die "the pebble tool is not on PATH"
HAVE_MAGICK=1
command -v magick >/dev/null || { HAVE_MAGICK=0; echo "warning: no ImageMagick, skipping contact sheet and diffs" >&2; }

SCENE_NAME="$(basename "${SCENE%.*}")"
OUT="${OUT:-tmp/shots/$SCENE_NAME/$PLATFORM}"
mkdir -p "$OUT"
rm -f "$OUT"/*.png

TMPDIR_="$(mktemp -d)"
# Cleanup in the trap, not at the foot of the script: a run that dies
# mid-scene used to leave its qemu and pypkjs alive to fight the next run's
# freshly-booted stack over one flash file.
cleanup() {
  rm -rf "$TMPDIR_"
  release_boot_lock
  [[ $KEEP -eq 1 ]] || kill_emus
}
trap cleanup EXIT

emu() { pebble "$@" --emulator "$PLATFORM"; }

if [[ $WIPE -eq 1 ]]; then
  # `pebble wipe` leaves the emulated flash alone, and that flash is where the
  # app's persisted timers and settings live -- so drop it and let the emulator
  # re-create a pristine one on the next launch. A live qemu writes the flash
  # back on exit, hence the kill-and-wait first: racing it produced a
  # half-written flash, a firmware panic mid-scene, and "Install an app to
  # continue" where a shot should be.
  echo "wiping $PLATFORM emulator data"
  kill_emus
  SDK_DATA="${HOME}/Library/Application Support/Pebble SDK"
  rm -f "$SDK_DATA"/*/"$PLATFORM"/qemu_spi_flash.bin 2>/dev/null || true
  rm -f "$SDK_DATA"/*/"$PLATFORM"/timeline.db 2>/dev/null || true
  rm -rf "$SDK_DATA"/*/"$PLATFORM"/localstorage 2>/dev/null || true
  rm -rf "$SDK_DATA"/*/"$PLATFORM"/app_cache 2>/dev/null || true
fi

if [[ $NO_INSTALL -eq 0 && $NO_BUILD -eq 0 ]]; then
  echo "building for $PLATFORM"
  pebble build >/dev/null
fi

# Attach the log stream *before* installing, so APP_LOG output from the app's
# own startup (heap numbers, errors) lands in the file too. `pebble logs` boots
# the emulator if it is not already up. PYTHONUNBUFFERED because the stream is
# polled for the app's startup marker below, and python block-buffers stdout
# into a file -- without it, the marker would only appear when the stream dies.
#
# Boot and install are the window where two runs' pebble processes would
# trample each other's registry writes and half-dead pids, so they happen one
# run at a time under the lock; the scene itself -- buttons and shots, which
# only read the registry -- runs unlocked in parallel.
acquire_boot_lock
( PYTHONUNBUFFERED=1 emu logs > "$OUT/logs.txt" 2>&1 & )
# Wait for that boot to give us a qemu -- and wait *passively*. Probing with
# ping or screenshot is a trap: when those commands find no live device they
# boot one themselves, and two emulators sharing one flash file is exactly
# the corruption this section exists to avoid. Retry the boot up to three
# times: a pid killed moments ago can still read as alive, and then logs
# "attaches" to the corpse's refused port instead of booting a fresh stack.
for attempt in 1 2 3; do
  for _ in $(seq 30); do
    pgrep -f "qemu-pebble.*/$PLATFORM/" >/dev/null && break
    sleep 1
  done
  pgrep -f "qemu-pebble.*/$PLATFORM/" >/dev/null && break
  [[ $attempt -lt 3 ]] && echo "  boot attempt $attempt found no emulator coming up; retrying" >&2
  kill_emus
  ( PYTHONUNBUFFERED=1 emu logs > "$OUT/logs.txt" 2>&1 & )
done
pgrep -f "qemu-pebble.*/$PLATFORM/" >/dev/null \
  || die "pebble logs did not boot a $PLATFORM emulator"
sleep "${BOOT_WAIT:-10}"   # first boot off a wiped flash formats it slowly

if [[ $NO_INSTALL -eq 0 ]]; then
  echo "installing on $PLATFORM"
  EMU_RETRIES=3 EMU_RETRY_SLEEP=10 try_emu install
  # The app's JS says "JS ready!" exactly once per launch, and the log stream
  # catches it. That -- not a guessed sleep -- is the moment the app owns the
  # screen and the buttons are aimed at it. Presses flung into a still-booting
  # app used to queue up and fire as a flood, landing the scene anywhere.
  for _ in $(seq 60); do
    grep -q "JS ready" "$OUT/logs.txt" 2>/dev/null && break
    sleep 1
  done
  grep -q "JS ready" "$OUT/logs.txt" 2>/dev/null \
    || { echo "warning: no startup marker in $OUT/logs.txt within 60s" >&2; sleep 5; }
fi
sleep "$SETTLE"

if [[ "$FIXED_TIME" != "none" ]]; then
  # Freeze the status bar clock after the firmware is answering commands --
  # sent into a still-booting emulator it is silently dropped. This is only a
  # head start: pypkjs re-syncs the firmware to the phone's wall clock within
  # half a minute, so the shot handler below re-pins before every capture.
  emu emu-set-time "$FIXED_TIME" >/dev/null 2>&1 \
    || echo "warning: could not pin the clock to $FIXED_TIME" >&2
fi
release_boot_lock

step=0
drift=0
while IFS= read -r line || [[ -n "$line" ]]; do
  line="${line%%#*}"
  line="$(echo "$line" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
  [[ -z "$line" ]] && continue
  cmd="$(echo "$line" | awk '{print $1}')"
  arg="$(echo "$line" | awk '{print $2}')"
  rest="$(echo "$line" | cut -s -d' ' -f2-)"
  case "$cmd" in
    press)
      count="${rest#* }"; [[ "$count" == "$rest" ]] && count=1
      for ((i=0; i<count; i++)); do
        try_emu emu-button click "$arg"
        sleep "$SETTLE"
      done
      ;;
    hold)
      ms="${rest#* }"; [[ "$ms" == "$rest" ]] && ms=1000
      try_emu emu-button push "$arg"
      sleep "$(echo "$ms" | awk '{print $1/1000}')"
      try_emu emu-button release "$arg"
      sleep "$SETTLE"
      ;;
    wait) sleep "$arg" ;;
    note) echo "  -- $rest" ;;
    fasttick)
      # fasttick <pinA> <button> <name>: pin A, press, re-pin FIXED_TIME
      # across a redraw tick, capture -- all in one websocket, by
      # tools/fasttick.py run on the pebble tool's own python. The only
      # way to catch a *running* countdown on a pinned clock: a CLI call
      # per op spends more time in pypkjs' re-sync than in the pin's
      # island, and every leaked tick expires the timer (Time's Up!).
      [[ "$FIXED_TIME" != "none" ]] || die "fasttick needs a -t pin time (FIXED_TIME=none)"
      step=$((step+1))
      name="$(printf '%02d-%s' "$step" "$(echo "$line" | awk '{print $4}')")"
      "$(head -n1 "$(command -v pebble)" | sed 's/^#!//')" \
        "$(dirname "${BASH_SOURCE[0]}")/fasttick.py" \
        "$PLATFORM" "$arg" "$(echo "$line" | awk '{print $3}')" "$FIXED_TIME" "$OUT/$name.png" \
        || die "fasttick failed"
      echo "  shot $name (fasttick)"
      ;;
    shot)
      step=$((step+1))
      name="$(printf '%02d-%s' "$step" "$arg")"
      # Re-pin the clock right before capture. pypkjs re-syncs the firmware to
      # the phone's wall clock within ~30s of any set, so the boot-time pin
      # goes stale mid-scene; a pin taken a second before the frame is fresh.
      # Twice, because a pin that races the re-sync is accepted and discarded.
      if [[ "$FIXED_TIME" != "none" ]]; then
        emu emu-set-time "$FIXED_TIME" >/dev/null 2>&1 || true
        sleep 0.5
        emu emu-set-time "$FIXED_TIME" >/dev/null 2>&1 || true
      fi
      try_emu screenshot "$OUT/$name.png" --no-open
      echo "  shot $name"
      if [[ -n "$BASELINE" && -f "$BASELINE/$name.png" && $HAVE_MAGICK -eq 1 ]]; then
        # The top band is the firmware's status bar -- its clock is not our UI
        # and would otherwise make every shot differ. Chop it off both sides of
        # the comparison rather than trying to freeze the emulated clock, which
        # emu-set-time does not do reliably.
        magick "$BASELINE/$name.png" -gravity North -chop 0x"$STATUS_BAND" "$TMPDIR_/base.png"
        magick "$OUT/$name.png" -gravity North -chop 0x"$STATUS_BAND" "$TMPDIR_/shot.png"
        px="$(magick compare -metric AE "$TMPDIR_/base.png" "$TMPDIR_/shot.png" null: 2>&1 || true)"
        px="${px%%[ (]*}"   # compare prints "553 (0.008)"; keep the count
        if [[ "$px" != "0" ]]; then
          echo "     CHANGED vs baseline: $px pixels differ"
          magick compare "$TMPDIR_/base.png" "$TMPDIR_/shot.png" "$OUT/$name.diff.png" 2>/dev/null || true
          drift=$((drift+1))
        fi
      fi
      ;;
    *) die "unknown scene command: $cmd" ;;
  esac
done < "$SCENE"

if [[ $HAVE_MAGICK -eq 1 ]]; then
  # shellcheck disable=SC2046
  magick $(ls "$OUT"/*.png | grep -v '\.diff\.png$' | sort) +append \
    -bordercolor gray -border 2 "$OUT/contact-sheet.png" 2>/dev/null || true
fi

echo "wrote $step shots to $OUT"
if [[ -n "$BASELINE" ]]; then
  if [[ $drift -eq 0 ]]; then
    echo "no visual drift against $BASELINE"
  else
    echo "$drift shot(s) differ from $BASELINE -- see the .diff.png files"
    exit 1
  fi
fi
