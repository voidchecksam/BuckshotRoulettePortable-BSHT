#include "scripts/PickupIndicator.h"

#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "scripts/DescriptionManager.h"
#include "scripts/InteractionManager.h"
#include "scripts/ItemManager.h"

#include <algorithm>

namespace scripts {

namespace {
// match(activeAxis): obj.transform.origin = Vector3(...) with one axis replaced.
void SetAxis(godot::Node3D* obj, const std::string& activeAxis, double value) {
    godot::Vector3 o = obj->get_position();
    if (activeAxis == "x") {
        o.x = static_cast<float>(value);
    } else if (activeAxis == "y") {
        o.y = static_cast<float>(value);
    } else if (activeAxis == "z") {
        o.z = static_cast<float>(value);
    } else {
        return;
    }
    obj->set_transform_origin(o);
}
}  // namespace

// gd: L38-L42
void PickupIndicator::_ready() {
    // PORT: absolute /root/main/ paths resolve from the scene root.
    godot::SceneTree* tree = get_tree();
    speaker_indicatorCheck = godot::cast_to<godot::AudioStreamPlayer>(tree->get_node("speaker parent/speaker_indicator check"));
    interaction = godot::script_cast<InteractionManager>(tree->get_node("standalone managers/interaction manager"));
    description = godot::script_cast<DescriptionManager>(tree->get_node("standalone managers/description manager"));
    itemManager = godot::script_cast<ItemManager>(tree->get_node("standalone managers/item manager"));
}

// gd: L44-L47
void PickupIndicator::_process(double /*delta*/) {
    if (lerpEnabled) LerpMovement();
    CheckIfActive();
}

// gd: L49-L61
void PickupIndicator::CheckIfActive() {
    if (interactionAllowed && !interactionInvalid) {
        if (interaction->activeParent != nullptr && interaction->activeParent == collider) {
            if (!fs) {
                BeginPickup();
                fs2 = false;
                fs = true;
            }
        }
        if (interaction->activeParent != collider) {
            if (!fs2) {
                Revert();
                fs = false;
                fs2 = true;
            }
        }
    }
}

// gd: L63-L76
void PickupIndicator::BeginPickup() {
    speaker_indicatorCheck->set_pitch_scale(static_cast<float>(godot::randf_range(0.9, 1.0)));
    speaker_indicatorCheck->play();
    current = originalAxis;
    next = axisOffset;
    description->uiArray[0]->set_text(itemName);
    description->uiArray[1]->set_text(itemName);
    description->uiArray[2]->set_text(itemDescription);
    description->uiArray[3]->set_text(itemDescription);
    elapsed = 0.0;
    moving = true;
    description->BeginLerp();
    itemManager->hasBegun = true;
}

// gd: L78-L91
void PickupIndicator::Revert() {
    // if (itemManager.hasBegun): description.EndLerp(), commented out in the .gd
    itemManager->hasBegun = false;
    const godot::Vector3 o = obj->get_position();
    if (activeAxis == "x") {
        current = o.x;
    } else if (activeAxis == "y") {
        current = o.y;
    } else if (activeAxis == "z") {
        current = o.z;
    }
    next = originalAxis;
    elapsed = 0.0;
    moving = true;
}

// gd: L93-L104
void PickupIndicator::LerpMovement() {
    if (moving) {
        elapsed += get_process_delta_time();
        const double c = std::clamp(elapsed / dur, 0.0, 1.0);
        const double pos = current + (next - current) * c;
        SetAxis(obj, activeAxis, pos);
    }
}

// gd: L106-L113
void PickupIndicator::SnapToMax() {
    SetAxis(obj, activeAxis, axisOffset);
}

// gd: L115-L122
void PickupIndicator::SnapToMin() {
    SetAxis(obj, activeAxis, originalAxis);
}

}  // namespace scripts
