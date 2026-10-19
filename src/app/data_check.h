// Checks bsht-extract's game/ folder before the port reads anything from it:
// the manifest, its format, and that every file is there. Headless.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace app {

struct GameDataCheck {
    bool ok = false;
    std::vector<std::string> lines;   // what is wrong, at most 60 characters a line
    int format = -1;                  // the manifest's, -1 when unread
    std::size_t files = 0;            // files the manifest lists
};

// The files the port opens from the game folder, by name.
std::vector<std::string> RequiredGameFiles();

GameDataCheck CheckGameData(const std::string& gameDir);

// One line for the log: "ok, format 1, 152 files" or the message joined.
std::string DescribeCheck(const GameDataCheck& check);

}  // namespace app
