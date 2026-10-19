// Material resources, shared as Godot shares them. A .tres material is one
// object however many meshes and scripts hold it; writing its albedo_color
// through one holder changes what every mesh using it draws. SegmentManager
// fades the cut barrel this way. The library is keyed by the resource path the
// exporters record ("res://materials/overwrite.tres").
#pragma once

#include "godot/node.h"

#include <string>

namespace godot {

class MaterialLibrary {
public:
    // The shared material for `res`: created from `initial` on the first
    // request, the same object on every later one. An empty `res` (a material
    // no resource file names) gets an object of its own. Never null.
    static SurfaceMaterial* get(const std::string& res, const SurfaceMaterial& initial);
    // Forget every material (tests; a scene reload).
    static void reset();
};

// The key the baker reads for a Texture2D resource ("res://misc/display
// rounds.png" -> "inline_textures/misc_display_rounds.png"), derived exactly
// as tools/godot/texture_export.gd names the PNG it saves. "" for "".
// A script writing `material.albedo_texture = tex` writes this key.
std::string TextureKeyForResource(const std::string& res);

}  // namespace godot
