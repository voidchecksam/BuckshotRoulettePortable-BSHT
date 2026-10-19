// app/ui_screens: the table scene draws every subtree of "Camera/dialogue UI"
// (except the dialogue box, a screen of its own). A hand-written list can miss
// one: "pills choice" (Double or Nothing's YES / NO, without which CROSS takes
// the focused NO and the mode cannot start) or "viewblocker parent" (the death
// blackout).
#include "app/ui_screens.h"

#include "assets/ui_layout.h"

#include "test_data.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace {

bool Has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

TEST(UiScreens, TheTableDrawsEverySubtreeButTheDialogueBox) {
    BSHT_NEEDS_FILE(std::string(BSHT_SOURCE_DIR) + "/assets/extracted/ui_layout.json");
    assets::UiLayout layout;
    ASSERT_EQ(layout.Load((std::string(BSHT_SOURCE_DIR) + "/assets/extracted/ui_layout.json").c_str(), nullptr),
              assets::UiLayoutError::None);

    const std::vector<std::string> screens = app::TableUiScreens(layout, "Camera/dialogue UI/main dialogue");

    EXPECT_TRUE(Has(screens, "Camera/dialogue UI/pills choice"));
    EXPECT_TRUE(Has(screens, "Camera/dialogue UI/viewblocker parent"));
    EXPECT_TRUE(Has(screens, "Camera/dialogue UI/bracket ui"));
    EXPECT_FALSE(Has(screens, "Camera/dialogue UI/main dialogue"));
    for (const assets::UiControl& c : layout.Controls()) {
        const std::string& p = c.path;
        if (p.rfind("Camera/dialogue UI/", 0) != 0 || p.rfind("Camera/dialogue UI/main dialogue", 0) == 0) continue;
        const bool covered = std::any_of(screens.begin(), screens.end(),
                                         [&p](const std::string& s) { return p.rfind(s, 0) == 0; });
        EXPECT_TRUE(covered) << p << " is drawn by no screen";
    }
}

TEST(UiScreens, EachSubtreeIsListedOnce) {
    BSHT_NEEDS_FILE(std::string(BSHT_SOURCE_DIR) + "/assets/extracted/ui_layout.json");
    assets::UiLayout layout;
    ASSERT_EQ(layout.Load((std::string(BSHT_SOURCE_DIR) + "/assets/extracted/ui_layout.json").c_str(), nullptr),
              assets::UiLayoutError::None);

    std::vector<std::string> screens = app::TableUiScreens(layout, "Camera/dialogue UI/main dialogue");
    const std::size_t n = screens.size();
    std::sort(screens.begin(), screens.end());
    screens.erase(std::unique(screens.begin(), screens.end()), screens.end());

    EXPECT_EQ(screens.size(), n);
}

}  // namespace
