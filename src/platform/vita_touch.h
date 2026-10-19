// The Vita's front touch panel, as Godot input events.
//
// Vita-only: this is the one place psp2/touch.h is included, so the entry point
// stays a few lines of platform wiring (check_entry_points.cmake) and the
// edge detection itself stays testable on desktop (platform/touch_tracker.h).
#pragma once

#include "platform/input.h"

namespace platform {

// sceTouchSetSamplingState for the front panel. Call once at boot; without it
// sceTouchPeek reports nothing and the screen is dead.
void StartTouchSampling();

// Appends this frame's touch events: a finger that arrived, moved or left.
// godot::Input's touch -> mouse emulation then drives the original's own mouse
// path, which is how a pointer game plays on the Vita.
void ReadTouch(FrameInput& in);

}  // namespace platform
