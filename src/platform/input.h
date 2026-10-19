// This frame's input.
//
// Gameplay reads only `events`: Godot InputEvents (keys, joypad buttons, the
// mouse and touch) that GameApp feeds to godot::Input, which delivers them
// through SceneTree::push_input as Godot's Viewport does. The original's own
// bindings (project.godot [input], input_map.json) decide what a key means:
// Enter accepts, the arrows and d-pad move focus.
//
// The remaining fields are debug-overlay intent only (SELECT / F1 opens it).
// The overlay is tooling, not the game; while it is open GameApp does not pass
// the events to the game, so its cursor keys never reach the table underneath.
//
// No platform includes: the desktop fills this from SDL and the Vita from
// sceCtrl, and game/ never learns which.
#pragma once

#include "godot/input_event.h"

#include <vector>

namespace platform {

struct FrameInput {
    // The window was closed. Escape does not quit: the original binds it to
    // `exit game`, which only the menu and CRT scripts read.
    bool quit = false;

    // Godot input events, in the order the platform received them.
    std::vector<godot::InputEventRef> events;

    // Debug overlay only.
    bool toggleDebug = false;
    bool debugPrev   = false;
    bool debugNext   = false;
    bool navUp       = false;
    bool navDown     = false;
    bool navLeft     = false;
    bool navRight    = false;
    bool confirm     = false;

    bool AnyNav() const { return navUp || navDown || navLeft || navRight; }
};

}  // namespace platform
