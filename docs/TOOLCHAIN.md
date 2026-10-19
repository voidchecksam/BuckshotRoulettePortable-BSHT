# Toolchain

What builds the port, and the few things about it that are not obvious.
`.github/workflows/ci.yml` runs every step below; when in doubt, it is the
reference.

## Windows: the desktop game and the tools

MSYS2, ucrt64 environment:

```
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,SDL2,python,python-pillow}
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

The exes link libstdc++ statically, so a build runs with only `SDL2.dll`
beside it. Both Windows exes carry a manifest that makes UTF-8 their code
page (Windows 10 1903 or later), so they work from a folder with any letters
in its name.

## The Vita build

### vitasdk

The port is built with the vitasdk snapshot `sdk-snapshot-20260825.612.1`
(GCC 15.2.0) from [vitasdk/autobuilds](https://github.com/vitasdk/autobuilds/releases).
Unpack it, set `VITASDK` to its folder and put `$VITASDK/bin` on `PATH`. If
`VITASDK` is not set, CMake also looks in `external/vitasdk`.

### The libraries

vitasdk has Sony's stubs but not vitaGL or SDL2. `tools/vita/build_vita_libs.sh`
builds and installs these, at the commits the port is tested with:

| Library | Commit | How |
|---|---|---|
| taiHEN stubs | release v0.11 | `taihen.tar.gz` unpacked into `$VITASDK/arm-vita-eabi` |
| [SceShaccCgExt](https://github.com/bythos14/SceShaccCgExt) | `fb0e9d3` | CMake, `-std=gnu17` |
| [math-neon](https://github.com/Rinnegatamante/math-neon) | `0faab81` | make |
| [vitaShaRK](https://github.com/Rinnegatamante/vitaShaRK) | `df24065` | make, `-std=gnu17` |
| [vitaGL](https://github.com/Rinnegatamante/vitaGL) | `6440cf0` | make, the flags below |
| [SDL2, vitagl branch](https://github.com/Northfear/SDL) | `f99bb63` | CMake, `-DVIDEO_VITA_VGL=ON` |

vitaGL's flags:

```
HAVE_GLSL_SUPPORT=1 HAVE_SHADER_CACHE=1 HAVE_PROFILING=1 NO_DEBUG=1 MATH_SPEEDHACK=1
PRIMITIVES_SPEEDHACK=1 INDICES_SPEEDHACK=1 SAMPLERS_SPEEDHACK=1 NO_SPLASHSCREEN=1
```

`PRIMITIVES_SPEEDHACK` and `INDICES_SPEEDHACK` are documented as "may cause
glitches". They are safe here: the port draws only triangles, every mesh fits
16-bit indices, and nothing is instanced. `NO_SPLASHSCREEN` keeps vitaGL's own
logo off the screen before the port's loading screen.

### Building the vpk

```
python tools/bake_menu_logo.py          # assets/baked/menu_logo.rgba, from art/menu-title/port_menu_title.png
cmake -S . -B build-vita -G Ninja -DBSHT_TARGET=vita -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build-vita                 # build-vita/src/bsht_vita.vpk
```

The vpk holds the binary, the LiveArea art, the menu logo and the port's
achievement text. The game itself comes from bsht-extract (docs/tools/bsht-extract.md).

## Things that bite

- **GCC 15 defaults to C23**, where `()` means `(void)`. taiHEN's
  `TAI_CONTINUE` macro then fails with "too many arguments to function".
  Anything that includes `taihen.h` is built with `-std=gnu17`.
- **CMake 4 dropped `cmake_minimum_required` below 3.5.** SDL2 asks for less:
  `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`.
- **No `%zu` on the Vita.** newlib on `arm-vita-eabi` prints a literal `zu` and
  every argument after it comes out wrong. Cast `size_t` to `unsigned long`
  and use `%lu`.
- **No 32-bit indices on the Vita** (`GL_OES_element_index_uint` is missing).
  The asset loaders convert to 16-bit and fail loudly past 65535 vertices.
- **Desktop GL is held to the GLES2 subset**: `src/platform/gl_compat.h` loads
  only the entry points the Vita has, and a test rejects legacy GL calls.
- **vita-elf-create and the 64 KiB boundary.** When `.text` ends just below a
  64 KiB boundary, older vita-elf-create corrupts its heap (`0xC0000374`, no
  message) and newer ones refuse the ELF (vita-toolchain #150). The port links
  with `-Wl,--defsym=__sce_headroom=0x10000`, which vitasdk's linker script
  honours.

## On the device

vitaGL compiles shaders at run time with Sony's compiler, so the Vita needs
`libshacccg.suprx` extracted from its own firmware, at `ur0:data/libshacccg.suprx`
(vitaGL also tries `ur0:data/external/`). Guide:
https://samilops2.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx

Crash dumps (`ux0:data/psp2core-*.psp2dmp`) can be read with
[vita-parse-core](https://github.com/xyzz/vita-parse-core) against
`build-vita/src/bsht_vita`; a cut `.psp2dmp.tmp` first goes through
`tools/vita/pad_core.py`.
