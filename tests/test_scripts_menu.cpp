// The menu's scripts, translated 1:1, on the real menu tree (runtime_menu.json
// + animations_menu.json). MenuManager.gd is the reference for every
// expectation; its line numbers are cited.
#include "app/port_branding.h"
#include "app/scene_host.h"
#include "assets/animation.h"
#include "game/music_router.h"
#include "game/save_store.h"
#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/input_map.h"
#include "godot/node.h"
#include "godot/print.h"
#include "godot/runtime_scene_loader.h"
#include "godot/scene_tree.h"
#include "scripts/ButtonClass.h"
#include "scripts/ControllerManager.h"
#include "scripts/CursorManager.h"
#include "scripts/GlobalVariables.h"
#include "scripts/MenuManager.h"
#include "scripts/OptionsManager.h"
#include "scripts/RoundManager.h"
#include "scripts/SaveFileManager.h"
#include "scripts/bind_support.h"
#include "scripts/resources.h"
#include "print_capture.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

const std::string kData = std::string(BSHT_SOURCE_DIR) + "/assets/extracted";
constexpr double kFrame = 1.0 / 60.0;

std::string SaveDir(const char* name) {
    const std::string dir = std::string(BSHT_BINARY_DIR) + "/menu_test_" + name;
    std::remove((dir + "/main_12.sav").c_str());
    return dir;
}

struct Menu : ::testing::Test {
    godot::SceneTree tree;
    assets::AnimationLibrary anims;
    game::SaveStore store{SaveDir(
        ::testing::UnitTest::GetInstance()->current_test_info()->name())};
    scripts::MenuManager* menu = nullptr;
    scripts::SaveFileManager* save = nullptr;

    void SetUp() override {
        Bind();
        if (!IsSkipped() && !HasFatalFailure()) Ready();
    }

    // The tree loaded and its scripts bound, as GameApp has it before _ready.
    void Bind() {
        if (godot::LoadRuntimeScene((kData + "/runtime_menu.json").c_str(), tree) !=
            godot::RuntimeSceneError::None) {
            GTEST_SKIP() << "export runtime_menu.json first";
        }
        ASSERT_TRUE(anims.LoadFromFile((kData + "/animations_menu.json").c_str()));
        godot::BindAnimationLibrary(tree, anims);
        godot::InputMap::get_singleton().load((kData + "/input_map.json").c_str());
        scripts::RegisterScripts();
        scripts::BindScripts(tree.get_root());
        menu = godot::script_cast<scripts::MenuManager>(tree.get_node("standalone managers/menu manager"));
        save = godot::script_cast<scripts::SaveFileManager>(tree.get_node("standalone managers/SaveFileManager"));
        ASSERT_NE(menu, nullptr);
        ASSERT_NE(save, nullptr);
        save->store = &store;
    }

    void Ready() {
        scripts::GlobalVariables()._ready();   // the autoload, as GameApp runs it
        tree.ready_scripts();
    }

    void Seconds(double s) {
        for (int i = 0; i < static_cast<int>(s * 60.0 + 0.5); ++i) tree.process_frame(kFrame);
    }
};

// The tree bound but not readied: each test readies it after its own setup.
struct MenuBeforeReady : Menu {
    void SetUp() override { Bind(); }
};

// Back from a game, the menu's music must play. MenuManager plays its track in
// _ready (Intro); with the music router bound only after ready, the sink would
// take the play for a sound with no length and the player would stop a frame
// later (the router sending Play, then Stop).
TEST_F(MenuBeforeReady, TheMenuTrackSurvivesReadyWhenTheRouterIsBoundFirst) {
    game::MusicRouter router("data", "");
    const app::SceneMusic music = app::FindSceneMusic(tree);
    ASSERT_EQ(music.player, menu->speaker_music);
    router.Bind(music.player);
    tree.set_audio_sink([&router](const godot::AudioPlayback& a, const godot::AudioPlayRequest&) {
        return godot::AudioPlayResult{router.IsMusic(&a) ? router.OnPlay() : 0.0f, -1};
    });

    Ready();
    const game::MusicRouter::Change play = router.Poll();
    ASSERT_EQ(play.kind, game::MusicRouter::Kind::Play);
    EXPECT_EQ(play.path, "data/" + menu->speaker_music->get_stream());
    Seconds(1.0);
    EXPECT_TRUE(menu->speaker_music->is_playing()) << "the menu track stopped by itself";
    EXPECT_NE(router.Poll().kind, game::MusicRouter::Kind::Stop);
}

TEST_F(Menu, ReadyShowsTheMainScreenOnly) {  // gd: L26, L95-L105
    EXPECT_EQ(menu->currentScreen, "main");
    EXPECT_TRUE(menu->parent_main->is_visible());
    for (godot::Control* s : menu->screens) {
        if (s != menu->parent_main) EXPECT_FALSE(s->is_visible()) << s->get_path();
    }
    EXPECT_TRUE(menu->title->is_visible());
}

// v2.2: the splash starts at once, and _process hands over focus once nine
// seconds have passed (T(), FinishIntro).
TEST_F(Menu, TheIntroPlaysTheSplashAtOnceAndHandsOverFocusAfterNineSeconds) {
    Seconds(0.1);
    EXPECT_EQ(menu->animator_intro->get_current_animation(), "splash screen");
    EXPECT_TRUE(menu->viewing_intro);
    Seconds(8.8);
    EXPECT_TRUE(menu->mouseblocker->is_visible());
    Seconds(0.3);
    EXPECT_FALSE(menu->mouseblocker->is_visible());
    EXPECT_FALSE(menu->viewing_intro);
    EXPECT_EQ(menu->controller->previousFocus, menu->firstFocus_main);
    EXPECT_TRUE(menu->assigningFocus);
}

TEST_F(Menu, ExitGameDuringTheIntroSkipsIt) {  // gd: _input, SkipIntro
    Seconds(1.0);
    auto esc = std::make_shared<godot::InputEventKey>();
    esc->keycode = 4194305;   // Escape, "exit game"
    esc->pressed = true;
    menu->_input(esc);
    EXPECT_EQ(menu->animator_intro->get_current_animation(), "skip intro");
    EXPECT_FALSE(menu->viewing_intro);
    EXPECT_FALSE(menu->mouseblocker->is_visible());
    EXPECT_EQ(menu->controller->previousFocus, menu->firstFocus_main);
}

TEST_F(Menu, TheVersionLabelIsTheGamesVersion) {  // gd: _ready
    EXPECT_EQ(menu->version->get_text(), "v2.2.0 (STEAM)");
}

// The port's version goes over the game's after MenuManager._ready wrote it:
// the menu reads v1.0 (PSVITA).
TEST_F(Menu, ThePortsVersionIsWrittenOverTheGamesAfterReady) {
    EXPECT_EQ(app::PortBranding::WriteVersion(tree), "v2.2.0 (STEAM)") << "what it replaced, for the log";
    EXPECT_EQ(menu->version->get_text(), app::PortBranding::kVersion);
}

TEST_F(Menu, TheThreeNewOptionButtonsReachTheOptionsManager) {  // gd: _ready, Toggle*
    menu->buttons_options[6]->get_node_owner()->emit_signal("is_pressed");
    menu->buttons_options[7]->get_node_owner()->emit_signal("is_pressed");
    menu->buttons_options[8]->get_node_owner()->emit_signal("is_pressed");
    EXPECT_TRUE(menu->optionmanager->setting_colorblind);
    EXPECT_FALSE(menu->optionmanager->setting_music_enabled);
    EXPECT_FALSE(menu->optionmanager->setting_greyscale_death);
    menu->buttons_options[7]->get_node_owner()->emit_signal("is_pressed");   // music back on
    scripts::GlobalVariables::colorblind = false;
}

// "show credits" ends on a METHOD key calling ReturnToLastScreen.
TEST_F(Menu, TheCreditsAnimationReturnsByItself) {
    menu->Credits();
    EXPECT_EQ(menu->currentScreen, "credits");
    EXPECT_TRUE(menu->call("ReturnToLastScreen", {}));
    EXPECT_EQ(menu->currentScreen, "main");
}

TEST_F(Menu, CreditsPlaysItsAnimationAndHidesTheRest) {  // gd: L109-L113
    menu->Show("credits");
    EXPECT_TRUE(menu->parent_creds->is_visible());
    EXPECT_FALSE(menu->parent_main->is_visible());
    EXPECT_FALSE(menu->title->is_visible());
    Seconds(0.1);
    EXPECT_EQ(menu->anim_creds->get_current_animation(), "show credits");
}

TEST_F(Menu, ReturningFromRebindingGoesToSubOptions) {  // gd: L130-L135
    menu->Show("sub options");
    menu->Show("rebind controls");
    menu->ReturnToLastScreen();
    EXPECT_EQ(menu->currentScreen, "sub options");
    menu->ReturnToLastScreen();
    EXPECT_EQ(menu->currentScreen, "main");
}

TEST_F(Menu, StartSnapsClearsTheSaveAndChangesToMainAfterFourSeconds) {  // gd: L142-L155
    store.Save("main_12", {{"current_batch_index", int64_t{2}}});
    ASSERT_TRUE(store.Exists("main_12"));
    tree.get_root();
    menu->buttons[0]->get_node_owner()->emit_signal("is_pressed");
    EXPECT_FALSE(menu->buttons[0]->isActive);
    EXPECT_FALSE(store.Exists("main_12"));
    Seconds(0.1);
    EXPECT_EQ(menu->animator_intro->get_current_animation(), "snap");
    Seconds(3.8);
    EXPECT_TRUE(tree.take_scene_change().empty());
    Seconds(0.3);
    EXPECT_EQ(tree.take_scene_change(), "res://scenes/main.tscn");
}

// PORT: v2.2 opens the Steam lobby (StartMultiplayer); the port says COMING SOON.
std::string Caption(const scripts::MenuManager& m) {
    const auto* label = godot::cast_to<godot::Label>(m.buttons[22]->ui);
    return label ? label->get_text() : std::string("<no label>");
}

TEST_F(Menu, PressingMultiplayerSaysComingSoon) {
    ASSERT_GT(menu->buttons.size(), 22u);
    ASSERT_NE(menu->buttons[22], nullptr);
    ASSERT_EQ(menu->buttons[22]->ui->get_name(), "button_multiplayer");
    testing_support::PrintCapture printed;
    menu->buttons[22]->get_node_owner()->emit_signal("is_pressed");
    EXPECT_EQ(Caption(*menu), "COMING SOON");
    EXPECT_TRUE(printed.Has("menu: multiplayer pressed -> coming soon"));
    Seconds(0.5);
    EXPECT_TRUE(tree.take_scene_change().empty());
    EXPECT_EQ(menu->currentScreen, "main");
}

TEST_F(Menu, ComingSoonGoesBackAfterOneAndAHalfSeconds) {
    menu->buttons[22]->get_node_owner()->emit_signal("is_pressed");
    Seconds(1.4);
    EXPECT_EQ(Caption(*menu), "COMING SOON");
    Seconds(0.2);
    EXPECT_EQ(Caption(*menu), "MP_UI MULTIPLAYER");
}

TEST_F(Menu, ASecondPressRestartsTheComingSoonTimer) {
    menu->buttons[22]->get_node_owner()->emit_signal("is_pressed");
    Seconds(1.0);
    menu->buttons[22]->get_node_owner()->emit_signal("is_pressed");
    Seconds(1.0);
    EXPECT_EQ(Caption(*menu), "COMING SOON");
    Seconds(0.6);
    EXPECT_EQ(Caption(*menu), "MP_UI MULTIPLAYER");
}

// The caption comes back as it was, whatever it was (a locale, a rename).
TEST_F(Menu, ComingSoonPutsBackTheCaptionItFound) {
    auto* label = godot::cast_to<godot::Label>(menu->buttons[22]->ui);
    ASSERT_NE(label, nullptr);
    label->set_text("MULTI");
    menu->buttons[22]->get_node_owner()->emit_signal("is_pressed");
    Seconds(1.0);
    menu->buttons[22]->get_node_owner()->emit_signal("is_pressed");
    Seconds(1.6);
    EXPECT_EQ(Caption(*menu), "MULTI");
}

TEST_F(Menu, ExitSnapsThenQuits) {  // gd: L161-L168
    menu->buttons[3]->get_node_owner()->emit_signal("is_pressed");
    Seconds(0.1);
    EXPECT_EQ(menu->animator_intro->get_current_animation(), "snap2");
    EXPECT_FALSE(tree.take_quit());
    Seconds(0.5);
    EXPECT_TRUE(tree.take_quit());
}

// SaveFileManager: SaveGame writes every field LoadGame reads (gd: L14-L69).
TEST(SaveFileManager, SaveThenLoadRoundTripsPlayerData) {
    godot::SceneTree tree;
    godot::Node* root = tree.add_node(std::make_unique<godot::Node>("main"), nullptr);
    godot::Node* rmNode = tree.add_node(std::make_unique<godot::Node>("round manager"), root);
    auto rmScript = std::make_unique<scripts::RoundManager>();
    scripts::RoundManager* rm = rmScript.get();
    rmNode->set_script(std::move(rmScript));
    scripts::PlayerData pd;
    rm->playerData = &pd;
    godot::Node* sNode = tree.add_node(std::make_unique<godot::Node>("save"), root);
    auto sScript = std::make_unique<scripts::SaveFileManager>();
    scripts::SaveFileManager* s = sScript.get();
    sNode->set_script(std::move(sScript));
    game::SaveStore store(SaveDir("roundtrip"));
    s->store = &store;
    s->roundManager = rm;

    pd.hasReadIntroduction = true;
    pd.currentBatchIndex = 2;
    pd.playername = "BALLS";
    pd.stat_beerDrank = 7;
    pd.seenGod = true;
    s->SaveGame();

    scripts::PlayerData fresh;
    rm->playerData = &fresh;
    s->LoadGame();
    EXPECT_TRUE(fresh.hasReadIntroduction);
    EXPECT_EQ(fresh.currentBatchIndex, 2);
    EXPECT_EQ(fresh.playername, "BALLS");
    EXPECT_EQ(fresh.stat_beerDrank, 7);
    EXPECT_TRUE(fresh.seenGod);

    s->ClearSave();
    EXPECT_FALSE(store.Exists("main_12"));
}

}  // namespace

// ControllerManager.mouseRaycast is null in menu.tscn, and leaving controller
// focus calls mouseRaycast->StopRaycastOverride() (the first d-pad press on
// the menu). Godot reports a call on a null instance as a script error and
// plays on; the port must not crash.
TEST_F(Menu, LeavingControllerFocusWithNoMouseRaycastDoesNotCrash) {
    auto* ctrl = godot::script_cast<scripts::ControllerManager>(tree.get_node("standalone managers/controller manager"));
    ASSERT_NE(ctrl, nullptr);
    ASSERT_EQ(ctrl->mouseRaycast, nullptr) << "menu.tscn leaves it unset";
    ctrl->stoppingOverride = true;
    ctrl->SetPrevFocus(true);    // controller takes focus
    ctrl->SetPrevFocus(false);   // and gives it back
    EXPECT_TRUE(ctrl->fs2);
}

namespace {

// v2.2 logs each step of the main save (SaveFileManager.gd).
TEST(SaveFileManager, EachStepOfTheSaveIsLogged) {
    godot::SceneTree tree;
    godot::Node* root = tree.add_node(std::make_unique<godot::Node>("main"), nullptr);
    godot::Node* rmNode = tree.add_node(std::make_unique<godot::Node>("round manager"), root);
    auto rmScript = std::make_unique<scripts::RoundManager>();
    scripts::RoundManager* rm = rmScript.get();
    rmNode->set_script(std::move(rmScript));
    scripts::PlayerData pd;
    rm->playerData = &pd;
    godot::Node* sNode = tree.add_node(std::make_unique<godot::Node>("save"), root);
    auto sScript = std::make_unique<scripts::SaveFileManager>();
    scripts::SaveFileManager* s = sScript.get();
    sNode->set_script(std::move(sScript));
    game::SaveStore store(SaveDir("logged"));
    s->store = &store;
    s->roundManager = rm;

    std::vector<std::string> lines;
    godot::set_print_sink([&lines](const std::string& l) { lines.push_back(l); });
    s->LoadGame();
    s->SaveGame();
    s->LoadGame();
    s->ClearSave();
    godot::set_print_sink(nullptr);
    const std::vector<std::string> expected = {"user does not have save file", "attempting to save game",
                                               "save file closed", "attempting to load save file",
                                               "closing save file", "clearing save file"};
    EXPECT_EQ(lines, expected);
}

}  // namespace

// v2.2's ControllerManager: a mouse motion with zero velocity does not take
// control from the pad; a moving one does.
TEST_F(Menu, OnlyAMovingMouseTakesControlFromThePad) {
    auto* ctrl = godot::script_cast<scripts::ControllerManager>(tree.get_node("standalone managers/controller manager"));
    ASSERT_NE(ctrl, nullptr);
    ctrl->dynamicallySwappingDevice = true;
    ctrl->checkingForInput = true;
    ctrl->cursor->controller_active = true;
    auto still = std::make_shared<godot::InputEventMouseMotion>();
    ctrl->_input(still);
    EXPECT_TRUE(ctrl->cursor->controller_active);
    auto moving = std::make_shared<godot::InputEventMouseMotion>();
    moving->velocity = {40.0f, 0.0f};
    ctrl->_input(moving);
    EXPECT_FALSE(ctrl->cursor->controller_active);
}

// The loading screen starts the menu's track before OptionsManager exists, so
// the entry points read the saved music setting themselves.
TEST(SavedOptions, MusicOffIsReadBeforeTheMenu) {
    const std::string dir = std::string(BSHT_BINARY_DIR) + "/options_music_test";
    const game::SaveStore store(dir);
    store.Clear(scripts::OptionsManager::savePath);
    EXPECT_TRUE(scripts::OptionsManager::SavedMusicEnabled(&store)) << "no options saved yet";
    EXPECT_TRUE(scripts::OptionsManager::SavedMusicEnabled(nullptr));
    game::SaveMap data;
    data["setting_music_enabled"] = false;
    ASSERT_TRUE(store.Save(scripts::OptionsManager::savePath, data));
    EXPECT_FALSE(scripts::OptionsManager::SavedMusicEnabled(&store));
    data["setting_music_enabled"] = true;
    ASSERT_TRUE(store.Save(scripts::OptionsManager::savePath, data));
    EXPECT_TRUE(scripts::OptionsManager::SavedMusicEnabled(&store));
}

// A player's boot honours the saved setting; a diagnostic or scenario boot
// never reads the player's saves, so its music plays.
TEST(SavedOptions, TheLoadingScreenPlaysMusicOnlyWhenTheSavesAllowIt) {
    const std::string dir = std::string(BSHT_BINARY_DIR) + "/options_boot_music_test";
    const game::SaveStore store(dir);
    game::SaveMap data;
    data["setting_music_enabled"] = false;
    ASSERT_TRUE(store.Save(scripts::OptionsManager::savePath, data));
    EXPECT_FALSE(app::MenuMusicAtBoot(dir, false, false));
    EXPECT_TRUE(app::MenuMusicAtBoot(dir, true, false));
    EXPECT_TRUE(app::MenuMusicAtBoot(dir, false, true));
    store.Clear(scripts::OptionsManager::savePath);
    EXPECT_TRUE(app::MenuMusicAtBoot(dir, false, false));
}
