// heaven.tscn, the true death's scene (HeavenManager.gd), on the real heaven
// export.
#include "assets/animation.h"
#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/input_map.h"
#include "godot/runtime_scene_loader.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"
#include "scripts/ButtonClass.h"
#include "scripts/CursorManager.h"
#include "scripts/HeavenManager.h"
#include "scripts/InteractionBranch.h"
#include "scripts/InteractionManager.h"
#include "scripts/JankPreventer.h"
#include "scripts/bind_support.h"

#include "print_capture.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

namespace {

std::string Extracted(const char* file) {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/" + file;
}

bool FileExists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

struct HeavenTree : ::testing::Test {
    assets::AnimationLibrary animations;
    godot::SceneTree tree;
    std::vector<std::string> bindErrors;
    scripts::Heaven* heaven = nullptr;

    void SetUp() override {
        for (const char* f : {"runtime_heaven.json", "animations_heaven.json", "dialogue_en.json", "input_map.json"}) {
            if (!FileExists(Extracted(f))) GTEST_SKIP() << "heaven exports missing";
        }
        godot::set_translation_root(std::string(BSHT_SOURCE_DIR) + "/assets/extracted");
        godot::set_locale("EN");
        godot::InputMap::get_singleton().load(Extracted("input_map.json").c_str());
        scripts::RegisterScripts();
        ASSERT_EQ(godot::LoadRuntimeScene(Extracted("runtime_heaven.json").c_str(), tree),
                  godot::RuntimeSceneError::None);
        ASSERT_TRUE(animations.LoadFromFile(Extracted("animations_heaven.json").c_str()));
        godot::BindAnimationLibrary(tree, animations);
        bindErrors = scripts::BindScripts(tree.get_root()).errors;
        heaven = godot::script_cast<scripts::Heaven>(tree.get_node("standalone managers/heaven manager"));
        ASSERT_NE(heaven, nullptr);
    }

    void Run(double seconds) {
        const int frames = static_cast<int>(seconds * 60.0 + 0.5);
        for (int i = 0; i < frames; ++i) tree.process_frame(1.0 / 60.0);
    }
};

TEST_F(HeavenTree, HeavenBindsItsExports) {
    for (const std::string& e : bindErrors) ADD_FAILURE() << e;
    EXPECT_EQ(heaven->buttonArray.size(), 2u);
    EXPECT_NE(heaven->animator, nullptr);
    EXPECT_NE(heaven->intbranch_heavendoor, nullptr);
    EXPECT_NE(heaven->btn_door, nullptr);
    auto* im = godot::script_cast<scripts::InteractionManager>(tree.get_node("standalone managers/interaction manager"));
    ASSERT_NE(im, nullptr);
    EXPECT_EQ(im->heaven, heaven);
}

// gd: L15-L20, L56-L67: +1 s the music, the blackout off, the pan then the idle;
// +11.1 s the door.
TEST_F(HeavenTree, TheCameraPansDownThenTheDoorOpensForUse) {
    tree.ready_scripts();
    EXPECT_TRUE(heaven->viewblocker->is_visible());
    Run(1.05);
    EXPECT_FALSE(heaven->viewblocker->is_visible());
    EXPECT_EQ(heaven->animator->get_current_animation(), "camera pan down");
    EXPECT_EQ(heaven->animator->get_queue().size(), 1u);
    EXPECT_FALSE(heaven->btn_door->is_visible());
    Run(11.1);
    EXPECT_TRUE(heaven->btn_door->is_visible());
    EXPECT_TRUE(heaven->intbranch_heavendoor->interactionAllowed);
    EXPECT_TRUE(heaven->cursor->cursor_visible);
    EXPECT_EQ(heaven->animator->get_current_animation(), "camera idle") << "queued after the pan";
}

// gd: L69-L82 through InteractionManager's alias.
TEST_F(HeavenTree, TheDoorFliesAndThenOffersRetryAndExit) {
    tree.ready_scripts();
    Run(12.2);
    auto* im = godot::script_cast<scripts::InteractionManager>(tree.get_node("standalone managers/interaction manager"));
    im->InteractWith("heaven door");
    EXPECT_FALSE(heaven->btn_door->is_visible());
    EXPECT_FALSE(heaven->bracket_door->is_visible());
    // v2.2: the hidden bracket is also moved out of the way.
    const godot::Vector3 away = heaven->bracket_door->get_global_position();
    EXPECT_FLOAT_EQ(away.x, 0.0f);
    EXPECT_FLOAT_EQ(away.y, 0.0f);
    EXPECT_FLOAT_EQ(away.z, -100.0f);
    EXPECT_FALSE(heaven->intbranch_heavendoor->interactionAllowed);
    EXPECT_EQ(heaven->animator->get_current_animation(), "move");
    EXPECT_FALSE(heaven->buttonArray[0]->isActive);
    Run(15.7);
    EXPECT_TRUE(heaven->buttonArray[0]->isActive);
    EXPECT_TRUE(heaven->buttonArray[1]->isActive);
    EXPECT_FALSE(im->checking);
}

// gd: L31-L50: RETRY once -> death.tscn after 3.12 s; a second
// press finds the buttons inactive.
TEST_F(HeavenTree, RetryGoesBackThroughTheDeathSceneOnce) {
    testing_support::PrintCapture log;
    tree.ready_scripts();
    for (scripts::ButtonClass* b : heaven->buttonArray) b->isActive = true;
    heaven->buttonArray[0]->OnPress();
    heaven->buttonArray[0]->OnPress();
    EXPECT_EQ(heaven->animator->get_current_animation(), "fade out");
    for (godot::Control* u : heaven->ui) EXPECT_FALSE(u->is_visible());
    Run(3.0);
    EXPECT_EQ(tree.take_scene_change(), "");
    Run(0.2);
    EXPECT_EQ(tree.take_scene_change(), "res://scenes/death.tscn");
    EXPECT_TRUE(log.Has("changing scene to: death"));   // v2.2
    Run(4.0);
    EXPECT_EQ(tree.take_scene_change(), "") << "one request";
    EXPECT_FALSE(tree.take_quit());
}

TEST_F(HeavenTree, ExitQuits) {
    tree.ready_scripts();
    for (scripts::ButtonClass* b : heaven->buttonArray) b->isActive = true;
    heaven->buttonArray[1]->OnPress();
    Run(3.2);
    EXPECT_TRUE(tree.take_quit());
    EXPECT_EQ(tree.take_scene_change(), "");
}

// JankPreventer.gd L15-L18 (isHeaven): v2.2 clears the bracket label in every
// locale (the buttons frame their own captions now).
TEST_F(HeavenTree, JankPreventerClearsTheBracketLabel) {
    godot::set_locale("DE");
    tree.ready_scripts();
    Run(1.1);
    auto* label = tree.get_node_as<godot::Label>("Camera/dialogue UI/death ui/ui_retry");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->get_text(), "");
    godot::set_locale("EN");
}

}  // namespace

namespace {

// v2.2: RETRY and EXIT show "<  RETRY  >" while hovered (ButtonClass adding_cursor).
TEST_F(HeavenTree, HoveringRetryFramesItsCaption) {
    tree.ready_scripts();
    auto* retry = godot::script_cast<scripts::ButtonClass>(
        tree.get_node("Camera/dialogue UI/death ui/text_retry/retry/button_retry"));
    ASSERT_NE(retry, nullptr);
    ASSERT_TRUE(retry->adding_cursor);
    ASSERT_NE(retry->t, nullptr);
    const std::string caption = godot::tr(retry->orig);
    retry->isActive = true;
    retry->mainActive = true;
    retry->OnHover();
    EXPECT_EQ(retry->t->get_text(), "<  " + caption + "  >");
    retry->OnExit();
    EXPECT_EQ(retry->t->get_text(), caption);
}

}  // namespace
