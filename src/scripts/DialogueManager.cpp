#include "scripts/DialogueManager.h"

#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"

namespace scripts {

bool Dialogue::call(const std::string& method, const std::vector<godot::Variant>& /*args*/) {
    if (method == "ShowDealerInspectionText") {
        ShowDealerInspectionText();
        return true;
    }
    if (method == "HideText") {
        HideText();
        return true;
    }
    return false;
}

// gd: L16-L18
void Dialogue::_ready() {
    origscale_backdrop = dialogueUI_backdrop->get_scale();
    speaker_click->set_stream(soundArray_clicks[3]);
}

// gd: L20-L32
seq::Task Dialogue::ShowText_ForDuration(std::string activeText, double showDuration) {
    const godot::Vector2 current = dialogueUI_backdrop->get_scale();
    if (current.x != origscale_backdrop.x || current.y != origscale_backdrop.y) {
        dialogueUI_backdrop->set_scale(origscale_backdrop);
    }
    looping = false;
    dialogueUI->set_visible_characters(0);
    dialogueUI->set_text(activeText);
    dialogueUI->set_visible(true);
    dialogueUI_backdrop->set_visible(true);
    looping = true;
    get_tree()->spawn(TickText());
    co_await get_tree()->create_timer(showDuration);
    looping = false;
    dialogueUI->set_visible(false);
    dialogueUI_backdrop->set_visible(false);
}

// gd: L35-L47
void Dialogue::ShowText_Forever(std::string activeText) {
    if (scaling) {
        dialogueUI_backdrop->set_scale({17.209f, dialogueUI_backdrop->get_scale().y});
    } else {
        dialogueUI_backdrop->set_scale(origscale_backdrop);
    }

    looping = false;
    dialogueUI->set_visible_characters(0);
    dialogueUI->set_text(activeText);
    dialogueUI->set_visible(true);
    dialogueUI_backdrop->set_visible(true);
    looping = true;
    get_tree()->spawn(TickText());
    scaling = false;
}

// gd: L49-L51
void Dialogue::ShowDealerInspectionText() {
    const godot::Vector2 current = dialogueUI_backdrop->get_scale();
    if (current.x != origscale_backdrop.x || current.y != origscale_backdrop.y) {
        dialogueUI_backdrop->set_scale(origscale_backdrop);
    }
    ShowText_Forever(godot::tr("INTERESTING"));
}

// gd: L53-L57
void Dialogue::HideText() {
    looping = false;
    dialogueUI->set_visible(false);
    dialogueUI_backdrop->set_visible(false);
}

// gd: L59-L69
seq::Task Dialogue::TickText() {
    while (looping) {
        dialogueUI->set_visible_characters(dialogueUI->get_visible_characters() + 1);
        if (!dealerLowPitched) {
            speaker_click->set_pitch_scale(static_cast<float>(godot::randf_range(0.5, 0.6)));
        } else {
            speaker_click->set_pitch_scale(static_cast<float>(godot::randf_range(0.2, 0.3)));
        }
        speaker_click->play();
        if (dialogueUI->get_visible_ratio() >= 1.0f) {
            looping = false;
        }
        co_await get_tree()->create_timer(incrementDelay);
    }
}

}  // namespace scripts
