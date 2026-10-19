// The item test fixtures: a small tree wired the way main.tscn wires the item
// managers. Shared by test_scripts_items.cpp (dealing) and
// test_scripts_item_use.cpp (use). Header-only; test code, so the helpers are
// inline in a named namespace rather than duplicated per file.
#pragma once

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/node.h"
#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "scripts/CameraManager.h"
#include "scripts/CompartmentManager.h"
#include "scripts/ControllerManager.h"
#include "scripts/CursorManager.h"
#include "scripts/DialogueManager.h"
#include "scripts/InteractionBranch.h"
#include "scripts/ItemAmounts.h"
#include "scripts/ItemGridIndicator.h"
#include "scripts/ItemManager.h"
#include "scripts/gridBranch.h"
#include "scripts/PickupIndicator.h"
#include "scripts/item_scenes.h"
#include "godot/packed_scene.h"
#include "godot/physics.h"
#include "godot/runtime_scene_loader.h"
#include <cstdio>
#include "scripts/RoundManager.h"
#include "scripts/resources.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace item_fixtures {


template <class T>
inline T* Attach(godot::Node* node) {
    auto script = std::make_unique<T>();
    T* raw = script.get();
    node->set_script(std::move(script));
    return raw;
}

template <class N>
inline N* Add(godot::SceneTree& t, const char* name, godot::Node* parent) {
    return static_cast<N*>(t.add_node(std::make_unique<N>(name), parent));
}

struct ItemTree : ::testing::Test {
    godot::SceneTree t;
    scripts::ItemManager* items = nullptr;
    scripts::RoundManager* roundManager = nullptr;
    scripts::CompartmentManager* comp = nullptr;
    scripts::Dialogue* dialogue = nullptr;
    scripts::InteractionBranch* intake = nullptr;
    scripts::PlayerData playerData;
    std::vector<scripts::RoundClass> rounds{1};

    void SetUp() override {
        godot::seed(20260921);
        godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
        godot::Node* managers = Add<godot::Node>(t, "standalone managers", root);

        auto* camera = Attach<scripts::CameraManager>(Add<godot::Node>(t, "camera manager", managers));
        camera->cam = Add<godot::Camera3D>(t, "Camera", root);
        camera->speaker_pan = Add<godot::AudioStreamPlayer>(t, "speaker_pan", root);

        auto* cursor = Attach<scripts::CursorManager>(Add<godot::Node>(t, "cursor manager", managers));
        cursor->speaker = Add<godot::AudioStreamPlayer>(t, "speaker_cursor", root);

        auto* controller =
            Attach<scripts::ControllerManager>(Add<godot::Node>(t, "controller manager", managers));

        dialogue = Attach<scripts::Dialogue>(Add<godot::Node>(t, "dialogue", managers));
        dialogue->dialogueUI = Add<godot::Label>(t, "dialogue text", root);
        dialogue->dialogueUI_backdrop = Add<godot::ColorRect>(t, "dialogue backdrop", root);
        dialogue->speaker_click = Add<godot::AudioStreamPlayer>(t, "speaker_click", root);

        comp = Attach<scripts::CompartmentManager>(Add<godot::Node>(t, "compartment manager", managers));
        comp->animator_compartment = Add<godot::AnimationPlayer>(t, "animator_compartment", root);
        comp->animator_briefcase = Add<godot::AnimationPlayer>(t, "animator_briefcase", root);

        roundManager = Attach<scripts::RoundManager>(Add<godot::Node>(t, "round manager", managers));
        rounds[0].usingItems = true;
        rounds[0].numberOfItemsToGrab = 2;
        roundManager->playerData = &playerData;
        for (auto& r : rounds) roundManager->roundArray.push_back(&r);
        roundManager->currentRound = 0;

        intake = Attach<scripts::InteractionBranch>(Add<godot::Node>(t, "briefcase intake", root));

        items = Attach<scripts::ItemManager>(Add<godot::Node>(t, "item manager", managers));
        items->roundManager = roundManager;
        items->camera = camera;
        items->cursor = cursor;
        items->controller = controller;
        items->dialogue = dialogue;
        items->comp = comp;
        items->grid = Attach<scripts::GridIndicator>(Add<godot::Node>(t, "item grid indicator", managers));
        items->grid->roundManager = roundManager;
        items->grid->parent = Add<godot::Node3D>(t, "item grid indicator parent", root);
        items->grid->animator = Add<godot::AnimationPlayer>(t, "animator_grid indicator", items->grid->parent);
        items->interaction_intake = intake;
        items->itemSpawnParent = Add<godot::Node3D>(t, "item spawn parent", root);
        items->btnParent_intake = Add<godot::Control>(t, "intake parent", root);
        items->btn_intake = Add<godot::Control>(t, "intake button", root);
        // The .gd's stringNumberArray is indexed by numberOfItemsToGrab. Its
        // contents do not matter: the lookup is dead.
        items->stringNumberArray = {"zero", "one", "two", "three", "four"};
        roundManager->itemManager = items;
    }

    void RunFor(double seconds) {
        const double step = 1.0 / 60.0;
        for (double s = 0.0; s < seconds; s += step) t.process_frame(step);
    }
};



inline std::string Extracted(const char* file) {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/" + file;
}

inline std::string ItemExport(const char* stem) {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/runtime_item_" + stem + ".json";
}

struct ItemDrawTree : ItemTree {
    scripts::Amounts* amounts = nullptr;
    std::vector<scripts::ItemResource> resources{2};
    std::vector<scripts::AmountResource> counts{2};
    bool scenesLoaded = false;

    void SetUp() override {
        ItemTree::SetUp();
        scripts::RegisterScripts();
        godot::PackedScene* saw = godot::PackedScene::load(
            "res://instances/item_handsaw.tscn", ItemExport("handsaw").c_str());
        godot::PackedScene* inv = godot::PackedScene::load(
            "res://instances/item_inverter.tscn", ItemExport("inverter").c_str());
        scenesLoaded = saw && inv;
        if (!scenesLoaded) return;

        resources[0].itemName = "handsaw";
        resources[0].instance = saw;
        resources[0].pos_inBriefcase = {1.0f, 0.0f, 0.0f};
        resources[0].pos_inHand = {5.0f, 2.0f, 0.0f};
        resources[1].itemName = "inverter";
        resources[1].instance = inv;
        resources[1].pos_inBriefcase = {-1.0f, 0.0f, 0.0f};
        resources[1].pos_inHand = {-5.0f, 2.0f, 0.0f};
        for (auto& r : resources) items->instanceArray.push_back(&r);

        counts[0].itemName = "handsaw";
        counts[0].amount_active = 1;
        counts[1].itemName = "inverter";
        counts[1].amount_active = 1;
        amounts = Attach<scripts::Amounts>(Add<godot::Node>(t, "item amounts", t.get_root()->get_child(0)));
        for (auto& c : counts) amounts->array_amounts.push_back(&c);
        items->amounts = amounts;

        items->speaker_itemgrab = Add<godot::AudioStreamPlayer>(t, "speaker_itemgrab", t.get_root()->get_child(0));
        items->soundArray_itemGrab = {"item grab1.ogg", "item grab2.ogg"};
        items->lerpDuration = 0.45;   // the scene's value (runtime_main.json)
    }

    scripts::AmountResource* countFor(const std::string& name) {
        for (auto& c : counts) if (c.itemName == name) return &c;
        return nullptr;
    }
    const scripts::ItemResource* resourceFor(const std::string& name) {
        for (auto& r : resources) if (r.itemName == name) return &r;
        return nullptr;
    }
    // Spawn the draw and run one frame, which reaches GrabItem's first await:
    // everything up to the lerp has happened by then.
    void Grab() {
        // What NOTIFICATION_READY does for a script whose has_process() is true
        // (scene_tree.cpp:145). Not ready_scripts(): that fires _ready on every
        // manager in the fixture, as the shooting tests warn. The contract
        // itself is pinned by ItemManagerAsksForAProcessEveryFrame below.
        items->get_node_owner()->set_process(items->has_process());
        t.spawn(items->GrabItem());
        RunFor(1.0 / 60.0);
    }
};



struct ItemTableTree : ItemDrawTree {
    std::vector<scripts::InteractionBranch*> playerSlots;

    void SetUp() override {
        ItemDrawTree::SetUp();
        if (!scenesLoaded) return;
        godot::Node* root = t.get_root()->get_child(0);
        for (int i = 0; i < 4; ++i) {
            auto* slot = Add<godot::Node3D>(t, ("grid" + std::to_string(i)).c_str(), root);
            slot->set_position({static_cast<float>(i), 0.0f, -2.0f});
            playerSlots.push_back(Attach<scripts::InteractionBranch>(
                Add<godot::Node>(t, "branch", slot)));
            auto* body = Add<godot::StaticBody3D>(t, "body", slot);
            Add<godot::CollisionShape3D>(t, "shape", body);
            items->gridParentArray.push_back(slot);
            items->gridOccupiedArray.push_back(false);
        }
        for (int i = 0; i < 4; ++i) {
            auto* slot = Add<godot::Node3D>(t, ("dealer grid" + std::to_string(i)).c_str(), root);
            slot->set_position({static_cast<float>(i), 0.0f, 2.0f});
            Attach<scripts::gridBranch>(Add<godot::Node>(t, "grid branch", slot))->activeIndex = 10 + i;
            items->gridParentArray_enemy.push_back(slot);
        }
        items->ResetDealerGrid();
        items->instanceArray_dealer = items->instanceArray;
        items->speaker_placedown = Add<godot::AudioStreamPlayer>(t, "speaker_placedown", root);
        items->gridParent_1 = Add<godot::Node3D>(t, "gp1", root);
        items->gridParent_2 = Add<godot::Node3D>(t, "gp2", root);
        items->gridParent_3 = Add<godot::Node3D>(t, "gp3", root);
        items->gridParent_4 = Add<godot::Node3D>(t, "gp4", root);
    }

    // Take one item from the briefcase and let it reach the hand.
    void TakeOne() {
        Grab();
        RunFor(items->lerpDuration + 0.1);
    }
    void Place(int slot) {
        t.spawn(items->PlaceDownItem(slot));
        RunFor(1.0 / 60.0);
    }
};


}  // namespace item_fixtures
