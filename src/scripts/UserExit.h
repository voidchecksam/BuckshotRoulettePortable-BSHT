// source: scripts/UserExit.gd (48 lines)
// PORT deviations:
//   - ExitGame's change_scene_to_file is performed by the host (GameApp).
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

// The Vita pad button a Godot joypad index stands for (PORT).
std::string VitaButtonName(int index);

class ExitManager : public godot::Script {
public:
    static constexpr const char* kClassName = "ExitManager";
    const char* get_class_name() const override { return kClassName; }

    ControllerManager* controller = nullptr;
    CursorManager* cursor = nullptr;
    godot::AnimationPlayer* anim = nullptr;
    godot::Label* label = nullptr;
    bool exitAllowed = false;
    double total = 0;
    // PORT: the Vita names its pad buttons ("START"), not Godot's
    // "Joypad Button 6". Set by GameApp from AppConfig.
    bool vitaPadNames = false;

    void _ready() override;                 // gd: L10-L12
    bool has_process() const override { return true; }
    void _process(double delta) override;   // gd: L14-L22
    void ShowUI();                          // gd: L24-L39
    void CheckExit();                       // gd: L41-L44
    void ExitGame();                        // gd: L46-L48

private:
    seq::Task ReadyRoutine();
};

}  // namespace scripts
