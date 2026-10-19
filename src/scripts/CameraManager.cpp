#include "scripts/CameraManager.h"

#include "godot/math_funcs.h"
#include "godot/rng.h"
#include "scripts/resources.h"

namespace scripts {

bool CameraManager::call(const std::string& method, const std::vector<godot::Variant>& /*args*/) {
    if (method == "BeginLerpCutSegment") {
        BeginLerpCutSegment();
        return true;
    }
    if (method == "BeginLerpEnemy") {
        BeginLerpEnemy();
        return true;
    }
    return false;
}

// gd: L24-L25
void CameraManager::_ready() {}

// gd: L28-L30
void CameraManager::_process(double /*delta*/) {
    LerpMovement();
}

// gd: L32-L33
void CameraManager::CutOutBackground() {
    animator_background->play("cut out");
}

// gd: L34-L35
void CameraManager::FadeInBackground() {
    animator_background->play("fade in");
}

// gd: L37-L50
void CameraManager::BeginLerp(std::string lerpName) {
    PanSound();
    activeSocket = lerpName;
    for (int i = 0; i < static_cast<int>(socketArray.size()); ++i) {
        if (lerpName == socketArray[static_cast<std::size_t>(i)]->socketName) {
            activeIndex = i;
            currentPos = cam->get_position();
            currentRot = cam->get_rotation_degrees();
            currentFov = cam->get_fov();
            elapsed = 0;
            moving = true;
        }
    }
}

// gd: L52-L62
void CameraManager::BeginLerpCutSegment() {
    PanSound();
    activeSocket = "dealer cut shotgun";
    for (int i = 0; i < static_cast<int>(socketArray.size()); ++i) {
        if (activeSocket == socketArray[static_cast<std::size_t>(i)]->socketName) {
            activeIndex = i;
            currentPos = cam->get_position();
            currentRot = cam->get_rotation_degrees();
            currentFov = cam->get_fov();
            elapsed = 0;
            moving = true;
        }
    }
}

// gd: L64-L74
void CameraManager::BeginLerpEnemy() {
    PanSound();
    activeSocket = "enemy";
    for (int i = 0; i < static_cast<int>(socketArray.size()); ++i) {
        if (activeSocket == socketArray[static_cast<std::size_t>(i)]->socketName) {
            activeIndex = i;
            currentPos = cam->get_position();
            currentRot = cam->get_rotation_degrees();
            currentFov = cam->get_fov();
            elapsed = 0;
            moving = true;
        }
    }
}

// gd: L76-L79
void CameraManager::PanSound() {
    const double pitch = godot::randf_range(.5, 1);
    speaker_pan->set_pitch_scale(static_cast<float>(pitch));
    if (isPlayingSound) speaker_pan->play();
}

// gd: L81-L83
void CameraManager::SavePrevious() {
    rot_previous = cam->get_rotation_degrees();
    pos_previous = cam->get_position();
}

// gd: L85-L96
void CameraManager::LerpMovement() {
    if (moving) {
        elapsed += get_process_delta_time();
        double c = godot::clampf(elapsed / dur, 0.0, 1.0);
        c = godot::ease(c, 0.2);
        const CameraSocket& socket = *socketArray[static_cast<std::size_t>(activeIndex)];
        const godot::Vector3 pos = godot::lerp(currentPos, socket.pos, c);
        const godot::Vector3 rot = godot::lerp(currentRot, socket.rot, c);
        const double fov = godot::lerp(currentFov, socket.fov, c);
        cam->set_fov(static_cast<float>(fov));
        cam->set_transform_origin(pos);
        cam->set_rotation_degrees(rot);
    }
}

}  // namespace scripts
