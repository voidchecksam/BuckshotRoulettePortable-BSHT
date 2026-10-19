#include "scripts/DescriptionManager.h"

#include "scripts/InteractionManager.h"
#include "scripts/PickupIndicator.h"

#include <algorithm>

namespace scripts {

// gd: L14-L15
void DescriptionManager::_ready() {
    HideText();
}

// gd: L17-L19
void DescriptionManager::_process(double /*delta*/) {
    LerpText();
    CheckInteraction();
}

// gd: L21-L24
void DescriptionManager::HideText() {
    for (godot::Label* label : uiArray) {
        godot::Color m = label->get_modulate();
        m.a = 0.0f;
        label->set_modulate(m);
    }
}

// gd: L26-L36
void DescriptionManager::CheckInteraction() {
    if (interaction->activeParent != nullptr) {
        godot::Node* parent = interaction->activeParent;
        bool found = false;
        for (int i = 0; i < parent->get_child_count(); i++) {
            if (godot::script_cast<PickupIndicator>(parent->get_child(i))) {
                found = true;
                return;
            }
        }
        if (!found && started) {
            EndLerp();
            started = false;
        }
    }
}

// gd: L38-L44
void DescriptionManager::BeginLerp() {
    started = true;
    current_opacity = uiArray[0]->get_modulate().a;
    next_opacity = 1.0;
    elapsed = 0.0;
    moving = true;
}

// gd: L46-L51
void DescriptionManager::EndLerp() {
    current_opacity = uiArray[0]->get_modulate().a;
    next_opacity = 0.0;
    elapsed = 0.0;
    moving = true;
}

// gd: L53-L61
void DescriptionManager::LerpText() {
    if (moving) {
        elapsed += get_process_delta_time();
        const double c = std::clamp(elapsed / dur, 0.0, 1.0);
        const double opacity = current_opacity + (next_opacity - current_opacity) * c;
        for (godot::Label* label : uiArray) {
            godot::Color color = label->get_modulate();
            color.a = static_cast<float>(opacity);
            label->set_modulate(color);
        }
    }
}

}  // namespace scripts
