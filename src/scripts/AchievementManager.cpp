#include "scripts/AchievementManager.h"

#include "game/achievements.h"
#include "scripts/GlobalVariables.h"

namespace scripts {

// gd: L3-L6 (Steam.setAchievement, then Steam.storeStats)
void Achievement::UnlockAchievement(const std::string& apiname) {
    if (!GlobalVariables::using_steam) return;
    if (store) store->Unlock(apiname);
}

// gd: L8-L11 (Steam.clearAchievement, then Steam.storeStats)
void Achievement::ClearAchievement(const std::string& /*apiname*/) {
    // PORT: no caller in the game; the local store never forgets an unlock.
}

}  // namespace scripts
