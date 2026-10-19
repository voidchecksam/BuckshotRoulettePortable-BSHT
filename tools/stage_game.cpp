// Dev tool: a game folder from the repo's assets/, as bsht-extract would write
// it, in seconds instead of a full extract. For bsht_desktop --data and Vita3K.
//
//   stage_game <game folder>      (run from the repo root)
#include "extract/package.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    if (argc != 2) {
        std::printf("usage: stage_game <game folder>\n");
        return 2;
    }
    const fs::path game = fs::absolute(argv[1]);
    const fs::path work = game.parent_path() / (game.filename().string() + ".work");
    std::error_code ec;
    fs::create_directories(work, ec);
    std::string error;
    const bool ok = extract::Package(fs::absolute("assets"), game, "from assets", error, work);
    fs::remove(work, ec);   // empty after a good run
    if (!ok) {
        std::printf("stage_game: %s\n", error.c_str());
        return 1;
    }
    std::size_t files = 0;
    for (const auto& entry : fs::directory_iterator(game, ec)) files += entry.is_regular_file() ? 1 : 0;
    std::printf("stage_game: wrote %zu files to %s\n", files, game.string().c_str());
    return 0;
}
