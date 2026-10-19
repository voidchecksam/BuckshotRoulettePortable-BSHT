// scripts/DialogueManager.gd translated: the typewriter the dealer speaks through.
#include "scripts/DialogueManager.h"

#include "godot/rng.h"
#include "godot/scene_tree.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

struct DialogueFixture {
    godot::SceneTree tree;
    godot::Label* label = nullptr;
    godot::ColorRect* backdrop = nullptr;
    godot::AudioStreamPlayer* click = nullptr;
    scripts::Dialogue* dialogue = nullptr;

    DialogueFixture() {
        godot::Node* root = tree.add_node(std::make_unique<godot::Node>("main"), nullptr);
        label = godot::cast_to<godot::Label>(tree.add_node(std::make_unique<godot::Label>("text"), root));
        label->set_visible(false);
        backdrop = godot::cast_to<godot::ColorRect>(
            tree.add_node(std::make_unique<godot::ColorRect>("backdrop"), root));
        backdrop->set_visible(false);
        backdrop->set_scale({13.969f, 1.748f});
        click = godot::cast_to<godot::AudioStreamPlayer>(
            tree.add_node(std::make_unique<godot::AudioStreamPlayer>("click"), root));

        auto script = std::make_unique<scripts::Dialogue>();
        script->speaker_click = click;
        script->soundArray_clicks = {"c1.ogg", "c2.ogg", "c3.ogg", "c4.ogg"};
        script->dialogueUI = label;
        script->dialogueUI_backdrop = backdrop;
        script->dialogueSpeed = 1.0;
        script->incrementDelay = 0.02;
        dialogue = script.get();
        auto node = std::make_unique<godot::Node>("dialogue manager");
        node->set_script(std::move(script));
        tree.add_node(std::move(node), root);
        tree.ready_scripts();
    }

    void frames(int n) {
        for (int i = 0; i < n; ++i) tree.process_frame(1.0 / 60.0);
    }
};

}  // namespace

TEST(ScriptsDialogue, ReadyTakesTheFourthClickAndTheBackdropScale) {
    DialogueFixture f;
    EXPECT_EQ(f.click->get_stream(), "c4.ogg");
    EXPECT_FLOAT_EQ(f.dialogue->origscale_backdrop.x, 13.969f);
}

TEST(ScriptsDialogue, ShowTextForeverTypesOneCharacterPerIncrementDelay) {
    DialogueFixture f;
    f.dialogue->ShowText_Forever("ABC");
    EXPECT_TRUE(f.label->is_visible());
    EXPECT_TRUE(f.backdrop->is_visible());
    EXPECT_EQ(f.label->get_text(), "ABC");
    EXPECT_EQ(f.label->get_visible_characters(), 1);   // TickText ran to its first await
    f.frames(2);                                        // 0.033 s: one 0.02 s timer fired
    EXPECT_EQ(f.label->get_visible_characters(), 2);
    f.frames(12);
    EXPECT_EQ(f.label->get_visible_characters(), 3);    // stops at visible_ratio 1
    EXPECT_FALSE(f.dialogue->looping);
    EXPECT_TRUE(f.label->is_visible());                 // Forever stays up
}

TEST(ScriptsDialogue, ScalingWidensTheBackdropForOneLine) {
    DialogueFixture f;
    f.dialogue->scaling = true;
    f.dialogue->ShowText_Forever("AB");
    EXPECT_FLOAT_EQ(f.backdrop->get_scale().x, 17.209f);
    EXPECT_FALSE(f.dialogue->scaling);
    f.dialogue->ShowText_Forever("CD");
    EXPECT_FLOAT_EQ(f.backdrop->get_scale().x, 13.969f);
}

TEST(ScriptsDialogue, ShowTextForDurationHidesAfterTheDuration) {
    DialogueFixture f;
    f.tree.spawn(f.dialogue->ShowText_ForDuration("AB", 1.0));
    f.frames(55);
    EXPECT_TRUE(f.label->is_visible());
    f.frames(10);
    EXPECT_FALSE(f.label->is_visible());
    EXPECT_FALSE(f.backdrop->is_visible());
    EXPECT_FALSE(f.dialogue->looping);
}

TEST(ScriptsDialogue, HideTextStopsTheTypewriter) {
    DialogueFixture f;
    f.dialogue->ShowText_Forever("ABCDEFGH");
    f.dialogue->HideText();
    f.frames(20);
    EXPECT_FALSE(f.label->is_visible());
    EXPECT_EQ(f.label->get_visible_characters(), 1);
}

TEST(ScriptsDialogue, ClickPitchIsDrawnFromTheGlobalRng) {
    DialogueFixture f;
    godot::seed(5);
    const double expected = godot::randf_range(0.5, 0.6);
    godot::seed(5);
    f.dialogue->ShowText_Forever("AB");
    EXPECT_FLOAT_EQ(f.click->get_pitch_scale(), static_cast<float>(expected));
    f.dialogue->dealerLowPitched = true;
    godot::seed(6);
    const double low = godot::randf_range(0.2, 0.3);
    godot::seed(6);
    f.dialogue->ShowText_Forever("CD");
    EXPECT_FLOAT_EQ(f.click->get_pitch_scale(), static_cast<float>(low));
}
