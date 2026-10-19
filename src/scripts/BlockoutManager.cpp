#include "scripts/BlockoutManager.h"

namespace scripts {

// gd: L5-L7
void Blockout::HideClub() {
    for (std::size_t i = 0; i < objArray_club.size(); ++i) {
        if (objArray_club[i]) objArray_club[i]->set_visible(false);
    }
}

}  // namespace scripts
