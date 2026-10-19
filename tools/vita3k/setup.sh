#!/usr/bin/env bash
# Vita3K in portable mode, entirely inside the repo (vita3k/). Logs each step.
#
# Sources of truth: github.com/Vita3K/Vita3K, vita3k.org/quickstart.
# Portable mode: Vita3K's app_init.cpp uses a `portable` directory beside the exe for its
# fs, config, logs and cache instead of %APPDATA%, so nothing lands on C:.
# Inputs you supply in vita3k/: libshacccg.suprx (from ur0:data on the Vita; vitaGL
# compiles its shaders with it at runtime), PSVUPDAT.PUP (firmware), PSP2UPDAT.PUP (fonts).
#
#   bash tools/vita3k/setup.sh            install or refresh
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
V="$REPO/vita3k"
ZIP="$REPO/external/downloads/vita3k-windows-latest.zip"

for f in libshacccg.suprx PSVUPDAT.PUP PSP2UPDAT.PUP; do
    [ -f "$V/$f" ] || { echo "vita3k: FATAL: $V/$f missing"; exit 1; }
done

if [ ! -f "$V/Vita3K.exe" ]; then
    if [ ! -f "$ZIP" ]; then
        echo "vita3k: downloading windows-latest.zip (continuous)"
        curl -sSL -o "$ZIP" https://github.com/Vita3K/Vita3K/releases/download/continuous/windows-latest.zip
    fi
    echo "vita3k: unpacking $(basename "$ZIP")"
    unzip -o -q "$ZIP" -d "$V"
fi

mkdir -p "$V/portable" "$REPO/.scratch"
# The very first launch in a fresh portable dir dies in logging init ("create_directories:
# Invalid argument") before it reads the PUP; the second launch works. So each PUP gets one
# retry, and success is judged by what landed in fs/, not by the exit code.
install_pup() {  # $1 = PUP, $2 = directory it must create under fs/
    local pup="$1" marker="$V/portable/fs/$2"
    for attempt in 1 2; do
        [ -d "$marker" ] && return 0
        echo "vita3k: installing firmware $pup (attempt $attempt)"
        "$V/Vita3K.exe" --firmware "$(cygpath -w "$V/$pup")" > "$REPO/.scratch/vita3k-firmware-$pup.log" 2>&1 || true
    done
    [ -d "$marker" ] || { echo "vita3k: FATAL: $pup did not install (.scratch/vita3k-firmware-$pup.log)"; exit 1; }
}
install_pup PSVUPDAT.PUP vs0
install_pup PSP2UPDAT.PUP sa0/data/font

# Unattended runs: the first-run wizard and the welcome dialog both block the CLI auto-boot.
CFG="$V/portable/config.yml"
if [ -f "$CFG" ]; then
    sed -i -e 's/^initial-setup: false/initial-setup: true/' -e 's/^show-welcome: true/show-welcome: false/' "$CFG"
    # Async pipeline compilation skips draws while a shader compiles, so the same frame would
    # differ by run. Surface sync stays off (Vita3K's default): even on, glReadPixels read the
    # surface before Vita3K wrote it back (3 of 4 shots black) and the game ran at 3-5 fps,
    # so shots are window grabs instead (run.sh, "scenario: holding shot N").
    sed -i -e 's/^disable-surface-sync: false/disable-surface-sync: true/' \
           -e 's/^async-pipeline-compilation: true/async-pipeline-compilation: false/' "$CFG"
    echo "vita3k: config -- $(grep -E '^(initial-setup|show-welcome|backend-renderer|modules-mode|disable-surface-sync|async-pipeline-compilation):' "$CFG" | tr '\n' ' ')"
fi
# This session runs elevated, and Vita3K's "running with administrator privileges" box is
# modal (gui-qt main_window.cpp, gui::mw_warnAdminPrivileges): turn it off for unattended runs.
GUI="$V/portable/gui-configs/CurrentSettings.ini"
if [ -f "$GUI" ] && ! grep -q '^warnAdminPrivileges=' "$GUI"; then
    printf '\n[MainWindow]\nwarnAdminPrivileges=false\n' >> "$GUI"
    echo "vita3k: admin-privileges warning off"
fi

mkdir -p "$V/portable/fs/ur0/data"
cp "$V/libshacccg.suprx" "$V/portable/fs/ur0/data/libshacccg.suprx"
echo "vita3k: ready -- fs: $(ls "$V/portable/fs" 2>/dev/null | tr '\n' ' ')"
