// source: scripts/TextInteraction.gd (52 lines)
// PORT deviations:
//   (none)
#pragma once

#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"

namespace scripts {

class TextInteraction : public godot::Script {
public:
    static constexpr const char* kClassName = "TextInteraction";
    const char* get_class_name() const override { return kClassName; }

    godot::Label3D* text = nullptr;
    InteractionManager* interaction = nullptr;
    double dur = 0.0;
    double colorvalue_min = 0.0;
    double colorvalue_max = 0.0;

    double elapsed = 0.0;
    bool moving = false;
    bool fs1 = true;
    bool fs2 = false;
    double colorvalue_current = 0.0;
    double colorvalue_next = 0.0;

    bool has_process() const override { return true; }
    void _process(double delta) override;   // gd: L16-L18
    void CheckStatus();                     // gd: L20-L30
    void BeginLerp();                       // gd: L32-L37
    void EndLerp();                         // gd: L39-L44
    void LerpOpacity();                     // gd: L46-L52
};

}  // namespace scripts
