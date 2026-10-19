#include "scripts/PipelineManager.h"

#include "godot/print.h"
#include "scripts/GlobalVariables.h"

namespace scripts {

bool PipelineManager::call(const std::string& method, const std::vector<godot::Variant>& /*args*/) {
    if (method == "Toggle") { Toggle(); return true; }
    return false;
}

// gd: L20-L21
void PipelineManager::_ready() {
    CheckStatus();
    if (scene == "menu") CheckStatus();
    AdjustSettings(scene);
}

// gd: L23-L24
void PipelineManager::CheckStatus() {
    // PORT: the game asks RenderingServer for a rendering device; the port
    // draws like the Vulkan reference, so it is never the GL path.
    GlobalVariables::using_gl = false;
    godot::print("running openGL: ", GlobalVariables::using_gl);
}

// gd: L26-L27
void PipelineManager::Toggle() {
    if (GlobalVariables::using_gl) {
        main_restroom->set_visible(false);
        main_restroom->set_visible(true);
    }
}

// gd: L29-L30
void PipelineManager::AdjustSettings(const std::string& /*scene*/) {
    // PORT: the whole body is the GL path (using_gl), never taken here.
}

}  // namespace scripts
