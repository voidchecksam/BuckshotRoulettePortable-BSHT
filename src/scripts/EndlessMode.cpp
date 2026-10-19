#include "scripts/EndlessMode.h"

#include "scripts/ItemAmounts.h"
#include "scripts/RoundManager.h"
#include "scripts/ShellSpawner.h"
#include "scripts/resources.h"

namespace scripts {

// gd: L9-L25
void Endless::SetupEndless() {
    smokerdude->set_visible(false);
    roundManager->endless = true;
    roundManager->playerData->hasReadIntroduction = true;
    roundManager->playerData->hasReadItemSwapIntroduction = true;
    roundManager->playerData->hasReadItemDistributionIntro = true;
    roundManager->playerData->hasReadItemDistributionIntro2 = true;
    roundManager->playerData->numberOfDialogueRead = 3;
    roundManager->playerData->skippingShellDescription = true;
    roundManager->playerData->indicatorShown = true;
    roundManager->playerData->cutterDialogueRead = true;
    roundManager->playerData->seenGod = true;
    roundManager->shellSpawner->skipDialoguePresented = true;
    roundManager->shellLoadingSpedUp = true;
    display->albedo_texture = displayrounds;
    for (AmountResource* res : amounts->array_amounts) res->amount_active = res->amount_don;
}

}  // namespace scripts
