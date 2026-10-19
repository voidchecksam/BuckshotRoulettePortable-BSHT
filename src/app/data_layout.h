// Where the port reads its data. On the desktop the repo's assets/ folders
// (the dev loop and the bakes); on the Vita, and on the desktop with --data,
// bsht-extract's flat game folder. Headless.
#pragma once

#include <string>

namespace app {

struct DataLayout {
    std::string data;    // exports: json, ui pixels, dialogue, font pages
    std::string rooms;   // baked rooms, sfx.bank, particle_smoke.rgba
    std::string port;    // the port's own files (menu_logo.rgba)
    std::string achievements;   // the port's achievement text, a port file
    std::string cursors;        // the desktop's mouse cursor images (optional)
    std::string sfxBank, menuMusic, roomMusic, musicDir;
    bool spaceFreeMusic = false;   // music file names without spaces (the game folder)
    bool gameFolder = false;       // check it with CheckGameData before loading
};

DataLayout DevAssetsLayout();
DataLayout GameFolderLayout(const std::string& gameDir, const std::string& portDir);

// One line for the log.
std::string DescribeLayout(const DataLayout& layout);

}  // namespace app
