# bsht-extract

`bsht-extract` turns a player's own copy of Buckshot Roulette into the data
folder the port reads. Players copy the result to `ux0:data/bsht/game/` on the
Vita. The player's steps are in the README; this page is for developers.

## What it does

```
bsht-extract "<Buckshot Roulette.exe, or its folder>" [--out DIR] [--keep-work]
```

It reads the file it is given, or the files in that folder and its direct
subfolders. It does not search the disk, look at Steam's library files, read
the registry or use the network. It writes only inside `--out` (default:
`bsht-data` next to the program), and never inside the game's folder or the
folder it was pointed at.

The Windows build imports the socket library (WS2_32.dll) all the same:
reproc, which runs Godot and the bakes as child processes, makes its pipes
from socket pairs bound to 127.0.0.1. Nothing connects anywhere else.

The stages, each logged with its time to the console and `<out>/extract.log`:

1. `[check]` finds the game, reads the version from `GlobalVariables.gd`
   inside the pack, and refuses anything but a supported version. The pack's
   fingerprint must match a known build, because the export runs the game's
   own scripts. Then it checks Godot's version, the exporter files, free space
   (3 GB), the output folder's path length on Windows (up to about 90
   characters: the unpacked game is written with the `\\?\` long-path prefix,
   but the bake reads exported files without it), and that any `game/` or `.work/` in `--out`
   was made by the tool. It holds `<out>/.bsht-extract.lock` for the whole run.
2. `[unpack]` copies every file out of the pack into `<out>/.work/game/` with
   the tool's own reader, writes an `override.cfg` (private user folder, Steam
   replaced by `steam_off.gd` and `steam_stub.gd`), turns off GDExtensions and
   copies the exporters in. v2.2's `scripts/PipelineManager.gd` is replaced by
   `pipeline_manager.gd`: headless, Godot has no rendering device, and the
   game's script then takes its OpenGL path (room hidden, lights halved).
3. `[export]` runs Godot 4.1.4 headless once per exporter, at a fixed 60 fps,
   with its user data folder inside `.work`. Each run must leave its files
   behind; if one does not, the stage stops and prints the end of its log.
   Models come from the meshes the scenes carry (`export_scene_meshes.gd`),
   not from `.godot/imported`: v2.2 keeps its imported models in Godot 4.3's
   format, which 4.1 cannot load. A shipped game lists `x.ogg.import`, never
   `x.ogg`, so the audio exporters read those listings. `export_ui.gd` records
   each Label's `clip_text` and `text_overrun_behavior` (the ending's stat
   lines trim to their width).
4. `[convert]` rebuilds `.ogg` files from Godot's packet data
   (`export_audio_packets.gd` writes `.oggpk`, `ogg_rebuild.cpp` turns them
   into pages with libogg).
5. `[bake]` runs `bake-room` (main, menu, heaven) and `bake-audio` as child
   processes of the tool itself. The shells' live/blank surfaces are baked at
   the colours v2.2 paints at ready (GlobalVariables' default and colour-blind
   pairs), not at their `.tres` colours.
6. `[package]` copies the shipped files into `<out>/game/` under names without
   spaces, with `manifest.json` (tool, game version, a SHA-256 per file). It is
   built in `.work/game.new`; the old `game/` moves to `.work/game.old`, the
   new one moves into place, and the old one is deleted.

`.work` is deleted after a good run unless `--keep-work` is given. After a
failed or stopped run it stays, and the next run clears it. Ctrl+C stops the
running child process; every child also has a 45 minute timeout. The children
share a job with the tool on Windows, so they end with it however it ends, and
the lock is an open file the system releases, so a killed run never blocks the
next one.

## Options for development

| Option | Use |
|---|---|
| `--project DIR` | Use an unpacked project instead of a game file (the reference game: `--project external/game-v2.2/game`). Skips the version and fingerprint checks and the unpack; the project's own `tools/godot/` are the exporters. For the run it writes an `override.cfg` marked `; written by bsht-extract` (private user folder, Steam off, the Steam stub when GodotSteam is not loaded) and afterwards removes it, or puts back the marked one that was there (the reference game's). An unmarked one is a developer's: the run stops. |
| `--godot FILE` | A Godot 4.1.4 binary. Default: `godot/godot.exe` (`godot/godot` elsewhere) next to the program, else `external/godot/` in this repo. On Windows that file must be `Godot_v4.1.4-stable_win64.exe` renamed, not the `_console.exe`, which only starts the exe named beside it. |
| `--exporters DIR` | The exporter scripts. Default: `exporters/` next to the program, else `tools/godot/`. |
| `--keep-work` | Keep `.work` (the unpacked game, every exporter's output and log). |
| `--unpack-to DIR` | Check and unpack the game into `DIR/game` as a private Godot project (Steam off, the exporters in `tools/godot/`), then stop. The port's reference game is made this way: `--unpack-to external/game-v2.2`. Not with `--project` or `--out`. |
| `--fingerprint FILE` | Print the build fingerprint of a copy and exit. |
| `--list-exporters` | Print the exporter scripts the tool runs, one a line, and exit. The release package's `exporters/` folder is exactly this list. |

Logs: `<out>/extract.log` for the run, `<out>/.work/logs/` for each exporter
and bake (kept with `--keep-work`, or after a failure).

Build the tool optimized for real use. In `build/` (no build type) the
fingerprint alone takes about 50 s instead of 4.

## Adding a game version

1. Read the new copy's version: run the tool on it; the check stage names the
   version it found.
2. Add it to `kSupported` in `src/extract/game_version.cpp`.
3. Print its fingerprint with `bsht-extract --fingerprint <exe>` (twice: the
   value must not change) and add it to `kKnownBuilds` in
   `src/extract/fingerprint.cpp`, with a name a player will recognise.
4. Run the tool on that copy end to end and check that the game folder is
   unchanged afterwards (list it before and after).

The port's code must then read the new version's data, which is a piece of
work of its own.

## Tests

`ctest -R "Arguments|PackReader|GameVersion|FindGame|Fingerprint|Unpack|GodotRun|OggRebuild|Package|Pipeline|Preflight|RemoveTree"`.
`tests/helpers/fake_godot.cpp` stands in for Godot where a test only needs a
child process. `tools/compare_data.py` compares two data folders by value
(a new extraction against a known good one).
