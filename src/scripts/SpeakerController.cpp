#include "scripts/SpeakerController.h"

#include "godot/math_funcs.h"

namespace scripts {

// gd: L18-L21
void SpeakerController::_ready() {
    speaker = godot::cast_to<godot::AudioStreamPlayer>(get_node_owner()->get_parent());
    originalVolumeDB = speaker->get_volume_db();
    originalPitch = speaker->get_pitch_scale();
}

// gd: L23-L25
void SpeakerController::_process(double /*delta*/) {
    LerpVolume();
    LerpPitch();
}

// gd: L27-L30
void SpeakerController::SnapVolume(bool state) {
    moving = false;
    if (state) {
        speaker->set_volume_db(static_cast<float>(originalVolumeDB));
    } else {
        speaker->set_volume_db(static_cast<float>(godot::linear_to_db(0)));
    }
}

// gd: L32-L37
void SpeakerController::BeginPitchShift(double pitchFrom, double pitchTo) {
    moving_pitch = false;
    elapsed_pitch = 0;
    currentPitch = pitchFrom;
    nextPitch = pitchTo;
    moving_pitch = true;
}

// gd: L39-L43
void SpeakerController::FadeIn() {
    elapsed = 0;
    currentVolume = godot::db_to_linear(speaker->get_volume_db());
    nextVolume = godot::db_to_linear(originalVolumeDB);
    moving = true;
}

// gd: L45-L49
void SpeakerController::FadeOut() {
    elapsed = 0;
    currentVolume = godot::db_to_linear(speaker->get_volume_db());
    nextVolume = 0;
    moving = true;
}

// gd: L51-L56
void SpeakerController::LerpVolume() {
    if (moving) {
        elapsed += get_process_delta_time();
        const double c = godot::clampf(elapsed / fadeDuration, 0.0, 1.0);
        const double vol = godot::lerpf(currentVolume, nextVolume, c);
        speaker->set_volume_db(static_cast<float>(godot::linear_to_db(vol)));
    }
}

// gd: L58-L64
void SpeakerController::LerpPitch() {
    if (moving_pitch) {
        elapsed_pitch += get_process_delta_time();
        double c = godot::clampf(elapsed_pitch / fadeDuration_pitch, 0.0, 1.0);
        c = godot::ease(c, 0.4);
        const double pitch = godot::lerpf(currentPitch, nextPitch, c);
        speaker->set_pitch_scale(static_cast<float>(pitch));
    }
}

}  // namespace scripts
