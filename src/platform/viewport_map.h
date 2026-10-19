// Screen pixels -> the 960x540 viewport the game is laid out in.
//
// project.godot stretches with mode `viewport` and aspect `keep`, so Godot
// scales the 960x540 image uniformly to fit the window and centres it behind
// black bars. Every pointer position the game sees (the desktop mouse and the
// Vita's front touch panel) has to come back through the same mapping, or the
// cursor and what it hits drift apart the moment the window is not 960x540.
//
// Transcribed from Godot 4.1.4-stable scene/main/window.cpp
// Window::_update_viewport_size (:987):
//   - the aspect decides which side gets bars (:1011-1044);
//   - screen and viewport sizes are floored (:1046-1047);
//   - the margin is rounded: margin = round((video_mode - screen) / 2)
//     (:1052-1058).
// PORT: Godot keeps a `window_transform` and an `offset` in viewport units for
// its own drawing; the port needs only the inverse mapping, so this keeps the
// screen-space margin and the uniform scale.
#pragma once

#include "godot/value_types.h"

#include <cmath>

namespace platform {

struct ViewportMap {
    // Screen pixels per viewport pixel (screen_size / viewport_size).
    float scale = 1.0f;
    // The black bar, in screen pixels, left and top.
    float offsetX = 0.0f;
    float offsetY = 0.0f;
};

inline ViewportMap FitKeepAspect(float windowW, float windowH, float viewW = 960.0f,
                                 float viewH = 540.0f) {
    ViewportMap map;
    if (windowW <= 0.0f || windowH <= 0.0f || viewW <= 0.0f || viewH <= 0.0f) return map;

    const float viewportAspect = viewW / viewH;
    const float windowAspect = windowW / windowH;

    // CONTENT_SCALE_ASPECT_KEEP: the viewport keeps its size and the drawn
    // screen rect shrinks on the axis that does not fit (:1026, :1040).
    float screenW = 0.0f;
    float screenH = 0.0f;
    if (std::fabs(viewportAspect - windowAspect) <= 1e-5f * viewportAspect) {
        screenW = windowW;
        screenH = windowH;
    } else if (viewportAspect < windowAspect) {
        // The window is wider: bars left and right.
        screenW = windowH * viewportAspect;
        screenH = windowH;
    } else {
        // The window is taller: bars top and bottom.
        screenW = windowW;
        screenH = windowW / viewportAspect;
    }
    screenW = std::floor(screenW);
    screenH = std::floor(screenH);
    const float viewportW = std::floor(viewW);
    const float viewportH = std::floor(viewH);

    if (screenW < windowW) {
        map.offsetX = std::round((windowW - screenW) / 2.0f);
    } else if (screenH < windowH) {
        map.offsetY = std::round((windowH - screenH) / 2.0f);
    }
    // Uniform by construction: screenW/viewportW == screenH/viewportH.
    map.scale = viewportW > 0.0f ? screenW / viewportW : 1.0f;
    if (map.scale <= 0.0f) map.scale = 1.0f;
    (void)viewportH;
    return map;
}

// A point in screen pixels as the game's 960x540 viewport sees it. Points on a
// black bar map outside the viewport, as Godot's inverse transform gives too;
// the caller decides whether to care.
inline godot::Vector2 ToViewport(const ViewportMap& map, float x, float y) {
    return {(x - map.offsetX) / map.scale, (y - map.offsetY) / map.scale};
}

// Where the viewport lands on the screen, in screen pixels: Godot's
// attach_to_screen_rect (window.cpp:1071). The 3D composite and the 2D
// interface both place themselves with this, so what is drawn and what a
// pointer hits are the same fit.
struct ViewportRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

inline ViewportRect ViewportRectFor(float windowW, float windowH, float viewW = 960.0f,
                                    float viewH = 540.0f) {
    const ViewportMap map = FitKeepAspect(windowW, windowH, viewW, viewH);
    return {map.offsetX, map.offsetY, viewW * map.scale, viewH * map.scale};
}

}  // namespace platform
