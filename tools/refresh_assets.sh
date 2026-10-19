#!/usr/bin/env bash
# Makes assets/extracted and assets/baked what bsht-extract wrote, for the
# desktop build and the tests.
#
#   build-o2/src/bsht-extract.exe "<Buckshot Roulette.exe>" --out .scratch/v22out --keep-work
#   bash tools/refresh_assets.sh .scratch/v22out
#
# Kept from the old assets/baked: menu_logo.rgba (the port's own logo,
# tools/bake_menu_logo.py). Added for the desktop only: the mouse cursors
# (export_cursors.gd on the reference game; the Vita has no cursor).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$(cd "$1" && pwd)/.work/out"
[ -d "$SRC/extracted" ] && [ -d "$SRC/baked" ] || { echo "no .work/out in $1: run bsht-extract with --keep-work" >&2; exit 2; }

logo="$REPO/assets/baked/menu_logo.rgba"
tmp_logo=""
if [ -f "$logo" ]; then
    mkdir -p "$REPO/.scratch"
    tmp_logo="$REPO/.scratch/menu_logo.rgba.keep"
    cp "$logo" "$tmp_logo"
fi

rm -rf "$REPO/assets/extracted" "$REPO/assets/baked"
cp -r "$SRC/extracted" "$REPO/assets/extracted"
cp -r "$SRC/baked" "$REPO/assets/baked"
# The renderer reads the smoke texture beside the rooms.
cp "$REPO/assets/extracted/particle_smoke.rgba" "$REPO/assets/baked/particle_smoke.rgba"
if [ -n "$tmp_logo" ]; then mv "$tmp_logo" "$logo"; else echo "note: no menu_logo.rgba (tools/bake_menu_logo.py)" >&2; fi

bash "$REPO/tools/godot/run_private.sh" res://tools/godot/export_cursors.gd -- \
    --out="$(cygpath -m "$REPO/assets/extracted")" > /dev/null
[ -f "$REPO/assets/extracted/cursors/cursors.json" ] || { echo "cursors were not exported" >&2; exit 1; }
echo "assets/ now holds $(sed -n 's/.*"game": *"\([^"]*\)".*/\1/p' "$1/game/manifest.json" | head -1) data from $1"
