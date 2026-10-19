// OptionsManager, KeyRebinding and the Vita's hidden rebind button, on the real
// menu tree. Expected strings and values were printed by Godot 4.1.4: the
// volume label steps 100%, 95%, ... and linear_to_db(0.9) = -0.9151498112135.
#include "app/scene_host.h"
#include "assets/animation.h"
#include "game/save_store.h"
#include "godot/audio_server.h"
#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/input_map.h"
#include "godot/math_funcs.h"
#include "godot/runtime_scene_loader.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"
#include "scripts/ButtonClass.h"
#include "scripts/ChecmarkBranch.h"
#include "scripts/GlobalVariables.h"
#include "scripts/KeyRebinding.h"
#include "scripts/MenuManager.h"
#include "scripts/OptionsManager.h"
#include "scripts/bind_support.h"

#include <cstdio>
#include <string>

#include "print_capture.h"

#include <gtest/gtest.h>

namespace {

const std::string kData = std::string(BSHT_SOURCE_DIR) + "/assets/extracted";

TEST(OptionsVolume, TheLabelStepsAsGodotPrintsIt) {
    const char* expected[] = {"100%", "95%", "90%", "85%", "80%", "75%", "70%", "65%", "60%", "55%", "50%",
                              "45%",  "40%", "35%", "30%", "25%", "20%", "15%", "10%", "5%",  "0%"};
    double v = 1;
    EXPECT_EQ(scripts::VolumeLabel(v), expected[0]);
    for (int i = 1; i <= 20; ++i) {
        if (v != 0) v = v - 0.05;
        if (v < 0) v = 0;
        EXPECT_EQ(scripts::VolumeLabel(v), expected[i]) << "step " << i;
    }
}

struct Options : ::testing::Test {
    godot::SceneTree tree;
    assets::AnimationLibrary anims;
    std::string dir;
    game::SaveStore store{""};
    scripts::OptionsManager* options = nullptr;
    scripts::MenuManager* menu = nullptr;

    void SetUp() override {
        dir = std::string(BSHT_BINARY_DIR) + "/options_test_" +
              ::testing::UnitTest::GetInstance()->current_test_info()->name();
        std::remove((dir + "/options_12.sav").c_str());
        store = game::SaveStore(dir);
        Build(tree);
    }
    void TearDown() override {
        godot::set_locale("EN");
        godot::AudioServer::get_singleton().reset();
        ReloadInputMap();
    }
    static void ReloadInputMap() {
        godot::InputMap::get_singleton().load((kData + "/input_map.json").c_str());
    }
    void Build(godot::SceneTree& t) {
        if (godot::LoadRuntimeScene((kData + "/runtime_menu.json").c_str(), t) != godot::RuntimeSceneError::None) {
            GTEST_SKIP() << "export runtime_menu.json first";
        }
        ReloadInputMap();
        godot::set_translation_root(kData);
        godot::set_locale("EN");
        ASSERT_TRUE(anims.LoadFromFile((kData + "/animations_menu.json").c_str()));
        godot::BindAnimationLibrary(t, anims);
        scripts::RegisterScripts();
        scripts::BindScripts(t.get_root());
        options = godot::script_cast<scripts::OptionsManager>(t.get_node("standalone managers/options manager"));
        menu = godot::script_cast<scripts::MenuManager>(t.get_node("standalone managers/menu manager"));
        ASSERT_NE(options, nullptr);
        options->store = &store;
        t.ready_scripts();
    }
};

TEST_F(Options, VolumeDownTwiceIsNinetyPercentOnTheLabelAndTheBus) {  // gd: L70-L76, L99-L105
    options->Adjust("decrease");
    options->Adjust("decrease");
    EXPECT_NEAR(options->setting_volume, 0.9, 1e-12);
    EXPECT_EQ(options->ui_volume->get_text(), "90%");
    EXPECT_NEAR(godot::AudioServer::get_singleton().get_bus_volume_db(0), -0.9151498f, 1e-5f);
}

TEST_F(Options, SettingsSurviveARestart) {  // gd: L180-L212
    options->Adjust("decrease");
    options->AdjustLanguage("FR");
    options->SaveSettings();
    godot::set_locale("EN");
    godot::SceneTree again;
    Build(again);
    EXPECT_NEAR(options->setting_volume, 0.95, 1e-12);
    EXPECT_EQ(options->setting_language, "FR");
    EXPECT_EQ(godot::get_locale(), "FR");
    EXPECT_TRUE(options->receivedFile);
}

TEST_F(Options, WindowModeChangesOnlyTheTwoAlphas) {  // gd: L107-L120
    options->Adjust("windowed");
    EXPECT_FLOAT_EQ(options->ui_windowed->get_modulate().a, 1.0f);
    EXPECT_FLOAT_EQ(options->ui_fullscreen->get_modulate().a, 0.5f);
    options->Adjust("fullscreen");
    EXPECT_FLOAT_EQ(options->ui_windowed->get_modulate().a, 0.5f);
    EXPECT_FLOAT_EQ(options->ui_fullscreen->get_modulate().a, 1.0f);
}

TEST_F(Options, ALanguageButtonSwitchesTr) {  // ButtonClass gd: L58, OptionsManager gd: L93-L97
    const std::string en = godot::tr("START");
    options->AdjustLanguage("FR");
    EXPECT_NE(godot::tr("START"), en);
}

// The original's TURKISH button sends "TU" while its table is "TR" (Godot
// then shows raw keys; JankPreventer.gd:15 checks "TR"). The port applies TR,
// so all 17 languages show, and keeps the original's saved value.
TEST_F(Options, TheTurkishButtonShowsTurkish) {
    options->AdjustLanguage("TU");
    EXPECT_EQ(godot::get_locale(), "TR");
    EXPECT_EQ(godot::tr("START"), "BA\xC5\x9E" "LA");   // BAŞLA
    EXPECT_EQ(options->setting_language, "TU");
}

TEST_F(Options, ResetControlsRestoresTheDefaultMap) {  // gd: L156-L160
    godot::InputMap& map = godot::InputMap::get_singleton();
    const std::string before = map.action_get_events("reset")[0]->as_text();
    auto key = std::make_shared<godot::InputEventKey>();
    key->keycode = 84;  // T
    map.action_erase_events("reset");
    map.action_add_event("reset", key);
    options->ResetControls();
    ASSERT_GE(map.action_get_events("reset").size(), 2u);
    EXPECT_EQ(map.action_get_events("reset")[0]->as_text(), before);
}

TEST_F(Options, TheRebindListShowsGodotsKeyNames) {  // KeyRebinding gd: L11-L33
    auto* rebinding = godot::script_cast<scripts::Rebinding>(tree.get_node("standalone managers/key rebinding"));
    ASSERT_NE(rebinding, nullptr);
    rebinding->UpdateBindList();
    auto* label = godot::cast_to<godot::Label>(
        tree.get_node("Camera/dialogue UI/menu ui/controller rebinding/rebind_select")->get_child(1));
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->get_text(), "Enter, Joypad Button 0 ");
}

// The Vita hides the rebind button. Pad navigation must not lead to it:
// Godot's _get_focus_neighbor skips a hidden neighbour by following its own
// neighbour in the same direction.
TEST_F(Options, OnTheVitaTheRebindButtonIsHiddenAndSkippedByTheFocusChain) {
    ASSERT_TRUE(app::HideRebindButton(tree));
    auto* rebind = godot::cast_to<godot::Control>(
        tree.get_node("Camera/dialogue UI/menu ui/options_controller/true button_rebinding"));
    ASSERT_NE(rebind, nullptr);
    EXPECT_FALSE(rebind->is_visible());
    // the Button itself is transparent (modulate a = 0); what the
    // player reads is its ButtonClass `ui`, the sibling Label "REBINDING".
    auto* label = godot::cast_to<godot::Control>(
        tree.get_node("Camera/dialogue UI/menu ui/options_controller/rebind controls"));
    ASSERT_NE(label, nullptr);
    EXPECT_FALSE(label->is_visible()) << "the REBINDING text stays on screen with nothing to select";
    menu->Show("controller");
    for (const char* from : {"true button_controller enable", "true button_mouse enable"}) {
        auto* c = godot::cast_to<godot::Control>(
            tree.get_node(std::string("Camera/dialogue UI/menu ui/options_controller/") + from));
        ASSERT_NE(c, nullptr);
        godot::Control* down = c->_get_focus_neighbor(3);   // SIDE_BOTTOM
        ASSERT_NE(down, nullptr) << from;
        EXPECT_NE(down, rebind);
        EXPECT_EQ(down->get_name(), "true button_return options controller");
    }
}

}  // namespace

namespace {

// The ACHIEVEMENTS entry is a real Button below EXIT: the pad goes EXIT ->
// ACHIEVEMENTS -> START through Godot's own focus links.
TEST_F(Options, ThePadReachesTheAchievementsEntryFromExit) {
    godot::Button* entry = app::AddAchievementsEntry(tree);
    ASSERT_NE(entry, nullptr);
    auto* exitButton = godot::cast_to<godot::Control>(
        tree.get_node("Camera/dialogue UI/menu ui/main screen/true button_exit"));
    ASSERT_NE(exitButton, nullptr);
    EXPECT_EQ(exitButton->_get_focus_neighbor(godot::SIDE_BOTTOM), entry);
    godot::Control* below = entry->_get_focus_neighbor(godot::SIDE_BOTTOM);
    ASSERT_NE(below, nullptr);
    EXPECT_EQ(below->get_name(), "true button_start");
    EXPECT_EQ(entry->_get_focus_neighbor(godot::SIDE_TOP), exitButton);
    EXPECT_TRUE(entry->is_visible_in_tree());
}

// The entry sits one menu row below EXIT, wherever the scene puts EXIT: v2.2's
// MULTIPLAYER row moved EXIT down onto the old fixed spot.
TEST_F(Options, TheAchievementsEntryIsOneRowBelowExit) {
    godot::Button* entry = app::AddAchievementsEntry(tree);
    ASSERT_NE(entry, nullptr);
    const std::string screen = "Camera/dialogue UI/menu ui/main screen/";
    auto* exitButton = godot::cast_to<godot::Control>(tree.get_node(screen + "true button_exit"));
    auto* credits = godot::cast_to<godot::Control>(tree.get_node(screen + "true button_credits"));
    ASSERT_NE(exitButton, nullptr);
    ASSERT_NE(credits, nullptr);
    const app::ScreenRect e = app::ScreenRectOf(*exitButton);
    const app::ScreenRect c = app::ScreenRectOf(*credits);
    const app::ScreenRect a = app::ScreenRectOf(*entry);
    EXPECT_FLOAT_EQ(a.y, e.y + (e.y - c.y));
    EXPECT_FLOAT_EQ(a.x + a.w * 0.5f, e.x + e.w * 0.5f) << "centred under EXIT";
    EXPECT_GE(a.y, e.y + e.h) << "no overlap with EXIT";
}

// On the Vita the CONTROLLER options (mouse or pad, key rebinding) and
// WINDOWED / FULLSCREEN mean nothing. They are hidden, RETURN closes the gap
// the CONTROLLER entry leaves, and the pad skips what is gone.
TEST_F(Options, OnTheVitaDesktopOnlyOptionsAreHidden) {
    ASSERT_TRUE(app::HideDesktopOnlyOptions(tree));
    const std::string ui = "Camera/dialogue UI/menu ui/";
    for (const char* path : {"sub options select/button_controller", "sub options select/true button_controller",
                             "options_audio video/true button_windowed", "options_audio video/true button_fullscreen",
                             "options_audio video/option monitor_windowed",
                             "options_audio video/option monitor_fullscreen",
                             "options_controller/true button_rebinding"}) {
        auto* c = godot::cast_to<godot::Control>(tree.get_node(ui + path));
        ASSERT_NE(c, nullptr) << path;
        EXPECT_FALSE(c->is_visible()) << path;
    }
    auto* returnLabel = godot::cast_to<godot::Control>(tree.get_node(ui + "sub options select/button_return"));
    EXPECT_LT(returnLabel->get_offset_since_load().y, 0.0f) << "RETURN moves up into the CONTROLLER slot";

    menu->Show("sub options");
    auto* language = godot::cast_to<godot::Control>(tree.get_node(ui + "sub options select/true button_language"));
    godot::Control* down = language->_get_focus_neighbor(3);   // SIDE_BOTTOM
    ASSERT_NE(down, nullptr);
    EXPECT_EQ(down->get_name(), "true button_exit sub options");
}

}  // namespace

// v2.2's three options: colorblind, music, greyscale death.
struct OptionsV22 : Options {
    void TearDown() override {
        Options::TearDown();
        scripts::GlobalVariables::colorblind = false;
        scripts::GlobalVariables::greyscale_death = false;
        scripts::GlobalVariables::music_enabled = true;
        for (int bus : {1, 2, 4}) godot::AudioServer::get_singleton().set_bus_mute(bus, false);
    }
};

TEST_F(OptionsV22, WithNoSettingsFileGreyscaleIsTickedAndColorblindIsNot) {  // gd: _ready
    EXPECT_FALSE(options->setting_colorblind);
    EXPECT_TRUE(options->setting_greyscale_death);
    EXPECT_TRUE(scripts::GlobalVariables::greyscale_death);
    EXPECT_EQ(options->checkmark_greyscale->ui->get_texture(), "res://misc/checkmark_active2.png");
}

TEST_F(OptionsV22, TheTogglesFlipTheSettingTheCheckmarkAndTheGlobal) {  // gd: ToggleColorblind, ToggleGreyscaleDeath
    options->ToggleColorblind();
    EXPECT_TRUE(options->setting_colorblind);
    EXPECT_TRUE(scripts::GlobalVariables::colorblind);
    EXPECT_EQ(options->checkmark_colorblind->ui->get_texture(), "res://misc/checkmark_active2.png");
    options->ToggleGreyscaleDeath();
    EXPECT_FALSE(options->setting_greyscale_death);
    EXPECT_FALSE(scripts::GlobalVariables::greyscale_death);
    EXPECT_EQ(options->checkmark_greyscale->ui->get_texture(), "res://misc/checkmark_inactive.png");
}

TEST_F(OptionsV22, MusicOffStopsTheVinylAndMutesTheMusicBuses) {  // gd: AdjustSettings_music, ApplySettings_music
    options->AdjustSettings_music();
    EXPECT_FALSE(options->setting_music_enabled);
    EXPECT_FALSE(scripts::GlobalVariables::music_enabled);
    EXPECT_FLOAT_EQ(options->anim_vinyl->get_speed_scale(), 0.0f);
    const godot::AudioServer& server = godot::AudioServer::get_singleton();
    EXPECT_TRUE(server.is_bus_mute(1));
    EXPECT_TRUE(server.is_bus_mute(2));
    EXPECT_TRUE(server.is_bus_mute(4));
    EXPECT_FALSE(server.is_bus_mute(3)) << "interactions stay audible";
    options->AdjustSettings_music();
    EXPECT_FALSE(server.is_bus_mute(1));
    EXPECT_FLOAT_EQ(options->anim_vinyl->get_speed_scale(), 1.0f);
}

TEST_F(OptionsV22, TheNewSettingsSurviveARestart) {  // gd: SaveSettings, LoadSettings
    options->ToggleColorblind();
    options->AdjustSettings_music();
    options->SaveSettings();
    scripts::GlobalVariables::colorblind = false;
    godot::SceneTree again;
    Build(again);
    EXPECT_TRUE(options->setting_colorblind);
    EXPECT_FALSE(options->setting_music_enabled);
    EXPECT_TRUE(scripts::GlobalVariables::colorblind);
    EXPECT_TRUE(godot::AudioServer::get_singleton().is_bus_mute(1));
    EXPECT_EQ(options->checkmark_colorblind->ui->get_texture(), "res://misc/checkmark_active2.png");
}

// A settings file from before v2.2 has none of the three keys: LoadSettings
// checks data.has() for each, so v2.2's defaults stand.
TEST_F(OptionsV22, AnOlderSettingsFileKeepsTheNewDefaults) {
    store.Save(options->savePath, game::SaveMap{{"setting_volume", 0.5}, {"setting_windowed", false},
                                                {"setting_language", std::string("EN")},
                                                {"setting_controllerEnabled", false}});
    testing_support::PrintCapture log;
    godot::SceneTree again;
    Build(again);
    // gd: print("user settings: ", data): Godot's Dictionary text (keys
    // sorted here: the save map is ordered).
    EXPECT_TRUE(log.Has("user settings: { \"setting_controllerEnabled\": false, \"setting_language\": \"EN\", "
                        "\"setting_volume\": 0.5, \"setting_windowed\": false }"));
    EXPECT_TRUE(options->receivedFile);
    EXPECT_NEAR(options->setting_volume, 0.5, 1e-12);
    EXPECT_FALSE(options->setting_colorblind);
    EXPECT_TRUE(options->setting_music_enabled);
    EXPECT_TRUE(options->setting_greyscale_death);
    EXPECT_TRUE(scripts::GlobalVariables::greyscale_death);
}
