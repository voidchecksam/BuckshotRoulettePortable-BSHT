// source: scripts/AchievementManager.gd (9 lines)
// PORT deviations:
//   - Steam.setAchievement / storeStats become game::Achievements::Unlock, a local
//     store with a toast. `store` is set by the host after binding; with none,
//     an unlock is dropped (tests, tools).
//   - ClearAchievement has no local counterpart the game ever calls; kept as a
//     no-op so the method list mirrors the .gd.
#pragma once

#include "godot/script.h"
#include "scripts/forward.h"

#include <string>

namespace game {
class Achievements;
}

namespace scripts {

class Achievement : public godot::Script {
public:
    static constexpr const char* kClassName = "Achievement";
    const char* get_class_name() const override { return kClassName; }

    game::Achievements* store = nullptr;

    void UnlockAchievement(const std::string& apiname);   // gd: L3-L6
    void ClearAchievement(const std::string& apiname);    // gd: L8-L11
};

}  // namespace scripts
