// source: scripts/DialogueManager.gd (69 lines)
// PORT deviations:
//   - `var elapsed = 0` / `var moving = false` are declared and never used by
//     the source; kept for diffability.
//   - `var origscale_backdrop` (untyped) holds a Vector2; typed godot::Vector2.
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

class Dialogue : public godot::Script {
public:
    static constexpr const char* kClassName = "Dialogue";
    const char* get_class_name() const override { return kClassName; }

    godot::AudioStreamPlayer* speaker_click = nullptr;
    std::vector<std::string> soundArray_clicks;
    godot::Label* dialogueUI = nullptr;
    godot::ColorRect* dialogueUI_backdrop = nullptr;
    double dialogueSpeed = 0.0;
    double incrementDelay = 0.0;

    bool dealerLowPitched = false;
    double elapsed = 0;
    bool moving = false;
    bool looping = false;
    godot::Vector2 origscale_backdrop;

    void _ready() override;                                                     // gd: L16-L18
    seq::Task ShowText_ForDuration(std::string activeText, double showDuration);  // gd: L20-L32

    bool scaling = false;

    void ShowText_Forever(std::string activeText);                              // gd: L35-L47
    // METHOD track keys of `dealer use handsaw` / `dealer use magnifying glass`.
    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;
    void ShowDealerInspectionText();                                            // gd: L49-L51
    void HideText();                                                            // gd: L53-L57
    seq::Task TickText();                                                       // gd: L59-L69
};

}  // namespace scripts
