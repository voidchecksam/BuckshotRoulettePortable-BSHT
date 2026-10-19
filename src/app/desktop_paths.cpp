#include "app/desktop_paths.h"

#include <filesystem>
#include <system_error>

namespace app {

bool IsReleaseLayout(const std::string& exeDir) {
    const std::string logo = exeDir + "/data/menu_logo.rgba";
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(std::u8string(logo.begin(), logo.end())), ec);
}

DesktopPaths ChooseDesktopPaths(const std::string& exeDir, const std::string& dataArg, bool releaseLayout) {
    DesktopPaths p;
    if (releaseLayout) {
        const std::string game = dataArg.empty() ? exeDir + "/game" : dataArg;
        p.layout = GameFolderLayout(game, exeDir + "/data");
        p.saveDir = exeDir + "/save";
        return p;
    }
    p.saveDir = "save";
    if (dataArg.empty()) {
        p.layout = DevAssetsLayout();
        return p;
    }
    p.layout = GameFolderLayout(dataArg, "assets/baked");
    p.layout.achievements = DevAssetsLayout().achievements;   // a port file, tracked in tools/
    return p;
}

}  // namespace app
