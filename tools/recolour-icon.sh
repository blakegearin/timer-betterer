#!/usr/bin/env bash
#
# Re-render the store icons (timer-icon-*) on a different background colour.
#
# The icons are a black/white hourglass on a flat sky-blue (#56ACD7) field,
# with anti-aliasing only between glyph and background. Instead of a naive
# colour swap (which leaves blue fringes on the blended edge pixels), we
# apply the exact affine RGB map that fixes black, white and every grey,
# while sending the background colour to the new accent. Any pixel is
# c*bg + (1-c)*grey, so it maps to c*accent + (1-c)*grey -- the halo
# recomputes itself.
#
# The map: with d = B - G (zero for every grey, dd for the background),
#   R' = R + dr/d * d    where dr = accent - bg per channel
#   G' = G + dg/d * d    bg lands exactly on the accent, greys stay put
#   B' = B + db/d * d
#
# Usage: recolour-icon.sh <src.png> <dst.png> <accent-hex>
#   tools/recolour-icon.sh assets/timer-icon-48x48.png \
#       assets/timer-betterer-icon-48x48.png 8EE59E
set -euo pipefail

src=$1
dst=$2
accent=${3#\#}

size=$(magick identify -format "%wx%h" "$src")

magick "$src" -depth 8 -alpha on RGBA:- |
	python3 -c '
import sys
h = sys.argv[1]
ar, ag, ab = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
br, bg, bb = 86, 172, 215  # the sky blue the icons were drawn on
dr, dg, db = ar - br, ag - bg, ab - bb
dd = bb - bg  # background blue minus green: the divisor
def half_up(num, den):  # round(num/den) with ties away from zero
    return (2 * num + den) // (2 * den) if num >= 0 else -((-2 * num + den) // (2 * den))
data = bytearray(sys.stdin.buffer.read())
for i in range(0, len(data), 4):
    d = data[i + 2] - data[i + 1]
    data[i] = min(255, max(0, data[i] + half_up(dr * d, dd)))
    data[i + 1] = min(255, max(0, data[i + 1] + half_up(dg * d, dd)))
    data[i + 2] = min(255, max(0, data[i + 2] + half_up(db * d, dd)))
sys.stdout.buffer.write(data)
' "$accent" |
	magick -size "$size" -depth 8 RGBA:- "$dst.tmp.png"

# The originals ship as RGB (144) and RGBA (48); match the source's alpha use.
if [ "$(magick identify -format "%[opaque]" "$src")" = "True" ]; then
	magick "$dst.tmp.png" -alpha off "$dst" && rm "$dst.tmp.png"
else
	mv "$dst.tmp.png" "$dst"
fi

echo "$dst"
