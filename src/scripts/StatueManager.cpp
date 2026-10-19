#include "scripts/StatueManager.h"

#include "scripts/RoundManager.h"
#include "scripts/resources.h"

namespace scripts {

// gd: L7-L10
void Statue::CheckStatus() {
    if (rm->playerData->playername == " vellon") {
        cup->set_visible(false);
        statue->set_visible(true);
    }
}

}  // namespace scripts
