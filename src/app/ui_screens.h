// Which 2D subtrees the table scene draws: every top-level subtree under
// "Camera/dialogue UI". The tree decides what is on screen (visible,
// modulate), as Godot draws whatever is visible, so a subtree such as the
// death blackout or Double or Nothing's YES / NO needs no listing. The
// dialogue box is excluded: it is a screen of its own (GameApp opens it beside
// whatever the player is choosing between).
#pragma once

#include <string>
#include <vector>

namespace assets {
class UiLayout;
}

namespace app {

// "Camera/dialogue UI/<child>" for every child with controls under it, in
// layout order, `excluded` (a prefix) left out.
std::vector<std::string> TableUiScreens(const assets::UiLayout& layout, const std::string& excluded);

}  // namespace app
