#include "scripts/SmokeController.h"

namespace scripts {

// gd: L6-L8
void SmokeController::_ready() {
    SpawnSmoke("barrel");
    SpawnSmoke("chamber");
}

// gd: L10-L15
void SmokeController::SpawnSmoke(std::string alias) {
    if (alias == "barrel") {
        particle_barrel->restart();
    } else if (alias == "chamber") {
        particle_chamber->restart();
    }
}

}  // namespace scripts
