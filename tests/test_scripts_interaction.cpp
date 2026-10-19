// The original's controller path, run on a small tree built the way main.tscn
// is: the Camera carries MouseRaycast, a bracket Button carries a
// ButtonClass_Main child, the shotgun is a StaticBody3D under a Node3D whose
// InteractionBranch names it, and CursorManager / ControllerManager /
// InteractionManager live under "standalone managers", after the Camera in tree
// order as in the scene, so MouseRaycast processes first.
#include "assets/animation.h"
#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/input.h"
#include "godot/input_map.h"
#include "godot/physics.h"
#include "godot/scene_tree.h"
#include "godot/rng.h"
#include "scripts/BlinkerBranch.h"
#include "scripts/ButtonClass_Main.h"
#include "scripts/ControllerManager.h"
#include "scripts/CursorManager.h"
#include "scripts/DecisionTextManager.h"
#include "scripts/DescriptionManager.h"
#include "scripts/InteractionBranch.h"
#include "scripts/InteractionManager.h"
#include "scripts/ItemManager.h"
#include "scripts/MouseRaycast.h"
#include "scripts/ObjectShaker.h"
#include "scripts/PermissionManager.h"
#include "scripts/PickupIndicator.h"
#include "scripts/RoundManager.h"
#include "scripts/ShellEjectManager.h"
#include "scripts/ShotgunShooting.h"
#include "scripts/TextInteraction.h"
#include "scripts/TextShaker.h"
#include "scripts/resources.h"

#include "scripts/intro_skip.h"

#include "godot/material.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

godot::InputEventRef KeyEv(int64_t code, bool pressed) {
    auto k = std::make_shared<godot::InputEventKey>();
    k->keycode = code;
    k->pressed = pressed;
    k->device = 0;
    return k;
}

template <class T>
T* Attach(godot::Node* node) {
    auto script = std::make_unique<T>();
    T* raw = script.get();
    node->set_script(std::move(script));
    return raw;
}

struct InteractionTree : ::testing::Test {
    godot::SceneTree t;
    godot::Camera3D* camera = nullptr;
    godot::Button* bracket = nullptr;
    godot::TextureRect* bracketUi = nullptr;
    godot::Node3D* shotgunParent = nullptr;
    godot::StaticBody3D* shotgunBody = nullptr;
    scripts::MouseRaycast* ray = nullptr;
    scripts::ButtonClass_Main* button = nullptr;
    scripts::InteractionBranch* branch = nullptr;
    scripts::CursorManager* cursor = nullptr;
    scripts::ControllerManager* controller = nullptr;
    scripts::InteractionManager* interaction = nullptr;
    scripts::ShotgunShooting* shotgun = nullptr;
    scripts::PickupIndicator* indicator = nullptr;
    scripts::DescriptionManager* description = nullptr;
    std::vector<godot::Label*> descriptionLabels;

    void SetUp() override {
        auto& map = godot::InputMap::get_singleton();
        map.clear();
        const std::pair<const char*, int64_t> nav[] = {{"ui_up", godot::key::UP}, {"ui_down", godot::key::DOWN},
                                                       {"ui_left", godot::key::LEFT}, {"ui_right", godot::key::RIGHT}};
        for (const auto& [name, code] : nav) {
            map.add_action(name);
            godot::InputEventRef bound = KeyEv(code, false);   // device 0, as project.godot pins them
            map.action_add_event(name, bound);
        }
        map.add_action("ui_accept");
        godot::InputEventRef accept = KeyEv(godot::key::ENTER, false);
        accept->device = godot::InputMap::ALL_DEVICES;
        map.action_add_event("ui_accept", accept);
        for (const char* unbound : {"ui_focus_next", "ui_focus_prev"}) map.add_action(unbound);

        godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);

        camera = static_cast<godot::Camera3D*>(t.add_node(std::make_unique<godot::Camera3D>("Camera"), root));
        camera->set_current(true);
        camera->set_fov(65.5f);
        godot::Node* dialogueUi = t.add_node(std::make_unique<godot::Node>("dialogue UI"), camera);
        bracket = static_cast<godot::Button*>(t.add_node(std::make_unique<godot::Button>("m button_shotgun"), dialogueUi));
        bracket->set_global_rect({433, 125}, {64, 56});
        bracket->set_mouse_filter(godot::Control::MOUSE_FILTER_IGNORE);   // as the scene's brackets
        godot::Node* buttonNode = t.add_node(std::make_unique<godot::Node>("m class_shotgun"), bracket);
        bracketUi = static_cast<godot::TextureRect*>(
            t.add_node(std::make_unique<godot::TextureRect>("TextureRect"), bracket));
        bracketUi->set_modulate({1, 1, 1, 0});

        shotgunParent = static_cast<godot::Node3D*>(
            t.add_node(std::make_unique<godot::Node3D>("shotgun parent new"), root));
        shotgunParent->set_position({0, 0, -5});
        shotgunBody = static_cast<godot::StaticBody3D*>(
            t.add_node(std::make_unique<godot::StaticBody3D>("StaticBody3D"), shotgunParent));
        auto* shape = static_cast<godot::CollisionShape3D*>(
            t.add_node(std::make_unique<godot::CollisionShape3D>("CollisionShape3D"), shotgunBody));
        auto box = std::make_shared<godot::BoxShape3D>();
        box->half_extents = {1, 1, 1};
        shape->shape = box;
        godot::Node* branchNode = t.add_node(std::make_unique<godot::Node>("interaction branch"), shotgunParent);
        godot::Node* indicatorNode = t.add_node(std::make_unique<godot::Node>("pickup indicator"), shotgunParent);

        // PickupIndicator._ready looks these up by absolute path.
        godot::Node* speakers = t.add_node(std::make_unique<godot::Node>("speaker parent"), root);
        t.add_node(std::make_unique<godot::AudioStreamPlayer>("speaker_indicator check"), speakers);

        godot::Node* managers = t.add_node(std::make_unique<godot::Node>("standalone managers"), root);
        godot::Node* descriptionNode = t.add_node(std::make_unique<godot::Node>("description manager"), managers);
        godot::Node* itemManagerNode = t.add_node(std::make_unique<godot::Node>("item manager"), managers);
        for (const char* name : {"item name", "item name shadow", "item description", "item description shadow"}) {
            descriptionLabels.push_back(
                static_cast<godot::Label*>(t.add_node(std::make_unique<godot::Label>(name), dialogueUi)));
        }
        godot::Node* cursorNode = t.add_node(std::make_unique<godot::Node>("cursor manager"), managers);
        godot::Node* controllerNode = t.add_node(std::make_unique<godot::Node>("controller manager"), managers);
        godot::Node* interactionNode = t.add_node(std::make_unique<godot::Node>("interaction manager"), managers);
        godot::Node* shotgunNode = t.add_node(std::make_unique<godot::Node>("shotgun shooting"), managers);

        ray = Attach<scripts::MouseRaycast>(camera);
        button = Attach<scripts::ButtonClass_Main>(buttonNode);
        branch = Attach<scripts::InteractionBranch>(branchNode);
        cursor = Attach<scripts::CursorManager>(cursorNode);
        controller = Attach<scripts::ControllerManager>(controllerNode);
        interaction = Attach<scripts::InteractionManager>(interactionNode);
        shotgun = Attach<scripts::ShotgunShooting>(shotgunNode);
        indicator = Attach<scripts::PickupIndicator>(indicatorNode);
        description = Attach<scripts::DescriptionManager>(descriptionNode);
        Attach<scripts::ItemManager>(itemManagerNode);

        // Shoot's first line is ejectionManager_player.FadeOutShell(), which
        // only needs the fader's AnimationPlayer.
        auto* ejector = Attach<scripts::ShellEjectManager>(
            t.add_node(std::make_unique<godot::Node>("shell eject player"), managers));
        ejector->animator_fader = static_cast<godot::AnimationPlayer*>(
            t.add_node(std::make_unique<godot::AnimationPlayer>("animator_fader"), root));
        ejector->mat_live = godot::MaterialLibrary::get("res://test/eject live.tres", godot::SurfaceMaterial{});
        ejector->mat_blank = godot::MaterialLibrary::get("res://test/eject blank.tres", godot::SurfaceMaterial{});
        // _ready sets surface 0 to the brass material, so it needs a mesh.
        ejector->mesh = static_cast<godot::MeshInstance3D*>(
            t.add_node(std::make_unique<godot::MeshInstance3D>("shell mesh"), root));
        shotgun->ejectionManager_player = ejector;

        indicator->obj = shotgunParent;
        indicator->collider = shotgunParent;
        indicator->dur = 0.05;
        indicator->axisOffset = 0.4;
        indicator->originalAxis = 0.0;
        indicator->activeAxis = "x";
        indicator->itemName = "SHOTGUN";
        indicator->itemDescription = "the gun";
        description->interaction = interaction;
        description->uiArray = descriptionLabels;
        description->dur = 0.1;

        // The scene's exported values for these nodes (runtime_main.json).
        ray->cursor = nullptr;
        branch->interactionAlias = "shotgun";
        branch->interactionAllowed = true;
        button->usingInteractionPipe = true;
        button->interaction = interaction;
        button->isActive = true;
        button->isDynamic = true;
        button->ui = bracketUi;
        button->ui_opacity_inactive = 0.0;
        button->ui_opacity_active = 1.0;
        button->overridingMouseRaycast = true;
        button->mouseRaycast = ray;
        button->mouseRaycastVector = {480, 270};   // the viewport centre: straight ahead
        controller->dynamicallySwappingDevice = true;
        controller->cursor = cursor;
        controller->mouseRaycast = ray;
        interaction->cursor = cursor;
        interaction->mouseRay = ray;
        interaction->shotgun = shotgun;

        AddMore();
        t.ready_scripts();
        scripts::SkipIntro(t);   // start at the table, as the parity scenarios do

        // RoundManager.BeginPlayerTurn -> cursor.SetCursor(true, true); SetupDeskUI
        // -> controller.previousFocus = deskUI_shotgun.
        cursor->cursor_visible = true;
        controller->previousFocus = bracket;

        godot::Input& input = godot::Input::get_singleton();
        input.reset();
        input.set_dispatch([this](const godot::InputEventRef& e) { t.push_input(e); });
    }

    // A derived fixture adds its nodes and scripts here, before _ready runs.
    virtual void AddMore() {}

    void Push(const godot::InputEventRef& e) {
        godot::Input::get_singleton().parse_input_event(e);
        godot::Input::get_singleton().flush_buffered_events();
    }
    void Frame() { t.process_frame(1.0 / 60.0); }
};

}  // namespace

// Nothing has focus on the player's turn; the first navigation key switches to
// the controller and focuses previousFocus, which aims the raycast at the
// bracket's point; the ray finds the shotgun branch.
TEST_F(InteractionTree, FirstNavKeyFocusesTheBracketAndAimsTheRay) {
    EXPECT_FALSE(cursor->controller_active);
    EXPECT_EQ(t.gui_get_focus_owner(), nullptr);

    Push(KeyEv(godot::key::DOWN, true));
    EXPECT_TRUE(cursor->controller_active);
    EXPECT_EQ(t.gui_get_focus_owner(), bracket);
    EXPECT_FLOAT_EQ(bracketUi->get_modulate().a, 1.0f) << "ButtonClass_Main.SetUI(true)";
    EXPECT_TRUE(ray->controller_overriding);
    EXPECT_FLOAT_EQ(ray->mouse.x, 480.0f);

    Push(KeyEv(godot::key::DOWN, false));
    Frame();
    ASSERT_TRUE(ray->result.has_value());
    EXPECT_EQ(ray->result->collider, shotgunBody);
    EXPECT_EQ(interaction->activeParent, shotgunParent);
    ASSERT_NE(interaction->activeInteractionBranch, nullptr);
    EXPECT_EQ(interaction->activeInteractionBranch->interactionAlias, "shotgun");
}

// ControllerManager._input: a moving mouse (v2.2 ignores a zero velocity) hands
// control back to the pointer:
// focus released (the bracket fades), the override stopped, the mouse position
// taken by MouseRaycast.
TEST_F(InteractionTree, AMouseEventHandsControlBackToThePointer) {
    Push(KeyEv(godot::key::DOWN, true));
    Push(KeyEv(godot::key::DOWN, false));
    ASSERT_TRUE(cursor->controller_active);

    auto motion = std::make_shared<godot::InputEventMouseMotion>();
    motion->position = {10, 10};
    motion->global_position = {10, 10};
    motion->velocity = {120, 0};
    Push(motion);

    EXPECT_FALSE(cursor->controller_active);
    EXPECT_EQ(t.gui_get_focus_owner(), nullptr);
    EXPECT_FLOAT_EQ(bracketUi->get_modulate().a, 0.0f) << "OnExit -> SetUI(false)";
    EXPECT_FALSE(ray->controller_overriding);
    EXPECT_FLOAT_EQ(ray->mouse.x, 10.0f);
    EXPECT_FLOAT_EQ(ray->mouse.y, 10.0f);
}

// PickupIndicator + DescriptionManager: once the ray has found the gun, the
// indicator slides its object out along activeAxis over `dur`, and the item's
// name goes into the description labels, which fade in.
TEST_F(InteractionTree, HoveredShotgunSlidesOutOnItsAxis) {
    indicator->interactionAllowed = true;
    ASSERT_FLOAT_EQ(shotgunParent->get_position().x, 0.0f);
    EXPECT_FLOAT_EQ(descriptionLabels[0]->get_modulate().a, 0.0f) << "DescriptionManager._ready hides the text";

    Push(KeyEv(godot::key::DOWN, true));
    Push(KeyEv(godot::key::DOWN, false));

    float previous = 0.0f;
    for (int frame = 1; frame <= 4; ++frame) {
        Frame();
        const float x = shotgunParent->get_position().x;
        EXPECT_GE(x, previous) << "frame " << frame << ": the slide only moves outward";
        EXPECT_LT(x, 0.4f) << "frame " << frame << ": still inside dur";
        previous = x;
    }
    EXPECT_GT(previous, 0.0f) << "the gun has started to slide out";
    EXPECT_EQ(descriptionLabels[1]->get_text(), "SHOTGUN");
    EXPECT_EQ(descriptionLabels[2]->get_text(), "the gun");
    EXPECT_GT(descriptionLabels[0]->get_modulate().a, 0.0f) << "BeginLerp fades the description in";
}

// TextShaker jitters the dialogue box by up to a pixel and snaps it back,
// twice per `delay`. The scene has one, on "Camera/dialogue UI/main dialogue".
TEST(ScriptsInteraction, TextShakerAlternatesRandomOffsetAndOrigin) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node>("main"), nullptr);
    auto* text = static_cast<godot::Control*>(t.add_node(std::make_unique<godot::Control>("main dialogue"), root));
    text->set_position_at_load({10, 20});
    auto* shaker = Attach<scripts::TextShaker>(t.add_node(std::make_unique<godot::Node>("text shaker"), text));
    shaker->text = text;
    shaker->delay = 0.03;
    shaker->offset1 = 0.0;
    shaker->offset2 = 1.0;

    // The same draws the script will make, from the same seed.
    godot::seed(5);
    const float expectedX = static_cast<float>(godot::randf_range(0.0, 1.0));
    const float expectedY = static_cast<float>(godot::randf_range(0.0, 1.0));

    godot::seed(5);
    t.ready_scripts();
    scripts::SkipIntro(t);   // start at the table, as the parity scenarios do
    t.process_frame(1.0 / 60.0);
    EXPECT_FLOAT_EQ(text->get_position().x, expectedX);
    EXPECT_FLOAT_EQ(text->get_position().y, expectedY);

    // The 0.03 s timer counts the frame it was created in (Godot's timer
    // rule), so at 60 fps each leg of the loop is two frames of clock and
    // lands here.
    t.process_frame(1.0 / 60.0);
    EXPECT_FLOAT_EQ(text->get_position().x, 10.0f) << "back to origpos after `delay`";
    EXPECT_FLOAT_EQ(text->get_position().y, 20.0f);

    // The timer for the second leg was created while timers were firing, so it
    // does not count that frame (Godot's timer rule) and the next offset lands
    // one frame later than the first did.
    t.process_frame(1.0 / 60.0);
    EXPECT_FLOAT_EQ(text->get_position().x, 10.0f) << "still at origpos";
    t.process_frame(1.0 / 60.0);
    EXPECT_NE(text->get_position().x, 10.0f) << "and off again: the shake alternates";
}

// BlinkerBranch flashes its parent's surface 0 between two materials. 112 of
// them blink in the room.
TEST(ScriptsInteraction, BlinkerSwapsSurfaceZeroBetweenItsMaterials) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node>("main"), nullptr);
    auto* mesh = static_cast<godot::MeshInstance3D*>(
        t.add_node(std::make_unique<godot::MeshInstance3D>("blinker"), root));
    godot::SurfaceMaterial own;
    own.albedo = {1, 1, 1, 1};
    own.albedo_at_load = own.albedo;
    mesh->add_surface_material(own);

    auto* blinker = Attach<scripts::BlinkerBranch>(t.add_node(std::make_unique<godot::Node>("BlinkerBranch"), mesh));
    blinker->mat_1.albedo = {1, 0, 0, 1};
    blinker->mat_2.albedo = {0, 0, 0, 1};
    blinker->delay_range1 = 0.05;
    blinker->delay_range2 = 0.1;

    godot::seed(1);
    const double delay = godot::randf_range(0.05, 0.1);
    godot::seed(1);
    t.ready_scripts();
    scripts::SkipIntro(t);   // start at the table, as the parity scenarios do
    t.process_frame(1.0 / 60.0);
    EXPECT_FLOAT_EQ(mesh->get_active_surface_albedo(0).r, 1.0f) << "mat_1 first";
    EXPECT_FLOAT_EQ(mesh->get_active_surface_albedo(0).g, 0.0f);

    // Past the drawn delay it is showing mat_2.
    for (int i = 0; i < static_cast<int>(delay * 60.0) + 2; ++i) t.process_frame(1.0 / 60.0);
    EXPECT_FLOAT_EQ(mesh->get_active_surface_albedo(0).r, 0.0f) << "then mat_2";
}

namespace {

// The shooting choice, built the way main.tscn has it:
// - "ui parent_shooting decision" holds the Label3D texts DEALER and YOU, each
//   with a StaticBody3D;
// - the two choice brackets aim the ray at them (473,109 / 473,426);
// - DecisionTextManager enables those colliders and plays "show text".
// The boxes sit where those viewport points cast from the fixture's camera
// (fov 65.5, 960x540): y = +/-1.9 at z = -5.
struct ChoiceTree : InteractionTree {
    assets::AnimationPlayerData textAnimations;
    scripts::RoundClass round;
    godot::Control* choiceParent = nullptr;
    godot::Button* dealerButton = nullptr;
    godot::Button* youButton = nullptr;
    godot::Label3D* textDealer = nullptr;
    godot::Label3D* textYou = nullptr;
    godot::StaticBody3D* dealerBody = nullptr;
    godot::StaticBody3D* youBody = nullptr;
    godot::AnimationPlayer* textAnimator = nullptr;
    scripts::DecisionTextManager* decision = nullptr;
    scripts::RoundManager* roundManager = nullptr;

    godot::StaticBody3D* AddTextBody(godot::Node* label, float y) {
        auto* body = static_cast<godot::StaticBody3D*>(
            t.add_node(std::make_unique<godot::StaticBody3D>("StaticBody3D"), label));
        body->set_position({0, y, -5});
        auto* shape = static_cast<godot::CollisionShape3D*>(
            t.add_node(std::make_unique<godot::CollisionShape3D>("CollisionShape3D"), body));
        auto box = std::make_shared<godot::BoxShape3D>();
        box->half_extents = {2.0f, 0.45f, 0.2f};
        shape->shape = box;
        return body;
    }

    godot::Button* AddChoiceButton(const char* name, godot::Vector2 aim) {
        auto* button = static_cast<godot::Button*>(t.add_node(std::make_unique<godot::Button>(name), choiceParent));
        button->set_mouse_filter(godot::Control::MOUSE_FILTER_IGNORE);
        godot::Node* classNode = t.add_node(std::make_unique<godot::Node>("m class"), button);
        auto* ui = static_cast<godot::TextureRect*>(t.add_node(std::make_unique<godot::TextureRect>("TextureRect"), button));
        auto* main = Attach<scripts::ButtonClass_Main>(classNode);
        main->usingInteractionPipe = true;
        main->interaction = interaction;
        main->isActive = true;
        main->isDynamic = true;
        main->ui = ui;
        main->ui_opacity_inactive = 0.0;   // as the scene: the choice is the 3D text, not a bracket
        main->ui_opacity_active = 0.0;
        main->overridingMouseRaycast = true;
        main->mouseRaycast = ray;
        main->mouseRaycastVector = aim;
        return button;
    }

    void AddMore() override {
        godot::Node* root = camera->get_parent();
        godot::Node* dialogueUi = bracket->get_parent();
        godot::Node* managers = t.get_node("standalone managers");

        // RoundManager: _ready plays two dealer animations; GrabShotgun's
        // ClearDeskUI and PermissionManager read the rest.
        godot::Node* roundNode = t.add_node(std::make_unique<godot::Node>("round manager"), managers);
        auto* hands = static_cast<godot::AnimationPlayer*>(
            t.add_node(std::make_unique<godot::AnimationPlayer>("animator_dealerHands"), roundNode));
        auto* dealerAnimator = static_cast<godot::AnimationPlayer*>(
            t.add_node(std::make_unique<godot::AnimationPlayer>("animator_dealer"), roundNode));
        roundManager = Attach<scripts::RoundManager>(roundNode);
        roundManager->animator_dealerHands = hands;
        roundManager->animator_dealer = dealerAnimator;
        roundManager->deskUI_parent = bracket;
        roundManager->deskUI_shotgun = bracket;
        roundManager->controller = controller;
        round.usingItems = false;
        roundManager->roundArray = {&round};
        roundManager->currentRound = 0;

        auto* perm = Attach<scripts::PermissionManager>(
            t.add_node(std::make_unique<godot::Node>("permission manager"), managers));
        perm->roundManager = roundManager;
        perm->itemManager = godot::script_cast<scripts::ItemManager>(t.get_node("standalone managers/item manager"));
        perm->indicatorArray = {indicator};
        perm->interactionBranchArray = {branch};
        perm->description = description;

        auto* shaker = Attach<scripts::ObjectShaker>(
            t.add_node(std::make_unique<godot::Node>("obj shaker branch"), shotgunParent));
        shaker->obj = shotgunParent;
        shaker->delay = 0.05;
        shaker->offset_1 = 0.0;
        shaker->offset_2 = 0.0;

        godot::Node* decisionParent =
            t.add_node(std::make_unique<godot::Node3D>("ui parent_shooting decision"), root);
        textDealer = static_cast<godot::Label3D*>(t.add_node(std::make_unique<godot::Label3D>("text dealer"), decisionParent));
        textYou = static_cast<godot::Label3D*>(t.add_node(std::make_unique<godot::Label3D>("text you"), decisionParent));
        for (godot::Label3D* label : {textDealer, textYou}) {
            auto* text = Attach<scripts::TextInteraction>(t.add_node(std::make_unique<godot::Node>("ui branch"), label));
            text->text = label;
            text->interaction = interaction;
            text->dur = 0.1;
            text->colorvalue_min = 0.4;
            text->colorvalue_max = 1.0;
        }
        dealerBody = AddTextBody(textDealer, 1.9f);
        youBody = AddTextBody(textYou, -1.9f);
        auto* dealerBranch = Attach<scripts::InteractionBranch>(dealerBody);   // the scene has it on the body
        dealerBranch->interactionAlias = "text dealer";
        dealerBranch->interactionAllowed = true;
        auto* youBranch = Attach<scripts::InteractionBranch>(
            t.add_node(std::make_unique<godot::Node>("interaction branch"), textYou));
        youBranch->interactionAlias = "text you";
        youBranch->interactionAllowed = true;

        textAnimations.animations.resize(2);
        textAnimations.animations[0].name = "show text";
        textAnimations.animations[0].length = 0.5f;
        textAnimations.animations[1].name = "hide text";
        textAnimations.animations[1].length = 0.5f;
        textAnimator = static_cast<godot::AnimationPlayer*>(
            t.add_node(std::make_unique<godot::AnimationPlayer>("animator_shooting text"), decisionParent));
        textAnimator->set_animations(&textAnimations);

        godot::Node* bracketUi = t.add_node(std::make_unique<godot::Node>("bracket ui"), dialogueUi);
        choiceParent = static_cast<godot::Control*>(t.add_node(std::make_unique<godot::Control>("shooting choice"), bracketUi));
        choiceParent->set_visible(false);
        dealerButton = AddChoiceButton("m button_choice dealer", {473, 109});
        youButton = AddChoiceButton("m button_choice you", {473, 426});
        youButton->set_focus_neighbor(godot::SIDE_TOP, "../m button_choice dealer");
        dealerButton->set_focus_neighbor(godot::SIDE_BOTTOM, "../m button_choice you");

        decision = Attach<scripts::DecisionTextManager>(
            t.add_node(std::make_unique<godot::Node>("shooting text manager"), decisionParent));
        decision->colliderArray = {dealerBody, youBody};
        decision->animator = textAnimator;
        decision->uiParent = choiceParent;
        decision->inter = interaction;   // v2.2: SetUI re-arms the shoot choice
        interaction->decision = decision;

        auto* shotgunAnimator = static_cast<godot::AnimationPlayer*>(
            t.add_node(std::make_unique<godot::AnimationPlayer>("animator_shotgun"), root));
        shotgun->roundManager = roundManager;
        shotgun->perm = perm;
        shotgun->collider_shotgun = shotgunBody;
        shotgun->animator_shotgun = shotgunAnimator;
        shotgun->shotgunshaker = shaker;
        shotgun->decisionText = decision;
        shotgun->btnParent_shootingChoice = choiceParent;
        shotgun->btn_you = youButton;
        shotgun->controller = controller;
        shotgun->cursorManager = cursor;
    }

    // Down focuses the gun's bracket; accept picks the gun up.
    void GrabTheGun() {
        Push(KeyEv(godot::key::DOWN, true));
        Push(KeyEv(godot::key::DOWN, false));
        Frame();
        Push(KeyEv(godot::key::ENTER, true));
        Push(KeyEv(godot::key::ENTER, false));
        Frame();
    }
};

}  // namespace

// ShotgunShooting.GrabShotgun: the desk UI clears, the gun's permissions and
// collider go off, the texts' colliders come on with "show text", the choice
// brackets show, and focus goes to YOU.
TEST_F(ChoiceTree, GrabShotgunShowsTheChoiceAndFocusesYou) {
    EXPECT_EQ(dealerBody->collision_layer, 0u) << "DecisionTextManager._ready";

    GrabTheGun();

    EXPECT_EQ(interaction->port_interactions, (std::vector<std::string>{"", "shotgun"}));
    EXPECT_TRUE(choiceParent->is_visible());
    EXPECT_EQ(t.gui_get_focus_owner(), youButton);
    EXPECT_EQ(controller->previousFocus, youButton);
    EXPECT_EQ(dealerBody->collision_layer, 1u);
    EXPECT_EQ(youBody->collision_layer, 1u);
    EXPECT_EQ(textAnimator->get_current_animation(), "show text");
    EXPECT_FALSE(branch->interactionAllowed) << "PermissionManager.SetInteractionPermissions(false)";
    EXPECT_EQ(shotgunBody->collision_layer, 0u) << "ShotgunCollider(false)";
    EXPECT_FALSE(shotgun->ejectionManager_player->hasFaded);
}

// Up moves to DEALER, the ray aims at its text, which TextInteraction
// brightens while YOU dims. Accept interacts with "text dealer": Shoot("dealer")
// runs, and DecisionTextManager.SetUI(false) hides the choice with "hide text".
TEST_F(ChoiceTree, ChoosingDealerCallsShootThenHidesTheChoice) {
    GrabTheGun();
    Push(KeyEv(godot::key::UP, true));
    Push(KeyEv(godot::key::UP, false));
    EXPECT_EQ(t.gui_get_focus_owner(), dealerButton);
    for (int i = 0; i < 12; ++i) Frame();
    ASSERT_NE(interaction->activeInteractionBranch, nullptr);
    EXPECT_EQ(interaction->activeInteractionBranch->interactionAlias, "text dealer");
    EXPECT_FLOAT_EQ(textDealer->get_modulate().r, 1.0f) << "TextInteraction brightens the focused choice";
    EXPECT_FLOAT_EQ(textYou->get_modulate().r, 0.4f) << "and dims the other";

    Push(KeyEv(godot::key::ENTER, true));
    Push(KeyEv(godot::key::ENTER, false));
    Frame();
    EXPECT_EQ(interaction->port_interactions, (std::vector<std::string>{"", "shotgun", "", "text dealer"}));
    // Shoot's first line is ejectionManager_player.FadeOutShell(), so the
    // observable is what Shoot itself does.
    EXPECT_TRUE(shotgun->ejectionManager_player->hasFaded)
        << "choosing DEALER runs ShotgunShooting.Shoot";
    EXPECT_FALSE(choiceParent->is_visible());
    EXPECT_EQ(t.gui_get_focus_owner(), nullptr)
        << "hiding the choice releases the focused bracket (viewport.cpp _gui_hide_control)";
    EXPECT_EQ(textAnimator->get_current_animation(), "hide text");
    EXPECT_EQ(dealerBody->collision_layer, 0u);
}

// Interacting with the gun runs the real GrabShotgun, which needs the whole
// choice tree, so these two live on ChoiceTree.

// Accept presses the focused bracket on release; OnPress runs
// InteractWith(alias "") (matches nothing) and then MainInteractionEvent, which
// interacts with the branch the ray found.
TEST_F(ChoiceTree, AcceptReleaseInteractsWithTheShotgun) {
    Push(KeyEv(godot::key::DOWN, true));
    Push(KeyEv(godot::key::DOWN, false));
    Frame();
    ASSERT_NE(interaction->activeInteractionBranch, nullptr);

    Push(KeyEv(godot::key::ENTER, true));
    EXPECT_TRUE(interaction->port_interactions.empty()) << "BaseButton presses on release";
    Push(KeyEv(godot::key::ENTER, false));
    EXPECT_EQ(interaction->port_interactions, (std::vector<std::string>{"", "shotgun"}));
}

// The mouse path itself: pointing at the gun and clicking interacts with it --
// InteractionManager._input acts on the branch the previous frame's ray found.
TEST_F(ChoiceTree, PointingAndClickingInteractsWithTheShotgun) {
    auto motion = std::make_shared<godot::InputEventMouseMotion>();
    motion->position = {480, 270};
    motion->global_position = {480, 270};
    Push(motion);
    Frame();
    ASSERT_NE(interaction->activeInteractionBranch, nullptr);

    auto click = std::make_shared<godot::InputEventMouseButton>();
    click->button_index = godot::mouse::LEFT;
    click->pressed = true;
    click->position = {480, 270};
    click->global_position = {480, 270};
    Push(click);
    EXPECT_EQ(interaction->port_interactions, (std::vector<std::string>{"shotgun"}));
}

namespace {

// v2.2's double-press guard: one shot per decision (InteractWith sets fs_dec
// when it shoots; DecisionTextManager.SetUI re-arms it). A real single shot is
// shoot_dealer_b1r1's.
TEST_F(InteractionTree, ASecondShootPressDoesNothingUntilTheChoiceIsShownAgain) {
    auto* decisionNode = t.add_node(std::make_unique<godot::Node>("decision text manager"), t.get_root());
    auto* decision = Attach<scripts::DecisionTextManager>(decisionNode);
    decision->animator = static_cast<godot::AnimationPlayer*>(
        t.add_node(std::make_unique<godot::AnimationPlayer>("animator_decision"), t.get_root()));
    decision->uiParent = static_cast<godot::Control*>(
        t.add_node(std::make_unique<godot::Control>("decision ui"), t.get_root()));
    decision->inter = interaction;
    interaction->decision = decision;
    decision->uiParent->set_visible(true);

    interaction->fs_dec = true;   // the first press already shot
    const std::size_t before = t.scheduler().LiveTaskCount();
    interaction->InteractWith("text dealer");
    interaction->InteractWith("text you");
    EXPECT_EQ(t.scheduler().LiveTaskCount(), before) << "no second shot";
    EXPECT_TRUE(decision->uiParent->is_visible()) << "SetUI(false) is inside the guard";

    decision->SetUI(true);
    EXPECT_FALSE(interaction->fs_dec);
    interaction->fs_dec = true;
    decision->SetUI(false);
    EXPECT_FALSE(interaction->fs_dec);
}

}  // namespace
