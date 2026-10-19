#include "scripts/ShellBranch.h"

namespace scripts {

// gd: L8-L9
void ShellClass::_ready() {}

// gd: L11-L12
void ShellClass::_process(double /*delta*/) {}

// gd: L14-L19
void ShellClass::ApplyStatus() {
    if (isLive) {
        mesh->set_surface_override_material(1, *mat_live);
    } else {
        mesh->set_surface_override_material(1, *mat_blank);
    }
}

}  // namespace scripts
