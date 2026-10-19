#!/usr/bin/env bash
# Stages and zips the two Windows downloads from a build.
#
#   bash tools/release/package_windows.sh <build dir> <Godot_v4.1.4-stable_win64.exe>
#        <folder with Godot's LICENSE.txt and COPYRIGHT.txt> <SDL2.dll> <version> <out dir>
#
# <out>/bsht-extract-<version>-windows.zip: bsht-extract/ with bsht-extract.exe,
#   godot/godot.exe and Godot's licence, exporters/ (exactly what
#   bsht-extract --list-exporters names), LICENSE, THIRD_PARTY.md, README.txt.
# <out>/bsht-desktop-<version>-windows.zip: bsht-desktop/ with bsht_desktop.exe,
#   SDL2.dll, data/menu_logo.rgba, data/achievements_en.json, LICENSE,
#   THIRD_PARTY.md, README.txt.
# The menu logo comes from assets/baked (python tools/bake_menu_logo.py).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:?build dir}"
GODOT="${2:?Godot exe}"
GODOT_LICENCE="${3:?folder holding the LICENSE.txt and COPYRIGHT.txt of Godot}"
SDL="${4:?SDL2.dll}"
VERSION="${5:?version, e.g. v1.0.0}"
OUT="${6:?out dir}"
PY="${PYTHON:-python}"

EXTRACT_EXE="$BUILD/src/bsht-extract.exe"
DESKTOP_EXE="$BUILD/src/bsht_desktop.exe"
LOGO="$REPO/assets/baked/menu_logo.rgba"
for f in "$EXTRACT_EXE" "$DESKTOP_EXE" "$GODOT" "$GODOT_LICENCE/LICENSE.txt" "$GODOT_LICENCE/COPYRIGHT.txt" "$SDL" "$LOGO"; do
    [ -f "$f" ] || { echo "package: missing $f"; exit 2; }
done

STAGE="$OUT/stage"
rm -rf "$STAGE"
EX="$STAGE/bsht-extract"
DT="$STAGE/bsht-desktop"
mkdir -p "$EX/godot" "$EX/exporters" "$DT/data"

cp "$EXTRACT_EXE" "$EX/"
cp "$GODOT" "$EX/godot/godot.exe"
cp "$GODOT_LICENCE/LICENSE.txt" "$GODOT_LICENCE/COPYRIGHT.txt" "$EX/godot/"
mapfile -t SCRIPTS < <("$EXTRACT_EXE" --list-exporters | tr -d '\r')
[ "${#SCRIPTS[@]}" -gt 0 ] || { echo "package: bsht-extract --list-exporters printed nothing"; exit 1; }
for s in "${SCRIPTS[@]}"; do cp "$REPO/tools/godot/$s" "$EX/exporters/"; done
cp "$REPO/tools/release/readme_extract.txt" "$EX/README.txt"

cp "$DESKTOP_EXE" "$SDL" "$DT/"
cp "$LOGO" "$REPO/tools/achievements_en.json" "$DT/data/"
cp "$REPO/tools/release/readme_desktop.txt" "$DT/README.txt"

# The licences of what the binaries are built from (THIRD_PARTY.md).
for d in "$EX" "$DT"; do
    cp "$REPO/LICENSE" "$REPO/THIRD_PARTY.md" "$d/"
    mkdir -p "$d/licenses"
    cp "$REPO"/licenses/*.txt "$d/licenses/"
    for lib in reproc picosha2 xatlas libogg font8x8; do
        cp "$REPO/third_party/$lib/LICENSE" "$d/licenses/$lib.txt"
    done
done

rm -f "$OUT/bsht-extract-$VERSION-windows.zip" "$OUT/bsht-desktop-$VERSION-windows.zip"
(cd "$STAGE" && "$PY" -m zipfile -c "../bsht-extract-$VERSION-windows.zip" bsht-extract/)
(cd "$STAGE" && "$PY" -m zipfile -c "../bsht-desktop-$VERSION-windows.zip" bsht-desktop/)
echo "package: ${#SCRIPTS[@]} exporters; $(ls "$OUT"/*-"$VERSION"-windows.zip | xargs -n1 basename | tr '\n' ' ')"
