// source: scripts/DescriptionManager.gd (61 lines)
// PORT deviations:
//   (none)
#pragma once

#include "godot/control.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"

#include <vector>

namespace scripts {

class DescriptionManager : public godot::Script {
public:
    static constexpr const char* kClassName = "DescriptionManager";
    const char* get_class_name() const override { return kClassName; }

    InteractionManager* interaction = nullptr;
    std::vector<godot::Label*> uiArray;
    double dur = 0.0;

    double elapsed = 0.0;
    bool moving = false;
    double current_opacity = 0.0;
    double next_opacity = 1.0;
    bool started = false;

    void _ready() override;                          // gd: L14-L15
    bool has_process() const override { return true; }
    void _process(double delta) override;            // gd: L17-L19
    void HideText();                                 // gd: L21-L24
    void CheckInteraction();                         // gd: L26-L36
    void BeginLerp();                                // gd: L38-L44
    void EndLerp();                                  // gd: L46-L51
    void LerpText();                                 // gd: L53-L61
};

}  // namespace scripts
