#include "scripts/TextFire.h"

#include "godot/math.h"
#include "godot/math_funcs.h"
#include "godot/rng.h"

#include <algorithm>

namespace scripts {

// gd: L16-L18
void TextFire::_ready() {
    godot::Node* parent = get_node_owner()->get_parent();
    speaker = godot::cast_to<godot::AudioStreamPlayer>(parent->get_child(1));
    ui = godot::cast_to<godot::Control>(parent);
}

// gd: L20-L27
void TextFire::Fire() {
    if (!changed) speaker->set_pitch_scale(static_cast<float>(godot::randf_range(.95, 1)));
    speaker->play();
    orig_x = ui->get_scale().x;
    orig_y = ui->get_scale().y;
    elapsed = 0;
    moving = true;
    ui->set_visible(true);
}

// gd: L29-L30
void TextFire::_process(double /*delta*/) {
    Lerp();
}

// gd: L32-L40
void TextFire::Lerp() {
    if (moving) {
        elapsed += get_process_delta_time();
        double c = std::clamp(elapsed / duration, 0.0, 1.0);
        c = godot::ease(c, 0.4);
        const double temp_x = godot::lerpf(max_x, orig_x, c);
        const double temp_y = godot::lerpf(max_y, orig_y, c);
        // gd: ui.scale.x = temp_x; ui.scale.y = temp_y (Vector2 is float).
        ui->set_scale({static_cast<float>(temp_x), static_cast<float>(temp_y)});
    }
}

}  // namespace scripts
