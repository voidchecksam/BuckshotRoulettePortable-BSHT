// source: scripts/MedicineManager.gd (75 lines)
// PORT deviations:
//   - call() dispatches the METHOD key of `dealer use expired medicine`
//     (UseMedicine_Dealer); the coroutine is spawned, as a method track's
//     call of a func with awaits runs to its first await and returns.
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

class Medicine : public godot::Script {
public:
    static constexpr const char* kClassName = "Medicine";
    const char* get_class_name() const override { return kClassName; }

    RoundManager* rm = nullptr;
    ItemInteraction* interaction = nullptr;
    CameraManager* cam = nullptr;
    DeathManager* death = nullptr;
    HealthCounter* counter = nullptr;
    godot::AnimationPlayer* anim_dealerhands = nullptr;
    DealerIntelligence* dealerai = nullptr;
    godot::AudioStreamPlayer* speaker_medicine = nullptr;
    std::string death_dealer;
    std::string death_player;

    bool dealerDying = false;   // gd: L15

    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;

    seq::Task UseMedicine();              // gd: L17-L38
    seq::Task UseMedicine_Dealer();       // gd: L40-L66
    seq::Task Perms(double d);            // gd: L68-L70
    bool GetFlip();                       // gd: L72-L75
};

}  // namespace scripts
