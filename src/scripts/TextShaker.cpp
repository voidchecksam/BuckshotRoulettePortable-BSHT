#include "scripts/TextShaker.h"

#include "godot/rng.h"
#include "godot/scene_tree.h"

namespace scripts {

// gd: L10-L13
void TextShaker::_ready() {
    origpos = text->get_position();
    looping = true;
    get_tree()->spawn(Shake());   // called without await in the .gd
}

// gd: L15-L24
seq::Task TextShaker::Shake() {
    while (looping) {
        const float randx = static_cast<float>(godot::randf_range(offset1, offset2));
        const float randy = static_cast<float>(godot::randf_range(offset1, offset2));
        text->set_position({randx, randy});
        co_await get_tree()->create_timer(delay);
        text->set_position(origpos);
        co_await get_tree()->create_timer(delay);
    }
}

}  // namespace scripts
