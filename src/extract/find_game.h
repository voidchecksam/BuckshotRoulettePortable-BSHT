// Picks the game file from the path the player gave: that file, or the files
// in that folder and its direct subfolders. Nothing else on the disk is read.
#pragma once

#include "extract/game_version.h"
#include "extract/pack_reader.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace extract {

struct FoundGame {
    std::string path;     // UTF-8
    PackInfo pack;
    GameVersion version;
};

struct FindResult {
    std::optional<FoundGame> game;
    std::vector<std::string> notes;   // why each candidate was passed over
};

FindResult FindGame(const std::string& utf8PathFromPlayer);

// The folders the tool must never write into: the one holding the game file,
// and the folder the player gave, when they gave a folder.
std::vector<std::filesystem::path> FoldersToLeaveAlone(const std::string& utf8PathFromPlayer, const FoundGame& game);

}  // namespace extract
