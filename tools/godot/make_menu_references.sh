#!/usr/bin/env bash
# The menu, intro, ending and heaven references only (make_references.sh does
# the work; these all run windowed in a private user://).
#
#   tools/godot/make_menu_references.sh [scenario names...]
set -euo pipefail
names=("$@")
[ ${#names[@]} -eq 0 ] && names=(menu_boot menu_options menu_start frames_menu_credits frames_menu_ja intro_walk intro_revival intro_crt_boot waiver_name ending_briefcase ending_true_death ending_don_score heaven_fly)
exec "$(dirname "$0")/make_references.sh" "${names[@]}"
