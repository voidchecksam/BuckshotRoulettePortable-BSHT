// source: scripts/SegmentManager.gd (40 lines)
// PORT deviations:
//   - `mat` is the shared material resource (godot::MaterialLibrary): the cut
//     segment draws with the same object, as in Godot.
#pragma once

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/node.h"
#include "godot/physics.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <string>
#include <vector>

namespace scripts {

class SegmentManager : public godot::Script {
public:
    static constexpr const char* kClassName = "SegmentManager";
    const char* get_class_name() const override { return kClassName; }

    godot::AudioStreamPlayer* speaker = nullptr;
    RoundManager* roundManager = nullptr;
    CameraManager* camera = nullptr;
    godot::SurfaceMaterial* mat = nullptr;  // shared resource (MaterialLibrary)

    double dur = 2;        // gd: L7
    double elapsed = 0;    // gd: L8
    bool moving = false;   // gd: L9

    // Animation METHOD track: "player use handsaw" and "dealer use handsaw"
    // call HideSegment (trap 4).
    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;

    void _process(double delta) override;                 // gd: L11-L12
    bool has_process() const override { return true; }   // the .gd defines _process
    seq::Task GrowBarrel();                               // gd: L14-L25
    void HideSegment();                                   // gd: L27-L29
    void ShowSegment();                                   // gd: L31-L33
    void LerpSegment();                                   // gd: L35-L40
};

}  // namespace scripts
