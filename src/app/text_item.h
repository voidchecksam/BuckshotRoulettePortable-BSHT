// One laid-out run of text, ready for the sprite batch. Positions are in
// 960x540 design pixels and UVs are normalised, exactly as assets::TextQuad
// leaves them, so a drawing half has nothing left to convert.
//
// Shared by the loading screen and the start menu: both are text on black in
// the game's own font, drawn through the same batch, and two copies of this
// would only be two things to keep in step.
#pragma once

#include "assets/font.h"

#include <vector>

namespace app {

struct TextItem {
    std::vector<assets::TextQuad> quads;
    float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

}  // namespace app
