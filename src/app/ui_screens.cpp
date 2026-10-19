#include "app/ui_screens.h"

#include "assets/ui_layout.h"

#include <algorithm>

namespace app {

std::vector<std::string> TableUiScreens(const assets::UiLayout& layout, const std::string& excluded) {
    static const std::string kRoot = "Camera/dialogue UI/";
    std::vector<std::string> screens;
    for (const assets::UiControl& c : layout.Controls()) {
        if (c.path.size() <= kRoot.size() || c.path.compare(0, kRoot.size(), kRoot) != 0) continue;
        const std::size_t slash = c.path.find('/', kRoot.size());
        const std::string subtree = c.path.substr(0, slash);
        if (subtree.compare(0, excluded.size(), excluded) == 0) continue;
        if (std::find(screens.begin(), screens.end(), subtree) == screens.end()) screens.push_back(subtree);
    }
    return screens;
}

}  // namespace app
