#include "scripts/SegmentManager.h"

#include "godot/scene_tree.h"
#include "scripts/CameraManager.h"
#include "scripts/RoundManager.h"

#include <algorithm>

namespace scripts {

bool SegmentManager::call(const std::string& method, const std::vector<godot::Variant>& /*args*/) {
    if (method == "HideSegment") {
        HideSegment();
        return true;
    }
    return false;
}

// gd: L11-L12
void SegmentManager::_process(double /*delta*/) {
    LerpSegment();
}

// gd: L14-L25
seq::Task SegmentManager::GrowBarrel() {
    const std::string prevsocket = camera->activeSocket;
    roundManager->currentShotgunDamage = 1;
    roundManager->barrelSawedOff = false;
    camera->BeginLerp("grow barrel");
    co_await get_tree()->create_timer(.6);
    ShowSegment();
    speaker->play();
    co_await get_tree()->create_timer(1.8);
    camera->BeginLerp(prevsocket);
    co_await get_tree()->create_timer(.6);
}

// gd: L27-L29
void SegmentManager::HideSegment() {
    moving = false;
    mat->albedo = {1.0f, 1.0f, 1.0f, 0.0f};
}

// gd: L31-L33
void SegmentManager::ShowSegment() {
    elapsed = 0;
    moving = true;
}

// gd: L35-L40
void SegmentManager::LerpSegment() {
    if (moving) {
        elapsed += get_tree()->get_process_delta_time();
        const double c = std::clamp(elapsed / dur, 0.0, 1.0);
        const double alpha = 0.0 + (1.0 - 0.0) * c;   // gd: lerp(0, 1, c)
        mat->albedo = {1.0f, 1.0f, 1.0f, static_cast<float>(alpha)};
    }
}

}  // namespace scripts
