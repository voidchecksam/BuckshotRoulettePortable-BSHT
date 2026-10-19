#!/usr/bin/env bash
# Frames of the reference game's round-1 ceremony (capture_frames.gd), from
# the WINDOWED binary. Frame N matches the port's --shot-delay (N - 3) / 60.
#
#   tools/godot/capture_reference.sh 363,543,783 build/shots/godot
set -euo pipefail
. "$(dirname "$0")/ref_game.sh"
GODOT="${GODOT:-$REPO/external/godot/Godot_v4.1.4-stable_win64.exe}"
frames="$1"
out="$(cygpath -m "$(mkdir -p "$2" && cd "$2" && pwd)")"
shift 2
"$GODOT" --fixed-fps 60 --path "$REF" --script res://tools/godot/capture_frames.gd -- "$frames" "$out" "$@"
