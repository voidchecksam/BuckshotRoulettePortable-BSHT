// source: scripts/CameraManager.gd (96 lines)
// PORT deviations:
//   - `var elapsed = 0` (untyped) accumulates seconds: double.
//   - `var activeIndex` starts null in GDScript; -1 here. It is only read while
//     `moving`, which BeginLerp sets together with it.
//   - `cam.transform.origin = pos` is Node3D::set_position.
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

class CameraManager : public godot::Script {
public:
    static constexpr const char* kClassName = "CameraManager";
    const char* get_class_name() const override { return kClassName; }

    godot::Camera3D* cam = nullptr;
    godot::Node3D* obj = nullptr;
    godot::Node* defib = nullptr;  // PORT: DefibCutter not translated yet
    godot::AnimationPlayer* animator_background = nullptr;
    godot::AudioStreamPlayer* speaker_pan = nullptr;
    std::vector<CameraSocket*> socketArray;
    double dur = 0.0;
    bool isPlayingSound = false;

    bool moving = false;
    double elapsed = 0;
    godot::Vector3 currentPos;
    godot::Vector3 currentRot;
    double currentFov = 0.0;
    int activeIndex = -1;
    std::string activeSocket;
    godot::Vector3 pos_previous;
    godot::Vector3 rot_previous;

    godot::AnimationPlayer* anim_test = nullptr;

    void _ready() override;                          // gd: L24-L25
    bool has_process() const override { return true; }
    void _process(double delta) override;            // gd: L28-L30
    void CutOutBackground();                         // gd: L32-L33
    void FadeInBackground();                         // gd: L34-L35
    void BeginLerp(std::string lerpName);            // gd: L37-L50
    // METHOD track keys of `dealer use handsaw` / `dealer use magnifying glass`.
    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;
    void BeginLerpCutSegment();                      // gd: L52-L62
    void BeginLerpEnemy();                           // gd: L64-L74
    void PanSound();                                 // gd: L76-L79
    void SavePrevious();                             // gd: L81-L83
    void LerpMovement();                             // gd: L85-L96
};

}  // namespace scripts
