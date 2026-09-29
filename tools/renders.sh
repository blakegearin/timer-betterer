#!/usr/bin/env bash
#
# Composite the platform screenshots into per-device renders: the emulator
# frame pasted into the official Pebble press artwork, so the README shows
# each screen on the watch that would show it.
#
# The geometry is lifted from the developer.repebble.com stylesheet's
# .pebble-screenshot rules: every device declares a box (width/height) and a
# screen rectangle, and the screenshot sits at the padding that centres the
# screen in the box, nudged by the device's own offset-x/offset-y (the site's
# trick for lining the img up with each SVG's artwork). Renders are built at
# twice the box so a 0.5x display stays crisp, which also turns every padding
# formula into integer pixels: at S=2 the offset is simply W - sw + ox.
#
# The round devices clip their screenshot to a circle -- their screens are
# discs, and square corners would spill onto the bezel.
#
# The SVGs are vendored under tools/renders/svg/ from
# https://developer.repebble.com/assets/images/pebbles/ (keep the official
# file names; they are the provenance).
#
# Usage:
#   tools/renders.sh                 every platform with screenshots
#   tools/renders.sh -p basalt       just these (repeatable)
#
# Requires ImageMagick and rsvg-convert.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ALL=(aplite basalt chalk diorite emery gabbro)
SHOTS="$ROOT/assets/screenshots"
SVGS="$ROOT/tools/renders/svg"
OUT_ROOT="$ROOT/assets/renders"
SCALE=2

die() { echo "error: $*" >&2; exit 1; }

command -v magick >/dev/null || die "ImageMagick is not on PATH"
command -v rsvg-convert >/dev/null || die "rsvg-convert is not on PATH (brew install librsvg)"

plats=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--platform) plats+=("$2"); shift 2 ;;
    -h|--help) sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) echo "unknown option $1" >&2; exit 1 ;;
    *) plats+=("$1"); shift ;;
  esac
done
[[ ${#plats[@]} -gt 0 ]] || plats=("${ALL[@]}")

# Build the device artwork: the SVG fitted into the 2x box (rsvg-convert
# keeps the aspect ratio, like background-size: contain) then centred on the
# transparent canvas.
build_frame() {
  local svg="$1" cw="$2" ch="$3" out="$4" tmp="$5"
  rsvg-convert -w "$cw" -h "$ch" "$svg" -o "$tmp/fit.png"
  magick -size "${cw}x${ch}" xc:none "$tmp/fit.png" -gravity center -composite "$out"
}

render_plat() {
  local plat="$1"
  local svg W H sw sh ox oy round=0
  case "$plat" in
    aplite)  svg=pebble-black.svg               W=246 H=403 sw=144 sh=168 ox=0  oy=-10 ;;
    basalt)  svg=pebble-time-black.svg          W=268 H=403 sw=144 sh=168 ox=0  oy=-1  ;;
    diorite) svg=pebble2-black.svg              W=221 H=402 sw=144 sh=168 ox=0  oy=2   ;;
    chalk)   svg=pebble-time-round-black-20.svg W=291 H=426 sw=180 sh=180 ox=0  oy=0 round=1 ;;
    emery)   svg=core-time2-red.svg             W=293 H=432 sw=200 sh=228 ox=0  oy=0   ;;
    gabbro)  svg=core-time-round2-black-20.svg  W=330 H=460 sw=260 sh=260 ox=-1 oy=0 round=1 ;;
    *) die "no device artwork for platform '$plat'" ;;
  esac
  [[ -d "$SHOTS/$plat" ]] || { echo "$plat: no screenshots, skipped"; return; }

  local cw=$((W * SCALE)) ch=$((H * SCALE))
  local pw=$((sw * SCALE)) ph=$((sh * SCALE))
  # the 2x top-left of the screen in the box: 2 * (box - screen + offset) / 2
  local px=$((W - sw + ox)) py=$((H - sh + oy))

  local out="$OUT_ROOT/$plat"
  mkdir -p "$out"
  rm -f "$out"/*.png
  local tmp; tmp="$(mktemp -d)"
  build_frame "$SVGS/$svg" "$cw" "$ch" "$tmp/frame.png" "$tmp"

  local shot name
  for shot in "$SHOTS/$plat"/0*.png; do
    [[ -f "$shot" ]] || continue
    name="$(basename "$shot")"
    # point filter: the screenshots are pixel art, half-pixels would smear
    magick "$shot" -filter point -resize "${pw}x${ph}!" "$tmp/screen.png"
    if [[ $round -eq 1 ]]; then
      # DstIn keeps the destination (the screenshot) only where the source
      # (the white circle) is opaque -- order matters: reversed, the circle
      # itself is what survives.
      magick "$tmp/screen.png" \
        \( -size "${pw}x${ph}" xc:none -fill white \
          -draw "ellipse $((pw / 2)),$((ph / 2)) $((pw / 2)),$((ph / 2)) 0,360" \) \
        -compose DstIn -composite "$tmp/screen-clip.png"
      mv "$tmp/screen-clip.png" "$tmp/screen.png"
    fi
    magick "$tmp/frame.png" "$tmp/screen.png" -geometry "+${px}+${py}" -composite "$out/$name"
    echo "$plat: $name"
  done
  rm -rf "$tmp"
}

for plat in "${plats[@]}"; do
  render_plat "$plat"
done
