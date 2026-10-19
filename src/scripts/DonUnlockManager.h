// source: scripts/DonUnlockManager.gd (45 lines)
// PORT deviations:
//   - The file at `savepath` (store_var of a Dictionary) is game::SaveStore
//     entry "pills", like SaveFileManager's saves; `store` is set by the host
//     only on a player's boot (app::UsesSaveStore). With none, nothing is
//     unlocked and nothing is written.
//   - FileAccess.file_exists(savepath), read by IntroManager, is Unlocked().
//   - UnlockRoutine's change_scene_to_file is performed by the host.
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

namespace game {
class SaveStore;
}

namespace scripts {

class Unlocker : public godot::Script {
public:
    static constexpr const char* kClassName = "Unlocker";
    const char* get_class_name() const override { return kClassName; }

    static constexpr const char* savepath = "pills";   // gd: user://buckshotroulette_pills.shell
    std::string t = "DOUBLE UNLOCK";                    // gd: L8

    Achievement* ach = nullptr;
    DialogueEnding* dia = nullptr;
    godot::Label* ui = nullptr;
    godot::AnimationPlayer* anim = nullptr;
    game::SaveStore* store = nullptr;

    seq::Task UnlockRoutine();   // gd: L10-L21
    void UnlockMode();        // gd: L23-L29
    void IncrementAmount();   // gd: L31-L45

    // PORT: FileAccess.file_exists(savepath).
    bool Unlocked() const;
};

}  // namespace scripts
