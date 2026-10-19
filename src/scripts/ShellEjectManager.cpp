#include "scripts/ShellEjectManager.h"

#include "godot/scene_tree.h"
#include "scripts/DealerIntelligence.h"
#include "scripts/GlobalVariables.h"
#include "scripts/RoundManager.h"
#include "scripts/ShellSpawner.h"
#include "scripts/SmokeController.h"
#include "scripts/resources.h"

namespace scripts {

bool ShellEjectManager::call(const std::string& method,
                             const std::vector<godot::Variant>& /*args*/) {
    if (method == "EjectShell") { EjectShell(); return true; }
    if (method == "BeerEjection_player") { BeerEjection_player(); return true; }
    if (method == "BeerEjection_dealer") { BeerEjection_dealer(); return true; }
    return false;
}

// gd: L16-L19
void ShellEjectManager::_ready() {
    mesh->set_surface_override_material(0, mat_brass);
    if (GlobalVariables::colorblind) {
        mat_live->albedo = GlobalVariables::colorblind_color_live;
        mat_blank->albedo = GlobalVariables::colorblind_color_blank;
    } else {
        mat_live->albedo = GlobalVariables::default_color_live;
        mat_blank->albedo = GlobalVariables::default_color_blank;
    }
}

// gd: L21-L34
void ShellEjectManager::EjectShell() {
    shellSpawner->roundManager->playerData->stat_shellsEjected += 1;
    animator_fader->play("RESET");
    if (shellSpawner->sequenceArray[0] == "live") {
        mesh->set_surface_override_material(1, *mat_live);
    } else {
        mesh->set_surface_override_material(1, *mat_blank);
    }
    mesh->set_visible(true);
    if (!isDealerSide) {
        animator->play("ejecting shell_player1");
    } else {
        animator->play("eject shell");
    }
    if (shellSpawner->sequenceArray[0] == "live") {
        smoke->SpawnSmoke("chamber");
    }
    shellSpawner->sequenceArray.erase(shellSpawner->sequenceArray.begin());
    ai->sequenceArray_knownShell.erase(ai->sequenceArray_knownShell.begin());
    hasFaded = false;
}

// gd: L36-L46
void ShellEjectManager::BeerEjection_player() {
    shellSpawner->roundManager->playerData->stat_shellsEjected += 1;
    animator_fader->play("RESET");
    if (shellSpawner->sequenceArray[0] == "live") {
        mesh->set_surface_override_material(1, *mat_live);
    } else {
        mesh->set_surface_override_material(1, *mat_blank);
    }
    mesh->set_visible(true);
    animator->play("ejecting shell_player1");
    shellSpawner->sequenceArray.erase(shellSpawner->sequenceArray.begin());
    ai->sequenceArray_knownShell.erase(ai->sequenceArray_knownShell.begin());
    hasFaded = false;
}

// gd: L48-L58
void ShellEjectManager::BeerEjection_dealer() {
    shellSpawner->roundManager->playerData->stat_shellsEjected += 1;
    animator_fader->play("RESET");
    if (shellSpawner->sequenceArray[0] == "live") {
        mesh->set_surface_override_material(1, *mat_live);
    } else {
        mesh->set_surface_override_material(1, *mat_blank);
    }
    mesh->set_visible(true);
    animator->play("eject shell beer");
    shellSpawner->sequenceArray.erase(shellSpawner->sequenceArray.begin());
    ai->sequenceArray_knownShell.erase(ai->sequenceArray_knownShell.begin());
    hasFaded = false;
}

// gd: L60-L72
void ShellEjectManager::DeathEjection() {
    animator_fader->play("RESET");
    if (shellSpawner->sequenceArray[0] == "live") {
        mesh->set_surface_override_material(1, *mat_live);
    } else {
        mesh->set_surface_override_material(1, *mat_blank);
    }
    mesh->set_visible(true);
    if (!isDealerSide) {
        animator->play("ejecting shell_player1");
    } else {
        animator->play("eject shell");
    }
    if (shellSpawner->sequenceArray[0] == "live") {
        smoke->SpawnSmoke("chamber");
    }
    shellSpawner->sequenceArray.erase(shellSpawner->sequenceArray.begin());
    ai->sequenceArray_knownShell.erase(ai->sequenceArray_knownShell.begin());
    hasFaded = false;
}

// gd: L74-L77
void ShellEjectManager::FadeOutShell() {
    if (!hasFaded) {
        animator_fader->play("fade out shell");
        hasFaded = true;
    }
}

}  // namespace scripts
