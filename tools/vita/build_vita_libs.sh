#!/usr/bin/env bash
# Builds the Vita libraries the port links, at the commits it is tested with,
# and installs them into $VITASDK/arm-vita-eabi. CI runs it; so can anyone with
# vitasdk (VITASDK set, $VITASDK/bin on PATH), git, curl, make, cmake and ninja.
#
#   bash tools/vita/build_vita_libs.sh [work dir]      (default: .scratch/vita-libs)
#
# Order matters: taiHEN's stubs, then SceShaccCgExt, math-neon, vitaShaRK,
# vitaGL, and SDL2's vitagl branch, which looks for vitaGL.h.
set -euo pipefail
: "${VITASDK:?set VITASDK to the vitasdk folder}"
WORK="${1:-.scratch/vita-libs}"
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"
SYS="$VITASDK/arm-vita-eabi"
TOOLCHAIN="$VITASDK/share/vita.toolchain.cmake"
JOBS="$(nproc 2>/dev/null || echo 4)"

TAIHEN_URL="https://github.com/yifanlu/taiHEN/releases/download/v0.11/taihen.tar.gz"
TAIHEN_SHA256="6a7d4592b983292e8b8c53c624fa800e33ff69fe06c9d267cafc33d0ded4d2a9"

# name repo commit: a clone of repo at commit in $WORK/name.
fetch() {
    local dir="$WORK/$1"
    [ -d "$dir/.git" ] || git clone -q --filter=blob:none "$2" "$dir"
    git -C "$dir" checkout -q --detach "$3"
    echo "vita-libs: $1 at $(git -C "$dir" rev-parse --short HEAD)"
}

curl -sSL "$TAIHEN_URL" -o "$WORK/taihen.tar.gz"
echo "$TAIHEN_SHA256  $WORK/taihen.tar.gz" | sha256sum -c --quiet
tar -xzf "$WORK/taihen.tar.gz" -C "$SYS"
echo "vita-libs: taiHEN v0.11 stubs installed"

# GCC 15 defaults to C23, where taihen.h's old variadic macro no longer
# compiles; gnu17 is the language it was written for.
fetch SceShaccCgExt https://github.com/bythos14/SceShaccCgExt.git fb0e9d3
cmake -S "$WORK/SceShaccCgExt" -B "$WORK/SceShaccCgExt/build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-std=gnu17" -DCMAKE_INSTALL_PREFIX="$SYS" > /dev/null
cmake --build "$WORK/SceShaccCgExt/build" > /dev/null
cmake --install "$WORK/SceShaccCgExt/build" > /dev/null

fetch math-neon https://github.com/Rinnegatamante/math-neon.git 0faab81
make -C "$WORK/math-neon" -j"$JOBS" > /dev/null
cp "$WORK/math-neon/libmathneon.a" "$SYS/lib/"
cp "$WORK/math-neon/source/math_neon.h" "$SYS/include/"

fetch vitaShaRK https://github.com/Rinnegatamante/vitaShaRK.git df24065
make -C "$WORK/vitaShaRK" -j"$JOBS" \
    CFLAGS="-g -Wl,-q -O2 -ffast-math -mtune=cortex-a9 -mfpu=neon -ftree-vectorize -std=gnu17" > /dev/null
cp "$WORK/vitaShaRK/libvitashark.a" "$SYS/lib/"
cp "$WORK/vitaShaRK/source/vitashark.h" "$SYS/include/"

# The flags docs/TOOLCHAIN.md explains. NO_SPLASHSCREEN: our loading screen is
# the first thing drawn.
fetch vitaGL https://github.com/Rinnegatamante/vitaGL.git 6440cf0
make -C "$WORK/vitaGL" -j"$JOBS" \
    HAVE_GLSL_SUPPORT=1 HAVE_SHADER_CACHE=1 HAVE_PROFILING=1 NO_DEBUG=1 MATH_SPEEDHACK=1 \
    PRIMITIVES_SPEEDHACK=1 INDICES_SPEEDHACK=1 SAMPLERS_SPEEDHACK=1 NO_SPLASHSCREEN=1 > /dev/null
cp "$WORK/vitaGL/libvitaGL.a" "$SYS/lib/"
cp "$WORK/vitaGL/source/vitaGL.h" "$SYS/include/"

fetch SDL-vita https://github.com/Northfear/SDL.git f99bb63
cmake -S "$WORK/SDL-vita" -B "$WORK/SDL-vita/build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release \
    -DVIDEO_VITA_VGL=ON -DVIDEO_VITA_PVR=OFF -DSDL_SHARED=OFF -DSDL_STATIC=ON \
    -DCMAKE_INSTALL_PREFIX="$SYS" > "$WORK/sdl-configure.log"
grep -q "Looking for vitaGL.h - found" "$WORK/sdl-configure.log" \
    || { echo "vita-libs: SDL2 did not find vitaGL.h (see $WORK/sdl-configure.log)"; exit 1; }
cmake --build "$WORK/SDL-vita/build" > /dev/null
cmake --install "$WORK/SDL-vita/build" > /dev/null

echo "vita-libs: done, installed into $SYS"
