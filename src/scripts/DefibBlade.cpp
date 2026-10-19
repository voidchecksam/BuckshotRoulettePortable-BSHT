#include "scripts/DefibBlade.h"

namespace scripts {

// gd: L10-L17
void DefibBlade::Blade(std::string state, bool playSound) {
    anim->play(state);
    if (state == "open") {
        speaker->set_stream(sound_open);
    } else {
        speaker->set_stream(sound_close);
        wire_normal->set_visible(false);
        wire_broken->set_visible(true);
    }
    if (playSound) speaker->play();
}

}  // namespace scripts
