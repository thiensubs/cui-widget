#!/usr/bin/env bash
# Render the animated flow diorama to a GIF (+ mp4) using ffmpeg.
#
# CUI exports PNG only (cui_write_png), so we render a deterministic frame
# sequence with cuiw-render (the @keyframes clock is virtual and reproducible)
# and hand the frames to ffmpeg, which encodes the GIF via a two-pass palette
# (palettegen/paletteuse) for accurate color, plus an H.264 mp4.
#
# Usage: tools/flow-gif.sh [fps] [cycles]
#   fps     frame rate (default 20)
#   cycles  how many animation cycles to capture (default 1; the flow cycle is 2.4 s)
# Outputs: docs/flow.gif and docs/flow.mp4
set -euo pipefail

cd "$(dirname "$0")/.."

FPS="${1:-20}"
CYCLES="${2:-1}"
CYCLE_S=2.4                                   # must match CYCLE_S in tools/flow.py
FRAMES=$(python3 -c "print(int($FPS * $CYCLE_S * $CYCLES))")
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

mkdir -p docs

echo "== rendering $FRAMES frames @ ${FPS}fps (cycle ${CYCLE_S}s x${CYCLES}) =="
./build/cuiw-render docs/flow.json "$TMP/%04d.png" 620 2 dark 0 "$FRAMES" "$FPS" >/dev/null

echo "== encoding GIF (two-pass palette) =="
ffmpeg -y -loglevel error -framerate "$FPS" -i "$TMP/%04d.png" \
  -vf "palettegen=stats_mode=diff" "$TMP/palette.png"
ffmpeg -y -loglevel error -framerate "$FPS" -i "$TMP/%04d.png" -i "$TMP/palette.png" \
  -lavfi "paletteuse=dither=bayer:bayer_scale=3" -loop 0 docs/flow.gif

echo "== encoding mp4 =="
ffmpeg -y -loglevel error -framerate "$FPS" -i "$TMP/%04d.png" \
  -vf "pad=ceil(iw/2)*2:ceil(ih/2)*2" -c:v libx264 -pix_fmt yuv420p -crf 20 \
  -movflags +faststart docs/flow.mp4

echo "== done =="
ls -lh docs/flow.gif docs/flow.mp4
