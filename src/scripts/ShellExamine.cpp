#include "scripts/ShellExamine.h"

#include "scripts/ShellSpawner.h"

namespace scripts {

bool ShellExamine::call(const std::string& method, const std::vector<godot::Variant>& /*args*/) {
    if (method == "SetupShell") {
        SetupShell();
        return true;
    }
    if (method == "RevertShell") {
        RevertShell();
        return true;
    }
    return false;
}

// gd: L10-L11
void ShellExamine::_ready() {
    shellParent->set_visible(false);
}

// gd: L13-L22
void ShellExamine::SetupShell() {
    // PORT: the .gd's locals color_blue / color_red (and v2.2's colour-blind
    // swap of them) are never read, so they are left out.
    const std::string& shellState = shellSpawner->sequenceArray[0];
    if (shellState == "live") {
        mesh->set_surface_override_material(1, *mat_live);
    } else {
        mesh->set_surface_override_material(1, *mat_blank);
    }
    shellParent->set_visible(true);
}

// gd: L24-L25
void ShellExamine::RevertShell() {
    shellParent->set_visible(false);
}

}  // namespace scripts
