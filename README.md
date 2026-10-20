# Buckshot Roulette PORTABLE - BSHT (for PS Vita)


**Please Sign The Waiver**

Affectionately shortened to BSHT-Vita, with BSHT-PSP soon!
*(im aware bsht might be shorthand for bullshit but bear w me)*

This is a native port of the goat Mike Klubnika's *Buckshot Roulette* to the
PS Vita. 60 fps bayybbeeeeeee

FULL TOP TO BOTTOM singleplayer experience, including Double or Nothing and
achievements (local ones, no Steam on a Vita). Everything else like
multiplayer and shit is deferred for later.

## BUY the game, its like a dollar bro

There's no game content in this repo. The models, sounds, textures and text
all come from your copy of Buckshot Roulette, thanks to this little thingy
called bsht-extract.

The tool only accepts the latest Steam copy, aka v2.2.0 hotfix 6. It checks
your files against that build and stops if anything is different, like an
older version, a mod or a broken install. If it refuses your Steam copy, run
*Verify integrity of game files* in Steam and try again.

I haven't tried the itch.io build yet, and i have a nagging theory it wont
work because of the version differences between the two. I'm also aware this
isn't the most ideal method, so i'll work on a one size fits all extractor of
sorts. But this is the best I can do for now.

## DOWNLOADING

Grab these from the General Release of Liability [Releases](../../releases) page:

| File | What it is |
|---|---|
| `BuckshotRoulettePortable-BSHT-v1.0.0.vpk` | the game for the Vita |
| `bsht-extract-v1.0.0-windows.zip` | the tool that converts your copy (Windows 10 or newer) |
| `bsht-desktop-v1.0.0-windows.zip` | the same port for Windows, if you want it on PC too, for whatever reason |

I plan to release BSHT for macOS and Linux (natively) too, so that's coming
later.

GitHub Actions builds all three from this repo, and I scanned them with
Windows Defender before uploading. The tool zip is big as shit because it
ships a stock copy of Godot 4.1.4, which it runs in the background to read
the game. It unpacks your copy and exports everything through real Godot,
no decompiler involved.

## SETTTUPP

Jailbroken Vita yada yada. Make sure you have `ur0:data/libshacccg.suprx`,
since this uses vitaGL and vitaGL needs it to compile shaders. The rest:

1. Unzip `bsht-extract` and point it at your copy. Either drag
   `Buckshot Roulette.exe` onto `bsht-extract.exe`, or point it at the game's
   folder from a command prompt:

   ```
   bsht-extract "...\SteamLibrary\steamapps\common\Buckshot Roulette\Buckshot Roulette_windows"
   ```

   It takes around half an hour and writes a `bsht-data\game\` folder next to
   itself. It doesn't touch the game's folder and doesn't go online. More
   about it in [docs/tools/bsht-extract.md](docs/tools/bsht-extract.md).
2. Copy that `game` folder to `ux0:data/bsht/game/` on the Vita.
3. Install the vpk with VitaShell. EzPz

If the data is missing, half copied or from the wrong version, the game shows
a plain text screen telling you what's wrong.

Controls: d-pad to move, CROSS to pick, CIRCLE to back out. The touchscreen
works like the mouse does on PC. Hold START to quit the game, and SELECT
opens the debug menu (L/R flip its pages). Saves go in `ux0:data/bsht/`.

## DESKTOP VERSION

Unzip `bsht-desktop`, put the same `game` folder next to `bsht_desktop.exe`
and run it. Saves go in a `save\` folder there, and the console window that
opens with it is the log. To use a game folder somewhere else:

```
bsht_desktop.exe --data <path to a game folder>
```

This is also the build I develop with, so it has some extra stuff (frame
grabs, parity runs, a debug overlay on F1 or a controller's Back button), but
it plays the whole game. Hold Escape to quit.

## BUGS REPORTING

Open an issue here and tell me what happened, what you expected, and about
where you were in the game. On the Vita a report file helps a lot. If the
issue is reproducible, try this:

1. In VitaShell, create an empty file called `BSHT_REPORT` in
   `ux0:data/bsht/`.
2. Play until the bug shows up again.
3. Attach the newest `ux0:data/bsht_report_<N>.txt` (one per boot, highest
   number is the latest), or all of them, ill figure it out. If it crashed,
   attach the `ux0:data/psp2core-*.psp2dmp` file too.

Delete `BSHT_REPORT` when you're done and it stops writing reports. On
Windows, just copy the text out of the console window.

## COMING SOON (?)

- The burner phone's screen animation (it's a still image for now)
- Multiplayer, starting with Ad-Hoc on the Vita
- A PSP version. If anyone wants to help, need all hands on deck, preferably
  with coding experience. I don't mind AI usage, this port is partly AI
  assisted, but you should at least know what you're doing. I made this
  performance first, which a lot of pure vibecoders might not like.
- Some BSHT ill think up probably (deadass might make a PC mod for Steam that
  lets you play multiplayer with the console)
- Android, Mac and Linux versions (if you're a lovely helper and make one,
  tell me, i'll put it in main and credit you)
- Feel free to add your suggestions. Honestly everything is fair game.

## BUILDING YOURSELF

For the desktop build and the tools you need Windows with MSYS2 (ucrt64: g++,
CMake, Ninja, SDL2, Python):

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

The Vita build needs vitasdk plus five libraries built from source (vitaGL,
the vitagl branch of SDL2, vitaShaRK, SceShaccCgExt, math-neon).
`tools/vita/build_vita_libs.sh` builds them at the commits I test with, and
[docs/TOOLCHAIN.md](docs/TOOLCHAIN.md) has the details. Then:

```
python tools/bake_menu_logo.py
cmake -S . -B build-vita -G Ninja -DBSHT_TARGET=vita -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build-vita        # build-vita/src/bsht_vita.vpk
```

CI (`.github/workflows/ci.yml`) runs all of this on every push and packages
the three downloads when a tag is pushed.

Tests that need the game's data skip themselves and say which file they were
looking for. To run them, run bsht-extract with `--keep-work` and copy its
output into `assets/` with `bash tools/refresh_assets.sh <output folder>`.
The parity tests also need the original game unpacked
(`bsht-extract <exe> --unpack-to external/game-v2.2`).

## HOW??

- The game's singleplayer GDScript is translated into C++ (`src/scripts`)
  with the same class and function names, running on a small runtime that
  copies the parts of Godot the scripts use (`src/godot`). If something acts
  weird, you can read the `.cpp` side by side with the `.gd` from your own
  copy (`--unpack-to` above gets you those). Its pretty easy, Godot games
  open up nicely.
- A checker runs the original scenes in real Godot 4.1.4 and compares them
  with the port frame by frame (`tests/parity`).
- Lighting is baked ahead of time into textures, so the Vita only does one
  texture read per surface and the picture stays close to Godot's. Saves a
  lot of performance while still looking like the original.

## CREDITS

- Mike Klubnika made Buckshot Roulette and gave this port his blessing. LOVE
  YOU MIKE
- Me (i added portable to the title and shit idfk)
- It uses a bunch of other people's libraries, listed below.

## LICENSE

The port's code is GPL-3.0 ([LICENSE](LICENSE)). The game itself isn't, duh.
The only thing from the original is the LiveArea art, and all game content is
courtesy and blessing of Mike Klubnika.

The libraries keep their own licences. [THIRD_PARTY.md](THIRD_PARTY.md) lists
each one (SDL2, vitaGL, Godot, stb, xatlas, reproc and the rest), and their
full texts are in [licenses/](licenses) and `third_party/`. All three
downloads carry them too, in a `licenses` folder (`data/licenses` in the vpk).
