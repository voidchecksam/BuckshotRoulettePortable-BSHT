// GlobalVariables.gd (v2.2): the autoload's version string and its debug
// language swap.
#include "godot/translation.h"
#include "scripts/GlobalVariables.h"

#include <string>

#include <gtest/gtest.h>

namespace {

using scripts::GlobalVariables;

TEST(GlobalVariables, ReadyNamesTheVersionTheSteamBuildShows) {  // gd: _ready L95-L98
    GlobalVariables g;
    g._ready();
    EXPECT_EQ(GlobalVariables::currentVersion, "v2.2.0 (STEAM)");
    EXPECT_EQ(GlobalVariables::version_to_check, "v2.2.0.6");
}

TEST(GlobalVariables, TheShellColoursAreV22s) {
    EXPECT_FALSE(GlobalVariables::colorblind);
    EXPECT_FLOAT_EQ(GlobalVariables::default_color_live.g, 0.28f);
    EXPECT_FLOAT_EQ(GlobalVariables::default_color_blank.b, 1.0f);
    EXPECT_FLOAT_EQ(GlobalVariables::colorblind_color_blank.r, 0.34f);
}

TEST(GlobalVariables, SwapLanguageWrapsBothWays) {  // gd: SwapLanguage
    godot::set_translation_root(std::string(BSHT_SOURCE_DIR) + "/assets/extracted");
    GlobalVariables g;
    g.SwapLanguage(false);
    EXPECT_EQ(godot::get_locale(), "ZHT");
    g.SwapLanguage(true);
    EXPECT_EQ(godot::get_locale(), "EN");
    g.SwapLanguage(true);
    EXPECT_EQ(godot::get_locale(), "EE");
    godot::set_locale("EN");
}

}  // namespace
