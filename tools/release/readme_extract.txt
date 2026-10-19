bsht-extract - turns your copy of Buckshot Roulette into the data the port reads
==============================================================================

You need Buckshot Roulette v2.2.0 hotfix 6 (the Steam build) and Windows 10 or
later.

1. Run it on the game, from a command prompt:

     bsht-extract.exe "D:\SteamLibrary\steamapps\common\Buckshot Roulette\Buckshot Roulette_windows\Buckshot Roulette.exe"

   or drop "Buckshot Roulette.exe" onto bsht-extract.exe.

2. Wait. It runs its own Godot headless, exports the game and bakes the
   lighting: about half an hour. The window waits for Enter at the end.

3. It writes bsht-data\game\ next to this program. Copy that game folder to
   ux0:data/bsht/game/ on the Vita, or next to bsht_desktop.exe on Windows.

It reads only the file you give it, never writes inside the game's folder,
never goes online and never looks at your Steam library. Everything it writes
is under bsht-data\.

godot\godot.exe is Godot 4.1.4, unmodified (MIT, see godot\LICENSE.txt).
exporters\ are the scripts it runs inside Godot.

Source, build steps and the licence: https://github.com/voidchecksam/BuckshotRoulettePortable-BSHT
