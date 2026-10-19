#include "scripts/CompartmentManager.h"

namespace scripts {

// gd: L14-L16
void CompartmentManager::_ready() {
    animator_compartment->play("RESET");
    animator_briefcase->play("RESET");
}

// gd: L18-L20
void CompartmentManager::PlaySound_ShowBriefcase() {
    speaker_briefcase->set_stream(soundArray_briefcase[0]);
    speaker_briefcase->play();
}

// gd: L22-L24
void CompartmentManager::PlaySound_HideBriefcase() {
    speaker_briefcase->set_stream(soundArray_briefcase[1]);
    speaker_briefcase->play();
}

// gd: L26-L28
void CompartmentManager::PlaySound_ClearItems() {
    speaker_main->set_stream(soundArray_main[0]);
    speaker_main->play();
}

// gd: L30-L32
void CompartmentManager::PlaySound_ShowItems() {
    speaker_main->set_stream(soundArray_main[1]);
    speaker_main->play();
}

// gd: L34-L36
void CompartmentManager::PlaySound_HideItems() {
    speaker_main->set_stream(soundArray_main[2]);
    speaker_main->play();
}

// gd: L38-L49
void CompartmentManager::CycleCompartment(std::string alias) {
    if (alias == "show items") {
        animator_compartment->play("show items");
        isHiding_items = false;
    } else if (alias == "hide items") {
        animator_compartment->play("hide items");
        isHiding_items = true;
    } else if (alias == "show briefcase") {
        animator_briefcase->play("show briefcase");
    } else if (alias == "hide briefcase") {
        animator_briefcase->play("hide briefcase");
    }
}

bool CompartmentManager::call(const std::string& method, const std::vector<godot::Variant>& /*args*/) {
    if (method == "PlaySound_ShowBriefcase") { PlaySound_ShowBriefcase(); return true; }
    if (method == "PlaySound_HideBriefcase") { PlaySound_HideBriefcase(); return true; }
    if (method == "PlaySound_ClearItems") { PlaySound_ClearItems(); return true; }
    if (method == "PlaySound_ShowItems") { PlaySound_ShowItems(); return true; }
    if (method == "PlaySound_HideItems") { PlaySound_HideItems(); return true; }
    return false;
}

}  // namespace scripts
