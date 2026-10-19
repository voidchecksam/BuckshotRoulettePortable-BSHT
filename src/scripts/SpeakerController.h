// source: scripts/SpeakerController.gd (81 lines)
// PORT deviations:
//   - `var speaker : AudioStreamPlayer2D` is godot::AudioStreamPlayer (the port
//     loads AudioStreamPlayer2D as AudioStreamPlayer).
//   - untyped `var elapsed = 0` / `var elapsed_pitch = 0` accumulate seconds: double.
#pragma once

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <string>
#include <vector>

namespace scripts {

class SpeakerController : public godot::Script {
public:
    static constexpr const char* kClassName = "SpeakerController";
    const char* get_class_name() const override { return kClassName; }

    double fadeDuration = 0.0;
    double fadeDuration_pitch = 0.0;
    double lowestPitchValue = 0.0;

    godot::AudioStreamPlayer* speaker = nullptr;
    double elapsed = 0;
    double elapsed_pitch = 0;
    bool moving = false;
    bool moving_pitch = false;
    double originalVolumeDB = 0.0;
    double originalPitch = 0.0;
    double currentPitch = 0.0;
    double nextPitch = 0.0;
    double currentVolume = 0.0;
    double nextVolume = 0.0;

    void _ready() override;                                        // gd: L18-L21
    bool has_process() const override { return true; }
    void _process(double delta) override;                          // gd: L23-L25
    void SnapVolume(bool state);                                   // gd: L27-L30
    void BeginPitchShift(double pitchFrom, double pitchTo);        // gd: L32-L37
    void FadeIn();                                                 // gd: L39-L43
    void FadeOut();                                                // gd: L45-L49
    void LerpVolume();                                             // gd: L51-L56
    void LerpPitch();                                              // gd: L58-L64
};

}  // namespace scripts
