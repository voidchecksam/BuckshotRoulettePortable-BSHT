#include "scripts/SaveFileManager.h"

#include "game/save_store.h"
#include "godot/print.h"
#include "scripts/RoundManager.h"
#include "scripts/resources.h"

namespace scripts {
namespace {

bool Bool(const game::SaveMap& d, const char* key, bool fallback) {
    const auto it = d.find(key);
    if (it == d.end()) return fallback;
    if (const bool* b = std::get_if<bool>(&it->second)) return *b;
    return fallback;
}

int Int(const game::SaveMap& d, const char* key, int fallback) {
    const auto it = d.find(key);
    if (it == d.end()) return fallback;
    if (const int64_t* i = std::get_if<int64_t>(&it->second)) return static_cast<int>(*i);
    return fallback;
}

std::string Str(const game::SaveMap& d, const char* key, const std::string& fallback) {
    const auto it = d.find(key);
    if (it == d.end()) return fallback;
    if (const std::string* s = std::get_if<std::string>(&it->second)) return *s;
    return fallback;
}

}  // namespace

// gd: L10-L11
void SaveFileManager::_ready() {
    if (!isMainMenu) LoadGame();
}

// gd: L13-L43
void SaveFileManager::SaveGame() {
    godot::print("attempting to save game");
    if (!roundManager || !roundManager->playerData) return;
    savedThisScene = *roundManager->playerData;
    if (!store) return;
    const PlayerData& p = *roundManager->playerData;
    const game::SaveMap data = {
        {"has_read_introduction", p.hasReadIntroduction},
        {"has_read_item_swap_introduction", p.hasReadItemSwapIntroduction},
        {"has_read_item_distribution_intro", p.hasReadItemDistributionIntro},
        {"current_batch_index", int64_t{p.currentBatchIndex}},
        {"player_entering_from_death", p.playerEnteringFromDeath},
        {"test_value", int64_t{p.testValue}},
        {"has_read_item_distribution_intro_2", p.hasReadItemDistributionIntro2},
        {"number_of_dialogue_read", int64_t{p.numberOfDialogueRead}},
        {"skipping_shell_description", p.skippingShellDescription},
        {"indicator_shown", p.indicatorShown},
        {"cutter_dialogue_read", p.cutterDialogueRead},
        {"entering_from_true_death", p.enteringFromTrueDeath},
        {"has_signed_waiver", p.hasSignedWaiver},
        {"seen_hint", p.seenHint},
        {"seen_god", p.seenGod},
        {"player_name", p.playername},

        {"stat_shotsFired", int64_t{p.stat_shotsFired}},
        {"stat_shellsEjected", int64_t{p.stat_shellsEjected}},
        {"stat_doorsKicked", int64_t{p.stat_doorsKicked}},
        {"stat_cigSmoked", int64_t{p.stat_cigSmoked}},
        {"stat_beerDrank", int64_t{p.stat_beerDrank}},
    };
    store->Save(savePath, data);
    godot::print("save file closed");
    // PORT: gd also prints the whole dictionary; the save map has no str().
}

// gd: L45-L75
void SaveFileManager::LoadGame() {
    if (!store || !roundManager || !roundManager->playerData) return;
    game::SaveMap data;
    if (!store->Load(savePath, data)) {   // gd: FileAccess.file_exists
        godot::print("user does not have save file");
        return;
    }
    godot::print("attempting to load save file");
    PlayerData& p = *roundManager->playerData;
    p.hasReadIntroduction = Bool(data, "has_read_introduction", p.hasReadIntroduction);
    p.hasReadItemSwapIntroduction = Bool(data, "has_read_item_swap_introduction", p.hasReadItemSwapIntroduction);
    p.hasReadItemDistributionIntro = Bool(data, "has_read_item_distribution_intro", p.hasReadItemDistributionIntro);
    p.currentBatchIndex = Int(data, "current_batch_index", p.currentBatchIndex);
    p.playerEnteringFromDeath = Bool(data, "player_entering_from_death", p.playerEnteringFromDeath);
    p.testValue = Int(data, "test_value", p.testValue);
    p.hasReadItemDistributionIntro2 =
        Bool(data, "has_read_item_distribution_intro_2", p.hasReadItemDistributionIntro2);
    p.numberOfDialogueRead = Int(data, "number_of_dialogue_read", p.numberOfDialogueRead);
    p.skippingShellDescription = Bool(data, "skipping_shell_description", p.skippingShellDescription);
    p.indicatorShown = Bool(data, "indicator_shown", p.indicatorShown);
    p.cutterDialogueRead = Bool(data, "cutter_dialogue_read", p.cutterDialogueRead);
    p.enteringFromTrueDeath = Bool(data, "entering_from_true_death", p.enteringFromTrueDeath);
    p.hasSignedWaiver = Bool(data, "has_signed_waiver", p.hasSignedWaiver);
    p.playername = Str(data, "player_name", p.playername);
    p.seenHint = Bool(data, "seen_hint", p.seenHint);
    p.seenGod = Bool(data, "seen_god", p.seenGod);

    p.stat_shotsFired = Int(data, "stat_shotsFired", p.stat_shotsFired);
    p.stat_shellsEjected = Int(data, "stat_shellsEjected", p.stat_shellsEjected);
    p.stat_doorsKicked = Int(data, "stat_doorsKicked", p.stat_doorsKicked);
    p.stat_cigSmoked = Int(data, "stat_cigSmoked", p.stat_cigSmoked);
    p.stat_beerDrank = Int(data, "stat_beerDrank", p.stat_beerDrank);
    godot::print("closing save file");
}

// gd: L77-L79
void SaveFileManager::ClearSave() {
    godot::print("clearing save file");
    if (store && store->Exists(savePath)) store->Clear(savePath);
}

}  // namespace scripts
