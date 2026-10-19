#!/usr/bin/env bash
# Install + run a vpk in Vita3K, then collect the app's own reports and shots. A run only
# counts when the report's line 1 carries the expected git sha and its stages say how far it
# got; Vita3K's log alone cannot tell "booted" from "stuck at the bubble".
#
#   bash tools/vita3k/run.sh [--switch NAME]... [--no-report] [--grab-every N] [--game DIR | --no-game] <vpk> [seconds] [scenario.json]
#     --switch NAME: an empty switch file in ux0:data/bsht for this run only (app/debug_switch.h)
#     --no-report: leave out the BSHT_REPORT switch every other run gets, as on a player's
#       Vita: the app then writes no report (a scenario needs the report, so not with one)
#     --grab-every N: also grab the window every N seconds (a player's boot has no shots to hold)
#     --game DIR: the game folder (bsht-extract's game/, or build/src/stage_game) copied to
#       ux0:data/bsht/game when its manifest differs; --no-game removes it. Neither: kept as is.
#   Options, not environment variables: run from a shell with MSYS2's usr/bin first on PATH
#   (the Vita build's), `bash` is MSYS2's and gets no variables from Git Bash.
#
# Install: the vpk is a zip of ux0:app/<TITLE_ID>; unpacking it there is deterministic (the Qt
# build's CLI install never auto-boots). Boot: `Vita3K -r <TITLE_ID>`.
# Scenario: copied to ux0:data/bsht/ (app/scenario_boot.h) with the BSHT_SHOT_HANDSHAKE switch.
# The app holds each shot's frame and logs "scenario: holding shot N"; this script grabs the
# window, then writes shot_N.done, and only then does the app move on ("released shot N").
# A hold that ends without it says "shot grab timed out N" and its PNG is renamed *_SUSPECT.
# (glReadPixels cannot see Vita3K's surfaces, so the app's own port_*.ppm are deleted here; on
# a real Vita those are the frames.) Every switch file is removed on exit, even on an abort.
# Output: .scratch/vita3k/<stamp>/ (bsht_report_*.txt, vita3k.log, shots as PNG).
set -euo pipefail
SWITCH_LIST=""
GRAB_EVERY=0
GAME=""
REPORT=1
while [ $# -gt 0 ]; do
    case "$1" in
        --switch) SWITCH_LIST="$SWITCH_LIST ${2:?--switch NAME}"; shift 2 ;;
        --no-report) REPORT=0; shift ;;
        --grab-every) GRAB_EVERY="${2:?--grab-every N}"; shift 2 ;;
        --game) GAME="${2:?--game DIR}"; shift 2 ;;
        --no-game) GAME=none; shift ;;
        --) shift; break ;;
        -*) echo "vita3k: unknown option $1"; exit 2 ;;
        *) break ;;
    esac
done
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
V="$REPO/vita3k"
VPK="${1:?usage: run.sh <vpk> [seconds] [scenario.json]}"
SECS="${2:-120}"
SCENARIO="${3:-}"
TITLE=BSHT00016
[ -f "$V/Vita3K.exe" ] || { echo "vita3k: run tools/vita3k/setup.sh first"; exit 1; }

FS="$V/portable/fs"
BSHT="$FS/ux0/data/bsht"
OUT="$REPO/.scratch/vita3k/$(date +%Y%m%d-%H%M%S)"
GRAB="$(cygpath -w "$REPO/tools/vita3k/grab.ps1")"
mkdir -p "$OUT" "$BSHT"

# Nothing of one run may leak into the next: a stale BSHT_FORCE_* or BSHT_SCENE_SOAK would
# silently change a later "player's boot".
clean_device_state() {
    rm -f "$BSHT"/BSHT_* "$BSHT"/shot_*.done "$BSHT/scenario.json" "$BSHT"/port_*.ppm
}
trap 'clean_device_state; taskkill //IM Vita3K.exe //F > /dev/null 2>&1 || true' EXIT
clean_device_state

rm -rf "$FS/ux0/app/$TITLE"
unzip -q -o "$VPK" -d "$FS/ux0/app/$TITLE"
echo "vita3k: installed $(basename "$VPK") into ux0:app/$TITLE"
rm -f "$FS/ux0/data"/bsht_report_*.txt
if [ "$GAME" = none ]; then
    rm -rf "$BSHT/game"
    echo "vita3k: no game folder"
elif [ -n "$GAME" ]; then
    [ -f "$GAME/manifest.json" ] || { echo "vita3k: $GAME has no manifest.json"; exit 2; }
    if cmp -s "$GAME/manifest.json" "$BSHT/game/manifest.json"; then
        echo "vita3k: game folder unchanged"
    else
        rm -rf "$BSHT/game"
        cp -r "$GAME" "$BSHT/game"
        echo "vita3k: copied the game folder $GAME ($(ls "$BSHT/game" | wc -l) files)"
    fi
fi
NAME=""
if [ -n "$SCENARIO" ] && [ "$REPORT" = 0 ]; then
    echo "vita3k: --no-report cannot run a scenario (its shots are paced through the report)"
    exit 2
fi
[ "$REPORT" = 1 ] && SWITCH_LIST="$SWITCH_LIST BSHT_REPORT"
if [ -n "$SCENARIO" ]; then
    cp "$SCENARIO" "$BSHT/scenario.json"
    NAME=$(python -c "import json,sys; print(json.load(open(sys.argv[1]))['name'])" "$(cygpath -w "$SCENARIO")")
    SWITCH_LIST="$SWITCH_LIST BSHT_SHOT_HANDSHAKE"
    echo "vita3k: scenario $NAME"
fi
for sw in $SWITCH_LIST; do : > "$BSHT/$sw"; echo "vita3k: switch $sw"; done

echo "vita3k: running for ${SECS}s"
timeout "$SECS" "$V/Vita3K.exe" -r "$TITLE" > "$OUT/vita3k_stdout.txt" 2>&1 &
EMU=$!
SEEN=0
START=$(date +%s); NEXT_GRAB="$GRAB_EVERY"
while kill -0 "$EMU" 2>/dev/null; do
    if [ "$GRAB_EVERY" -gt 0 ] && [ $(( $(date +%s) - START )) -ge "$NEXT_GRAB" ]; then
        SHOT="$OUT/t$(printf %03d "$NEXT_GRAB").png"
        powershell -NoProfile -File "$GRAB" "$(cygpath -w "$SHOT")" > /dev/null 2>&1 || true
        NEXT_GRAB=$((NEXT_GRAB + GRAB_EVERY))
    fi
    R=$(ls "$FS/ux0/data"/bsht_report_*.txt 2>/dev/null | tail -1 || true)
    if [ -n "$NAME" ] && [ -n "$R" ]; then
        mapfile -t HOLDS < <(grep -o 'scenario: holding shot [0-9]*' "$R" | awk '{print $4}' || true)
        while [ "$SEEN" -lt "${#HOLDS[@]}" ]; do
            AT="${HOLDS[$SEEN]}"; SEEN=$((SEEN + 1))
            powershell -NoProfile -File "$GRAB" "$(cygpath -w "$OUT/vita3k_${NAME}_${AT}.png")" | tail -1 \
                || echo "vita3k: grab of shot $AT FAILED"
            : > "$BSHT/shot_${AT}.done"   # the app releases the frame only now
        done
        # "ran for" is the app's exit summary: the scenario is over, close the emulator.
        if grep -q '^ran for ' "$R"; then kill "$EMU" 2>/dev/null || true; fi
    fi
    sleep 0.2
done
RC=0; wait "$EMU" 2>/dev/null || RC=$?
echo "vita3k: emulator closed (exit $RC; 124 = the ${SECS}s timeout, 143 = closed after the scenario)"

if [ "$REPORT" = 0 ]; then
    if ls "$FS/ux0/data"/bsht_report_*.txt > /dev/null 2>&1; then
        echo "vita3k: WARNING a report was written without the BSHT_REPORT switch"
    else
        echo "vita3k: no report, as asked (no BSHT_REPORT switch)"
    fi
fi
if ! cp "$FS/ux0/data"/bsht_report_*.txt "$OUT/" 2>/dev/null && [ "$REPORT" = 1 ]; then
    echo "vita3k: NO app report -- the app never reached its report"
fi
cp "$V/portable/vita3k.log" "$OUT/" 2>/dev/null || true
for r in "$OUT"/bsht_report_*.txt; do
    [ -f "$r" ] || continue
    echo "  $(basename "$r"): $(head -1 "$r" | sed 's/.*git /git /') | last $(grep '^stage:' "$r" | tail -1)"
    # Every switch logs itself when read; one that is not in the report was not seen (Vita3K
    # has reported a freshly written switch file missing at boot).
    for sw in $SWITCH_LIST; do
        grep -q "$sw" "$r" || echo "vita3k: WARNING switch $sw was NOT seen by the app -- this run is not what was asked; run it again"
    done
    for at in $(grep -o 'shot grab timed out [0-9]*' "$r" | awk '{print $5}'); do
        echo "vita3k: WARNING shot $at timed out before its grab was released -- marked SUSPECT"
        [ -f "$OUT/vita3k_${NAME}_${at}.png" ] && mv "$OUT/vita3k_${NAME}_${at}.png" "$OUT/vita3k_${NAME}_${at}_SUSPECT.png"
    done
done
echo "vita3k: $(ls "$OUT"/*.png 2>/dev/null | wc -l) shots; collected in ${OUT#$REPO/}"
