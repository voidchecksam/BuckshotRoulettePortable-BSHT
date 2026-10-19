#include "scripts/ColorblindManager.h"

#include "godot/material.h"
#include "scripts/GlobalVariables.h"

namespace scripts {

// gd: L7-L9
void Colorblind::_ready() {
    if (GlobalVariables::colorblind) {
        mat_live->albedo = GlobalVariables::colorblind_color_live;
        mat_blank->albedo = GlobalVariables::colorblind_color_blank;
    } else {
        mat_live->albedo = GlobalVariables::default_color_live;
        mat_blank->albedo = GlobalVariables::default_color_blank;
    }
}

}  // namespace scripts
