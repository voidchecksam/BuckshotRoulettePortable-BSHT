#!/usr/bin/env bash
# Runs one of our .gd tools headless on the reference game, with user:// in a
# private folder and Steam stubbed (ref_game.sh).
#
#   tools/godot/run_private.sh res://tools/godot/<script>.gd [-- args...]
set -euo pipefail
. "$(dirname "$0")/ref_game.sh"
GODOT="${GODOT:-$REPO/external/godot/Godot_v4.1.4-stable_win64_console.exe}"
script="$1"
shift
"$GODOT" --headless --path "$REF" --script "$script" "$@"
