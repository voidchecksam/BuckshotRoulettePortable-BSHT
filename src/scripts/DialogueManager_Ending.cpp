#include "scripts/DialogueManager_Ending.h"

#include "godot/rng.h"
#include "godot/scene_tree.h"

namespace scripts {

// gd: L13-L14
void DialogueEnding::_ready() {
    speaker_click->set_stream(soundArray_clicks[0]);
}

// gd: L16-L22
void DialogueEnding::ShowText_Forever(std::string activeText) {
    looping = false;
    dialogueUI->set_visible_characters(0);
    dialogueUI->set_text(activeText);
    dialogueUI->set_visible(true);
    looping = true;
    get_tree()->spawn(TickText(), get_node_owner());
}

// gd: L24-L27
void DialogueEnding::HideText() {
    looping = false;
    dialogueUI->set_visible(false);
}

// gd: L30-L40
seq::Task DialogueEnding::TickText() {
    while (looping) {
        dialogueUI->set_visible_characters(dialogueUI->get_visible_characters() + 1);
        if (!overriding) {
            speaker_click->set_pitch_scale(static_cast<float>(godot::randf_range(.8, 1)));
        } else {
            speaker_click->set_pitch_scale(static_cast<float>(godot::randf_range(.2, .4)));
        }
        speaker_click->play();
        if (dialogueUI->get_visible_ratio() >= 1.0f) {
            looping = false;
        }
        co_await get_tree()->create_timer(incrementDelay);
    }
}

}  // namespace scripts
