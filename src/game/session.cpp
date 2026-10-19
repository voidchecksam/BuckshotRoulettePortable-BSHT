#include "game/session.h"

#include "godot/scene_tree.h"
#include "scripts/MusicManager.h"
#include "scripts/RoundManager.h"
#include "scripts/bind_support.h"
#include "scripts/intro_skip.h"
#include "scripts/resources.h"

namespace game {

bool Session::Start(godot::SceneTree* tree) {
    if (!tree) return false;
    auto* roundManager =
        godot::script_cast<scripts::RoundManager>(tree->get_node("standalone managers/round manager"));
    if (!roundManager || !roundManager->playerData) {
        if (log_) log_("session: the translated scripts are not bound");
        return false;
    }
    if (playsIntro_) {
        // The original from main.tscn's first frame: IntroManager runs the
        // restroom, the walk and BeginMainGame; SignatureManager the waiver.
        if (log_) log_("session: the restroom intro plays");
        return true;
    }
    // PORT: a diagnostic starts at the table, as if the waiver were signed.
    roundManager->playerData->hasSignedWaiver = true;
    // PORT: --batch diagnostic. The original reaches a later batch only by
    // winning the earlier ones; RoundManager.MainBatchSetup reads this index.
    // Only a requested batch overrides it, so a loaded save is never clobbered.
    if (startBatch_ > 0) roundManager->playerData->currentBatchIndex = startBatch_;
    // PORT: the scene starts inside the restroom intro. This path skips it as
    // tools/godot/dump_state.gd does: the intro manager is freed (its pending
    // awaits die with it) and its blackout lifted.
    scripts::SkipIntro(*tree);
    // PORT: the intro loads the batch's track (IntroManager.MainTrackLoad,
    // gd: L155-L158) and then calls BeginMainGame (BeginGame, gd: L306-L308).
    // Without the track nothing starts the music and the menu's keeps playing.
    if (roundManager->musicManager) roundManager->musicManager->LoadTrack();
    roundManager->BeginMainGame();
    if (log_) log_("session: RoundManager.BeginMainGame()");
    return true;
}

}  // namespace game
