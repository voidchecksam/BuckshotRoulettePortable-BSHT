# Third-party code

What this port is built with, where it lives, and its licence. The libraries
in `third_party/` carry their own licence files; the other texts are in
`licenses/`. Every download carries all of them in a `licenses/` folder (the
vpk in `data/licenses/`).

## In this repository (`third_party/`)

| Library | Version | Licence | Used for |
|---|---|---|---|
| [reproc](https://github.com/DaanDeMeyer/reproc) | 14.2.7 | MIT | bsht-extract runs Godot with it |
| [PicoSHA2](https://github.com/okdshin/PicoSHA2) | 1.0.1 | MIT | bsht-extract's fingerprints and manifest |
| [xatlas](https://github.com/jpcy/xatlas) | f700c77 | MIT | unwrapping surfaces for the light bake |
| [libogg](https://github.com/xiph/ogg) | 1.3.6 | BSD-3-Clause | reading the game's Ogg streams |
| [stb_image, stb_vorbis, stb_dxt](https://github.com/nothings/stb) | 2.30 / 1.22 / 1.12 | public domain or MIT | images, Vorbis decoding, texture compression |
| [font8x8](https://github.com/dhepper/font8x8) | | public domain | the "no game data" screen |

## Transcribed into the port's own code

| From | Licence | Where |
|---|---|---|
| [Godot Engine](https://godotengine.org) 4.1.4 | MIT (`licenses/godot-engine.txt`) | the engine behaviour the scripts rely on, transcribed in `src/godot`, and Godot's light and colour maths in `src/assets` |
| PCG32 by M.E. O'Neill, as Godot ships it | Apache-2.0 (`licenses/pcg.txt`, `licenses/apache-2.0.txt`) | `src/godot/rng.h` |

## Linked into the Vita build (built from source, see docs/TOOLCHAIN.md)

| Library | Licence |
|---|---|
| [vitaGL](https://github.com/Rinnegatamante/vitaGL) | LGPL-3.0 (`licenses/lgpl-3.0.txt`, with the GPL-3.0 in `LICENSE`) |
| [SDL2, vitagl branch](https://github.com/Northfear/SDL) | zlib (`licenses/sdl2.txt`) |
| [vitaShaRK](https://github.com/Rinnegatamante/vitaShaRK) | LGPL-3.0 |
| [SceShaccCgExt](https://github.com/bythos14/SceShaccCgExt) | GPL-3.0 (`LICENSE`) |
| [math-neon](https://github.com/Rinnegatamante/math-neon) | MIT (`licenses/math-neon.txt`) |
| [vitasdk](https://vitasdk.org) (newlib, the stubs) | its own, per component |

## In the Windows builds

| What | Licence |
|---|---|
| SDL2.dll (MSYS2's build of SDL 2) | zlib (`licenses/sdl2.txt`) |
| mingw-w64's winpthreads, linked in | MIT-style (`licenses/winpthreads.txt`) |
| GCC's libstdc++ and libgcc, linked in | GPL-3.0 with the GCC Runtime Library Exception |
| Godot 4.1.4, in the bsht-extract package only, unmodified | MIT; its own `LICENSE.txt` and `COPYRIGHT.txt` are in `godot/` |

## The menu title (`art/menu-title/port_menu_title.png`)

Buckshot Roulette's logo (Mike Klubnika's), with the port's lines set in
[Sorts Mill Goudy](https://github.com/google/fonts/tree/main/ofl/sortsmillgoudy) and
[Press Start 2P](https://github.com/google/fonts/tree/main/ofl/pressstart2p) (SIL Open Font
License 1.1). Only the finished image is here; the fonts are not.

## Used to build and test only

GoogleTest (BSD-3-Clause), fetched by CMake for the test suite.
