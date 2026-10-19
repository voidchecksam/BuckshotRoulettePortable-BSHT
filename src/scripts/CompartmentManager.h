// source: scripts/CompartmentManager.gd (49 lines)
// PORT deviations:
//   - `match (alias)` is an if/else chain in the same order.
#pragma once

#include "godot/animation_player.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"

#include <string>
#include <vector>

namespace scripts {

class CompartmentManager : public godot::Script {
public:
    static constexpr const char* kClassName = "CompartmentManager";
    const char* get_class_name() const override { return kClassName; }

    godot::AnimationPlayer* animator_compartment = nullptr;
    godot::AnimationPlayer* animator_briefcase = nullptr;
    godot::AudioStreamPlayer* speaker_briefcase = nullptr;
    godot::AudioStreamPlayer* speaker_main = nullptr;
    std::vector<std::string> soundArray_briefcase;
    std::vector<std::string> soundArray_main;
    bool isHiding_items = true;

    bool toggle = false;
    bool toggle2 = false;

    void _ready() override;                          // gd: L14-L16
    void PlaySound_ShowBriefcase();                  // gd: L18-L20
    void PlaySound_HideBriefcase();                  // gd: L22-L24
    void PlaySound_ClearItems();                     // gd: L26-L28
    void PlaySound_ShowItems();                      // gd: L30-L32
    void PlaySound_HideItems();                      // gd: L34-L36
    void CycleCompartment(std::string alias);        // gd: L38-L49

    // METHOD track keys by name (the compartment animations call the sounds).
    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;
};

}  // namespace scripts
