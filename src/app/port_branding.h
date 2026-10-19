// The port's own menu title and version: the original's 3D title card reads
// blurry at the Vita's resolution, so the menu shows the port's title
// (art/menu-title/port_menu_title.png, baked by tools/bake_menu_logo.py to its
// on-screen pixel size and drawn 1:1) in its place, and the version reads
// "v1.0 (PSVITA)". Without the baked logo file the title is text.
//
// The 3D title node stays in the tree with its visibility untouched:
// MenuManager shows and hides it per screen, and that decides when the title
// draws. Only its render layers go to 0, so the mesh itself is never drawn.
// Headless: Apply edits the tree, Layout places the title; GameApp draws it.
#pragma once

#include "app/achievements_screen.h"
#include "assets/font.h"
#include "assets/ui_layout.h"
#include "godot/scene_tree.h"

#include <string>

namespace app {

class PortBranding {
public:
    static constexpr const char* kTitle = "BUCKSHOT ROULETTE";
    static constexpr const char* kSubtitle = "PORTABLE";
    static constexpr const char* kVersion = "v1.0 (PSVITA)";
    static constexpr const char* kTitleNode = "title";
    static constexpr const char* kVersionNode = "Camera/dialogue UI/menu ui/main screen/version";

    // The credits: the port's credit closes the original's "MAIN CREDITS" in
    // every locale (godot::set_message_suffix), just above the Critical Reflex
    // logo.
    static constexpr const char* kPortCredit =
        "\n\nPorted and rewritten in C++ by Abdul Muizz\n(justaverage_ / voidchecksum)";
    static constexpr const char* kMainCreditsKey = "MAIN CREDITS";
    static constexpr const char* kMainCreditsNode =
        "Camera/dialogue UI/menu ui/credits/credits move parent/credits_main credits";

    // How far the credit's lines push what follows the main credits down, in
    // the scrolling parent's pixels. 0 without the label or its face.
    static float CreditHeight(const assets::FontAtlas& fonts, const assets::UiLayout& layout);
    // Moves every sibling below the main credits down by `height`, so the gap
    // Godot leaves above the logo stays the same. Returns how many moved.
    static int MakeRoomForTheCredit(godot::SceneTree& tree, float height);
    // The menu's Discord corner: a link a Vita cannot open. Hidden, so focus
    // skips it too.
    static constexpr const char* kHiddenMenuNodes[] = {
        "Camera/dialogue UI/menu ui/main screen/discord icon",
        "Camera/dialogue UI/menu ui/main screen/bracket selection_discord",
        "Camera/dialogue UI/menu ui/main screen/true button_discord",
    };

    // Where the original 3D title card's text sat on the 960x540 design
    // screen, and its grey (Godot menu frame 600: 276 x 193 at (340, 96),
    // 139/255): the logo takes its place at its size, so it reads as the
    // original did. tools/bake_menu_logo.py bakes the logo to that 276-pixel
    // width.
    static constexpr float kLogoTop = 96.0f;
    static constexpr float kLogoBottom = 289.0f;
    static constexpr float kLogoGrey = 139.0f / 255.0f;

    // The baked logo's pixel size; 0 x 0 when there is no logo file.
    struct LogoSize {
        int w = 0;
        int h = 0;
    };

    // What Apply did, for the log (a re-export that renames a node must not
    // bring the Discord corner back silently).
    struct Applied {
        bool ok = false;                      // the title and version nodes were found
        int hidden = 0;                       // of kHiddenMenuNodes found and hidden
    };

    // On a menu tree, before its scripts are ready: hides the 3D title mesh,
    // prefixes the credits and hides the Discord corner. Changes nothing (ok
    // false) when the title or version node is missing.
    static Applied Apply(godot::SceneTree& tree);

    // After the scripts are ready: writes kVersion over the label MenuManager
    // set (v2.2's _ready writes the game's) and returns what it replaced; ""
    // when the label is missing.
    static std::string WriteVersion(godot::SceneTree& tree);

    // One log line for an Apply result.
    static std::string Describe(const Applied& done);

    // While the 3D title node is visible: the logo centred in its band, at its
    // own pixel size (shrunk to fit only if taller than the band), or the text
    // title when there is no logo. Nothing otherwise.
    static void Layout(const godot::SceneTree& tree, const assets::FontAtlas& fonts, LogoSize logo,
                       OverlayDraw& out);
};

}  // namespace app
