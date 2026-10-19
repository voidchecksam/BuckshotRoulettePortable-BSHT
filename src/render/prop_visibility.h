// Whether the renderer draws a prop this frame, and with what alpha. One rule,
// in its own header, so it has a test that needs no GL context and a guard that
// keeps it single (tests/check_prop_visibility_rule.cmake).
//
// It is Godot's rule on Godot's state, read from the godot:: tree:
//   - is_visible_in_tree(): the node's own flag ANDed up its Node3D parents
//   - layers & camera cull_mask: `dealer head crushed` has layers 0 and is
//     invisible until DealerIntelligence.gd gives it layer 1
//   - the alpha Godot composites with (GeometryInstance3D::get_draw_alpha):
//     1 - transparency, times the albedo alpha of an alpha-transparent
//     material. ButtonClass_Main hides the 3D focus brackets with
//     transparency 1; the muzzle-flash cone is hidden by albedo alpha 0 until
//     its "fire" animation raises it.
//
// The renderer only reads this state; it never decides visibility from a copy
// of its own.
#pragma once

#include "godot/node.h"

#include <cstdint>

namespace render {

inline float PropAlpha(const godot::GeometryInstance3D& node) {
    return node.get_draw_alpha();
}

inline bool PropIsDrawn(const godot::GeometryInstance3D& node, uint32_t cullMask) {
    return node.is_visible_in_tree() && (node.get_layers() & cullMask) != 0u &&
           PropAlpha(node) > 0.0f;
}

// Drawn in the blended pass, after the opaque geometry: partly transparent, or
// on an alpha-transparent material at any albedo alpha. Godot sends every
// material whose transparency is not DISABLED to its transparent pass, where
// the texture's alpha cuts a shape out of its quad (the health counter's
// lightning icons, which drawn opaque are solid squares).
// An ADD material (the blood splatter plane) adds its colour, scaled by its
// alpha, over what is behind it: black is no change.
inline bool PropIsAdditive(const godot::GeometryInstance3D& node) {
    return node.get_active_material_blend_mode() == godot::kBlendModeAdd;
}

inline bool PropIsBlended(const godot::GeometryInstance3D& node) {
    return PropAlpha(node) < 1.0f || node.get_active_material_transparency() > 0 || PropIsAdditive(node);
}

// Godot's ALPHA_DEPTH_PRE_PASS (4) writes the depth of every pixel at alpha
// >= 0.99 before blending, so such a surface hides what is behind it.
inline bool PropWritesDepth(const godot::GeometryInstance3D& node) {
    constexpr int kDepthPrePass = 4;
    if (!PropIsBlended(node)) return true;
    return node.get_active_material_transparency() == kDepthPrePass && PropAlpha(node) >= 0.99f;
}

}  // namespace render
