# Sourced by the scripts that run Godot on the reference game (the v2.2 copy
# `bsht-extract <exe> --unpack-to external/game-v2.2` writes). Sets REF and
# REPO, keeps user:// private and Steam off, and copies our .gd tools in.
#
#   . tools/godot/ref_game.sh
#   "$GODOT" --path "$REF" --script res://tools/godot/dump_state.gd -- ...

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REF="${BSHT_REFERENCE_GAME:-$REPO/external/game-v2.2/game}"

if [ ! -f "$REF/project.binary" ]; then
    echo "no reference game at $REF" >&2
    echo "make it with: bsht-extract \"<Buckshot Roulette.exe>\" --unpack-to external/game-v2.2" >&2
    exit 2
fi
if [ "$(head -n 1 "$REF/override.cfg" 2>/dev/null | tr -d '\r')" != "; written by bsht-extract" ]; then
    echo "$REF/override.cfg was not written by bsht-extract; refusing to run the game there" >&2
    exit 2
fi

# The parity user folder (dump_state.gd checks the name), Steam stubbed.
printf '; written by bsht-extract\n\n[application]\n\nconfig/use_custom_user_dir=true\nconfig/custom_user_dir_name="bsht_parity_user"\n\n[autoload]\n\nSteam="*res://tools/godot/steam_stub.gd"\nGlobalSteam="*res://tools/godot/steam_off.gd"\n' > "$REF/override.cfg"

mkdir -p "$REF/tools/godot"
cp "$REPO"/tools/godot/*.gd "$REF/tools/godot/"

mkdir -p "$REPO/external/godot/appdata"
APPDATA="$(cygpath -w "$REPO/external/godot/appdata")"
export APPDATA
