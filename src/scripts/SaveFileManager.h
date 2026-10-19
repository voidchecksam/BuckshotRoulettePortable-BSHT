// source: scripts/SaveFileManager.gd (79 lines)
// PORT deviations:
//   - FileAccess store_var/get_var of a Dictionary become game::SaveStore
//     Save/Load of a flat map with the same keys, one file per save path
//     (user://buckshotroulette_main_12.shell -> main_12.sav).
//     `store` is set by the host; with none (tests, parity, diagnostics) there
//     is no file, as on a first boot.
//   - LoadGame reads each key with its current value as the default, so an
//     older or partial save loads what it has (GDScript would error on a
//     missing key).
//   - savePath_pills / savePath_stats are declared and never used here.
#pragma once

#include "godot/script.h"
#include "scripts/forward.h"
#include "scripts/resources.h"

#include <optional>
#include <string>

namespace game {
class SaveStore;
}

namespace scripts {

class SaveFileManager : public godot::Script {
public:
    static constexpr const char* kClassName = "SaveFileManager";
    const char* get_class_name() const override { return kClassName; }

    static constexpr const char* savePath = "main_12";
    static constexpr const char* savePath_pills = "pills";
    static constexpr const char* savePath_stats = "playerstats";

    RoundManager* roundManager = nullptr;
    bool isMainMenu = false;
    game::SaveStore* store = nullptr;

    void _ready() override;   // gd: L10-L11
    void SaveGame();          // gd: L13-L43
    // PORT: what SaveGame wrote in this scene (a copy of the PlayerData), so
    // GameApp::ChangeScene carries exactly what the original reloads from the
    // save file, and nothing when the scene never saved (a death in Double or
    // Nothing, DeathManager.gd:141-146). Set even without a store.
    std::optional<PlayerData> savedThisScene;
    void LoadGame();          // gd: L45-L75
    void ClearSave();         // gd: L77-L79
};

}  // namespace scripts
