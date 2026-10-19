#include "scripts/TextInteraction.h"

#include "scripts/InteractionManager.h"

#include <algorithm>

namespace scripts {

// gd: L16-L18
void TextInteraction::_process(double /*delta*/) {
    CheckStatus();
    LerpOpacity();
}

// gd: L20-L30
void TextInteraction::CheckStatus() {
    if (interaction->activeParent == text) {
        if (!fs1) {
            BeginLerp();
            fs2 = false;
            fs1 = true;
        }
    } else {
        if (!fs2) {
            EndLerp();
            fs1 = false;
            fs2 = true;
        }
    }
}

// gd: L32-L37
void TextInteraction::BeginLerp() {
    colorvalue_current = text->get_modulate().r;
    colorvalue_next = colorvalue_max;
    elapsed = 0.0;
    moving = true;
}

// gd: L39-L44
void TextInteraction::EndLerp() {
    colorvalue_current = text->get_modulate().r;
    colorvalue_next = colorvalue_min;
    elapsed = 0.0;
    moving = true;
}

// gd: L46-L52
void TextInteraction::LerpOpacity() {
    if (moving) {
        elapsed += get_process_delta_time();
        const double c = std::clamp(elapsed / dur, 0.0, 1.0);
        const float color = static_cast<float>(colorvalue_current + (colorvalue_next - colorvalue_current) * c);
        text->set_modulate({color, color, color, text->get_modulate().a});
    }
}

}  // namespace scripts
