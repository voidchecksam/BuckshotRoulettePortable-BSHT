// source: scripts/ShellEjectManager.gd (75 lines)
// PORT deviations:
//   - the three exported StandardMaterial3D are godot::SurfaceMaterial by
//     value, as BlinkerBranch and ShellBranch already hold theirs.
//   - smoke.SpawnSmoke("chamber") draws nothing yet (see SmokeController).
#pragma once

#include "godot/animation_player.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"

#include <string>
#include <vector>

namespace scripts {

// The shell that flies out after a shot. This is where the chamber is
// consumed: every ejection does sequenceArray.remove_at(0), and the dealer's
// sequenceArray_knownShell alongside it. The .gd has the same removal
// commented out in Shoot and FinalizeShooting; the ejection owns it.
class ShellEjectManager : public godot::Script {
public:
    static constexpr const char* kClassName = "ShellEjectManager";
    const char* get_class_name() const override { return kClassName; }

    godot::MeshInstance3D* mesh = nullptr;
    ShellSpawner* shellSpawner = nullptr;
    godot::SurfaceMaterial* mat_blank = nullptr;  // shared resource (MaterialLibrary)
    godot::SurfaceMaterial* mat_live = nullptr;  // shared resource (MaterialLibrary)
    godot::SurfaceMaterial mat_brass;
    godot::AnimationPlayer* animator = nullptr;
    godot::AnimationPlayer* animator_fader = nullptr;
    bool isDealerSide = false;
    SmokeController* smoke = nullptr;
    DealerIntelligence* ai = nullptr;

    bool hasFaded = false;

    // EjectShell, BeerEjection_player and BeerEjection_dealer are called from
    // ANIMATION METHOD TRACKS (animation_methods.json), never from script flow.
    // Without this dispatcher the runtime drops them silently.
    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;

    void _ready() override;      // gd: L16-L19
    void EjectShell();           // gd: L21-L34
    void BeerEjection_player();  // gd: L36-L46
    void BeerEjection_dealer();  // gd: L48-L58
    void DeathEjection();        // gd: L60-L72
    void FadeOutShell();         // gd: L74-L77
};

}  // namespace scripts
