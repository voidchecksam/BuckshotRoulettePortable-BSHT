#include "scripts/ShellDropManager.h"

#include "godot/rng.h"

namespace scripts {

bool ShellDropManager::call(const std::string& method, const std::vector<godot::Variant>& /*args*/) {
    if (method == "PlayShellDropSound") { PlayShellDropSound(); return true; }
    return false;
}

// gd: L6-L9
void ShellDropManager::PlayShellDropSound() {
    const int randindex =
        static_cast<int>(godot::randi_range(0, static_cast<int64_t>(soundArray.size()) - 1));
    if (randindex < 0 || randindex >= static_cast<int>(soundArray.size())) return;
    speaker->set_stream(soundArray[static_cast<std::size_t>(randindex)]);
    speaker->play();
}

}  // namespace scripts
