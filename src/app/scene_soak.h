// A scene-change soak. With the switch BSHT_SCENE_SOAK on (an environment
// variable on the desktop, an empty file in ux0:data/bsht on the Vita),
// GameApp changes menu -> main -> menu ... by itself, `cycles` round trips,
// `secondsPerScene` in each, logging the heap at every change, then quits. It
// shows the heap across scene changes: the Vita's is a fixed 128 MB, and a
// fragmented one fails a large allocation with std::bad_alloc.
//
//   SceneSoak soak(3, 4.0f);
//   if (std::string next = soak.Next(dt, scene_.name); !next.empty()) tree_->change_scene_to_file(next);
//   if (soak.Done()) quit();
#pragma once

#include <string>

namespace app {

class SceneSoak {
public:
    SceneSoak(int cycles, float secondsPerScene) : changesLeft_(2 * cycles), seconds_(secondsPerScene) {}

    // Once per frame: the scene to change to now, or "" (stay).
    std::string Next(float dt, const std::string& currentScene) {
        if (done_) return {};
        elapsed_ += dt;
        if (elapsed_ < seconds_) return {};
        elapsed_ = 0.0f;
        if (changesLeft_ == 0) {   // the last scene has had its time too
            done_ = true;
            return {};
        }
        --changesLeft_;
        return currentScene == "menu" ? "res://scenes/main.tscn" : "res://scenes/menu.tscn";
    }

    bool Done() const { return done_; }

private:
    int changesLeft_;
    float seconds_;
    float elapsed_ = 0.0f;
    bool done_ = false;
};

}  // namespace app
