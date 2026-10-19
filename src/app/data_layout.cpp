#include "app/data_layout.h"

namespace app {

DataLayout DevAssetsLayout() {
    DataLayout l;
    l.data = "assets/extracted";
    l.rooms = "assets/baked";
    l.port = "assets/baked";
    l.sfxBank = "assets/baked/sfx.bank";
    l.menuMusic = "assets/extracted/audio/music/music main_gate modded.ogg";
    l.roomMusic = "assets/extracted/audio/music_room vol2.ogg";
    l.musicDir = "assets/extracted/audio/music";   // bsht-extract rebuilds the game's .ogg files here
    l.achievements = "tools/achievements_en.json";
    l.cursors = "assets/extracted/cursors";
    return l;
}

// bsht-extract's game/ (extract/package.cpp): everything flat, the two main
// tracks renamed, every music name without spaces.
DataLayout GameFolderLayout(const std::string& gameDir, const std::string& portDir) {
    std::string dir = gameDir;
    while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    DataLayout l;
    l.data = dir;
    l.rooms = dir;
    l.port = portDir;
    l.sfxBank = dir + "/sfx.bank";
    // The flat name MenuManager's stream maps to, so the router adopts the
    // track the loading screen started instead of restarting it.
    l.menuMusic = dir + "/music_main_gate_modded.ogg";
    l.roomMusic = dir + "/music_room.ogg";
    l.musicDir = dir;
    l.achievements = portDir + "/achievements_en.json";
    l.cursors = dir + "/cursors";
    l.spaceFreeMusic = true;
    l.gameFolder = true;
    return l;
}

std::string DescribeLayout(const DataLayout& layout) {
    if (layout.gameFolder) return "data: game folder " + layout.data + ", port files " + layout.port;
    return "data: dev assets " + layout.data + " + " + layout.rooms;
}

}  // namespace app
