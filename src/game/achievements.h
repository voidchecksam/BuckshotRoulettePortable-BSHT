// Achievements, kept locally. The original unlocks them on Steam from
// AchievementManager.gd; the port records each id in the "achievements" save
// and queues a toast the first time, as Steam's overlay does. The names and
// descriptions are data (tools/achievements_en.json), never compiled in.
//
// Lives in src/game but is compiled into bsht_scripts: the translated
// Achievement script is its caller, and scripts cannot link bsht_game.
#ifndef BSHT_GAME_ACHIEVEMENTS_H
#define BSHT_GAME_ACHIEVEMENTS_H

#include <deque>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace game {

class SaveStore;

struct AchievementText {
    std::string id;
    std::string name;
    std::string description;
    // Steam hides it: the screen shows "???" until it is unlocked.
    bool hidden = false;
};

class Achievements {
public:
    // The 16 from achievements_en.json, in file order. False if absent or empty.
    bool LoadText(const char* jsonPath);
    // Reads the unlocked set from the "achievements" save; later unlocks write it.
    void Attach(const SaveStore* store);

    // True the FIRST time an id is unlocked: persists it and queues a toast.
    bool Unlock(const std::string& id);
    bool IsUnlocked(const std::string& id) const;
    const std::vector<AchievementText>& All() const { return text_; }
    // FIFO of first-time unlocks, for the toast.
    bool PopToast(AchievementText& out);
    // Where unlocks and refusals are reported (the host's log). Optional.
    void SetLog(std::function<void(const std::string&)> log) { log_ = std::move(log); }

private:
    void Say(const std::string& line) const;
    const AchievementText* Find(const std::string& id) const;

    std::vector<AchievementText> text_;
    std::set<std::string> unlocked_;
    std::deque<AchievementText> toasts_;
    const SaveStore* store_ = nullptr;
    std::function<void(const std::string&)> log_;
};

}  // namespace game

#endif  // BSHT_GAME_ACHIEVEMENTS_H
