// Stealing with adrenaline on a controller: each bracket on the dealer's side
// aims MouseRaycast at a fixed point, and the item the ray finds is the one
// picked. A tall item in front of a slot (a beer, the handcuffs) must not
// take the pick from the item under the bracket.
#include "main_tree_fixture.h"
#include "godot/camera_projection.h"
#include "scripts/ButtonClass_Main.h"
#include "scripts/CameraManager.h"
#include "scripts/ItemManager.h"
#include "scripts/MouseRaycast.h"
#include "scripts/PickupIndicator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

namespace {

constexpr int kSlots = 8;
const godot::Vector3 kParked{0.0f, -1000.0f, 0.0f};

struct StealAim : ::testing::Test {
    testing_support::MainTree m;
    scripts::ItemManager* items = nullptr;
    scripts::MouseRaycast* ray = nullptr;
    godot::Camera3D* cam = nullptr;
    std::vector<scripts::ButtonClass_Main*> buttons;
    // pool[type][slot]: one instance of each dealer item per slot.
    std::vector<std::vector<godot::Node3D*>> pool;

    void SetUp() override {
        if (!m.ok) GTEST_SKIP() << "export the main scene first";
        items = godot::script_cast<scripts::ItemManager>(m.tree.get_node("standalone managers/item manager"));
        auto* camera = godot::script_cast<scripts::CameraManager>(m.tree.get_node("standalone managers/camera manager"));
        cam = godot::cast_to<godot::Camera3D>(m.tree.get_node("Camera"));
        ray = godot::script_cast<scripts::MouseRaycast>(cam);
        ASSERT_NE(items, nullptr);
        ASSERT_NE(camera, nullptr);
        ASSERT_NE(ray, nullptr);
        ASSERT_EQ(items->gridParentArray_enemy.size(), static_cast<std::size_t>(kSlots));

        bool socketFound = false;
        for (const scripts::CameraSocket* s : camera->socketArray) {
            if (s->socketName != "enemy items") continue;
            cam->set_fov(static_cast<float>(s->fov));
            cam->set_transform_origin(s->pos);
            cam->set_rotation_degrees(s->rot);
            socketFound = true;
        }
        ASSERT_TRUE(socketFound);

        godot::Node* stealing = m.tree.get_node("Camera/dialogue UI/bracket ui/item stealing");
        ASSERT_NE(stealing, nullptr);
        for (int i = 0; i < stealing->get_child_count(); ++i) {
            godot::Node* btn = stealing->get_child(i);
            for (int k = 0; k < btn->get_child_count(); ++k) {
                auto* main = godot::script_cast<scripts::ButtonClass_Main>(btn->get_child(k));
                if (main && main->overridingMouseRaycast) buttons.push_back(main);
            }
        }
        ASSERT_EQ(buttons.size(), static_cast<std::size_t>(kSlots));

        for (const scripts::ItemResource* res : items->instanceArray_dealer) {
            std::vector<godot::Node3D*> perSlot;
            for (int s = 0; s < kSlots; ++s) {
                auto* node = godot::cast_to<godot::Node3D>(items->itemSpawnParent->add_child(res->instance->instantiate()));
                node->set_transform_origin(kParked);
                perSlot.push_back(node);
            }
            pool.push_back(perSlot);
        }
        ASSERT_FALSE(pool.empty());
    }

    // ItemManager.GrabItems_Enemy's placement: the slot's origin and rotation plus the item's offsets.
    void Place(const std::vector<int>& typeBySlot) {
        for (auto& perSlot : pool) {
            for (godot::Node3D* n : perSlot) n->set_transform_origin(kParked);
        }
        for (int s = 0; s < kSlots; ++s) {
            const scripts::ItemResource* res = items->instanceArray_dealer[static_cast<std::size_t>(typeBySlot[s])];
            godot::Node3D* grid = items->gridParentArray_enemy[static_cast<std::size_t>(s)];
            godot::Node3D* n = pool[static_cast<std::size_t>(typeBySlot[s])][static_cast<std::size_t>(s)];
            const godot::Vector3 p = grid->get_position();
            const godot::Vector3 r = grid->get_rotation_degrees();
            n->set_transform_origin({p.x + res->pos_offset.x, p.y + res->pos_offset.y, p.z + res->pos_offset.z});
            n->set_rotation_degrees({r.x + res->rot_offset.x, r.y + res->rot_offset.y, r.z + res->rot_offset.z});
        }
        m.tree.physics_sync();
    }

    // The item the player sees under the bracket: the placed item whose origin lands nearest the aim.
    godot::Node3D* UnderBracket(const std::vector<int>& typeBySlot, godot::Vector2 aim) const {
        godot::Node3D* best = nullptr;
        float bestD = 1e9f;
        for (int s = 0; s < kSlots; ++s) {
            godot::Node3D* n = pool[static_cast<std::size_t>(typeBySlot[s])][static_cast<std::size_t>(s)];
            const godot::Vector2 p = godot::unproject_position(*cam, n->get_global_position(), godot::kViewportSize);
            const float d = std::hypot(p.x - aim.x, p.y - aim.y);
            if (d < bestD) {
                bestD = d;
                best = n;
            }
        }
        return best;
    }

    std::string Name(int type) const { return items->instanceArray_dealer[static_cast<std::size_t>(type)]->itemName; }

    // PickupIndicator.BeginPickup's end pose (hovered) or Revert's (at rest): obj moved along its axis.
    static void Lift(godot::Node3D* item, bool up) {
        auto* ind = godot::script_cast<scripts::PickupIndicator>(item->get_child(0));
        godot::Vector3 p = ind->obj->get_position();
        const float v = static_cast<float>(up ? ind->axisOffset : ind->originalAxis);
        if (ind->activeAxis == "x") p.x = v;
        if (ind->activeAxis == "y") p.y = v;
        if (ind->activeAxis == "z") p.z = v;
        ind->obj->set_position(p);
    }

    godot::Node* Aim(godot::Vector2 aim) {
        ray->GetRaycastOverride(aim);
        ray->get_selection();
        return (ray->result && ray->result->collider) ? ray->result->collider->get_parent() : nullptr;
    }

    static bool IsAdrenaline(godot::Node3D* item) {
        return godot::script_cast<scripts::PickupIndicator>(item->get_child(0))->itemName == "ADRENALINE";
    }

    // Every bracket picks the item under it, with the others at rest and with
    // each other item still raised from a hover; returns how many did not.
    int CheckLayout(const std::vector<int>& typeBySlot) {
        Place(typeBySlot);
        int wrong = 0;
        for (scripts::ButtonClass_Main* b : buttons) {
            godot::Node3D* expected = UnderBracket(typeBySlot, b->mouseRaycastVector);
            if (IsAdrenaline(expected)) continue;   // adrenaline cannot be stolen
            for (int lifted = -1; lifted < kSlots; ++lifted) {
                godot::Node3D* other = nullptr;
                if (lifted >= 0) {
                    other = pool[static_cast<std::size_t>(typeBySlot[lifted])][static_cast<std::size_t>(lifted)];
                    if (other == expected) continue;
                    Lift(other, true);
                    m.tree.physics_sync();
                }
                godot::Node* got = Aim(b->mouseRaycastVector);
                if (other) {
                    Lift(other, false);
                    m.tree.physics_sync();
                }
                if (got == expected) continue;
                ++wrong;
                ADD_FAILURE() << "aim (" << b->mouseRaycastVector.x << ", " << b->mouseRaycastVector.y << ") over "
                              << godot::script_cast<scripts::PickupIndicator>(expected->get_child(0))->itemName
                              << (other ? " with a raised neighbour" : "") << " picked "
                              << (got ? godot::script_cast<scripts::PickupIndicator>(got->get_child(0)) ?
                                        godot::script_cast<scripts::PickupIndicator>(got->get_child(0))->itemName :
                                        got->get_name() :
                                        std::string("nothing"));
            }
        }
        ray->StopRaycastOverride();
        return wrong;
    }
};

// The issue's layouts: every pair of dealer items in alternating slots, both ways round.
TEST_F(StealAim, EveryBracketPicksTheItemUnderItWhateverStandsInFront) {
    const int types = static_cast<int>(pool.size());
    int wrong = 0;
    for (int a = 0; a < types; ++a) {
        for (int b = 0; b < types; ++b) {
            std::vector<int> layout(kSlots);
            for (int s = 0; s < kSlots; ++s) layout[static_cast<std::size_t>(s)] = (s % 2 == 0) ? a : b;
            const int w = CheckLayout(layout);
            if (w) ADD_FAILURE() << Name(a) << " / " << Name(b) << ": " << w << " wrong";
            wrong += w;
        }
    }
    EXPECT_EQ(wrong, 0);
}

}  // namespace
