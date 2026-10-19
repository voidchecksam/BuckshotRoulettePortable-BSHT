#include "scripts/DecisionTextManager.h"

#include "scripts/GlobalVariables.h"
#include "scripts/InteractionManager.h"

namespace scripts {

// gd: L10-L14
void DecisionTextManager::_ready() {
    if (GlobalVariables::using_gl) {
        for (godot::Node3D* i : array_obj) i->set_visible(false);
    }
    for (godot::StaticBody3D* c : colliderArray) {
        c->collision_layer = 0;
        c->collision_mask = 0;
    }
}

// gd: L16-L31
void DecisionTextManager::SetUI(bool state) {
    if (state) {
        for (godot::StaticBody3D* c : colliderArray) {
            c->collision_layer = 1;
            c->collision_mask = 1;
        }
        animator->play("show text");
        if (GlobalVariables::using_gl) {
            for (godot::Node3D* i : array_obj) i->set_visible(true);
        }
        inter->fs_dec = false;
    } else {
        for (godot::StaticBody3D* c : colliderArray) {
            c->collision_layer = 0;
            c->collision_mask = 0;
        }
        animator->play("hide text");
        uiParent->set_visible(false);
        if (GlobalVariables::using_gl) {
            for (godot::Node3D* i : array_obj) i->set_visible(false);
        }
        inter->fs_dec = false;
    }
}

}  // namespace scripts
