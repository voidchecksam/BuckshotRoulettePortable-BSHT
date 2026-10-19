// Where the desktop game reads its data and keeps its saves. The release zip
// has data/ (the port's own files) beside bsht_desktop.exe, and the player
// copies bsht-extract's game/ next to it; everything is found from the exe's
// folder, so a shortcut or a terminal elsewhere changes nothing. The dev tree
// has no data/ beside the exe and keeps reading assets/. Headless.
#pragma once

#include "app/data_layout.h"

#include <string>

namespace app {

struct DesktopPaths {
    DataLayout layout;
    std::string saveDir;
};

// exeDir: the exe's folder, without a trailing slash. dataArg: --data, empty
// when absent. releaseLayout: IsReleaseLayout(exeDir).
DesktopPaths ChooseDesktopPaths(const std::string& exeDir, const std::string& dataArg, bool releaseLayout);

// <exeDir>/data/menu_logo.rgba exists. exeDir is UTF-8.
bool IsReleaseLayout(const std::string& exeDir);

}  // namespace app
