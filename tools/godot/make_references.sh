#!/usr/bin/env bash
# Regenerates parity reference dumps (assets/reference/*.json) from the
# reference game, running tools/godot/dump_state.gd once per scenario.
#
#   tools/godot/make_references.sh [scenario names...]   (default: every scenario)
#
# The windowed binary runs input scenarios (headless Godot never delivers
# parsed input to _input) and every menu, intro, ending and heaven scenario
# (they grab frames); the rest run headless.
set -euo pipefail
. "$(dirname "$0")/ref_game.sh"
G="${GODOT:-$REPO/external/godot/Godot_v4.1.4-stable_win64_console.exe}"
GW="${GODOT_WINDOWED:-$REPO/external/godot/Godot_v4.1.4-stable_win64.exe}"
OUT="$(cygpath -m "$REPO/assets/reference")"
SHOTS="$(cygpath -m "$REPO/build/shots")"
mkdir -p "$REPO/assets/reference" "$REPO/build/shots"

names=("$@")
if [ ${#names[@]} -eq 0 ]; then
    for s in "$REPO"/tests/parity/scenarios/*.json; do names+=("$(basename "$s" .json)"); done
fi
for n in "${names[@]}"; do
    file="$REPO/tests/parity/scenarios/$n.json"
    s="$(cygpath -m "$file")"
    bin="$G"
    flags=(--headless)
    case "$n" in menu_*|intro_*|waiver_*|ending_*|heaven_*|frames_*) bin="$GW"; flags=() ;; esac
    if grep -q '"timeline"' "$file"; then bin="$GW"; flags=(); fi
    "$bin" "${flags[@]}" --fixed-fps 60 --path "$REF" --script res://tools/godot/dump_state.gd -- "$s" --ref-dir="$OUT" --shots-dir="$SHOTS"
done
