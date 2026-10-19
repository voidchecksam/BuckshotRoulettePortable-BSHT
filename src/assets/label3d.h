// Label3D: text drawn as quads in a node's own space.
//
// The DEALER / YOU shooting choice, the health counter, the restroom and
// signature-machine screens. Layout is transcribed from Godot 4.1's
// Label3D::_shape and _generate_glyph_surfaces: lines stacked from a
// vertically-aligned start, each line offset by its alignment, the outline
// pass drawn first from an outline-rasterised face, and every size scaled by
// pixel_size.
//
// Headless. Layout yields label-local quads; the renderer transforms them by
// the label's world matrix and draws them from the font atlas.
#pragma once

#include "assets/font.h"
#include "core/mat4.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace assets {

// One Label3D as the scene authored it. Defaults are Godot 4.1's.
struct Label3D {
    std::string path;
    std::string text;
    std::string font;             // res:// path, the atlas key prefix
    int   fontSize    = 32;
    float pixelSize   = 0.005f;
    float modulate[4]        = {1.0f, 1.0f, 1.0f, 1.0f};
    float outlineModulate[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    int   outlineSize = 12;
    int   alignH      = 1;        // Godot HorizontalAlignment; CENTER
    int   alignV      = 1;        // Godot VerticalAlignment; CENTER
    float lineSpacing = 0.0f;
    bool  visible     = true;
    float transparency = 0.0f;    // GeometryInstance3D: 0 opaque .. 1 invisible
    bool  uppercase   = false;
    core::Mat4 world;             // global transform at scene load
    bool  hasWorld    = false;

    // Ancestors whose own visibility is false at scene load. A label is on
    // screen only once every one of them has been shown: the health counter's
    // DEALER / PLAYER wait on HealthCounter.Bootup showing their parents.
    // Empty for a label whose whole chain starts visible.
    std::vector<std::string> hiddenAncestors;
};

class Labels3D {
public:
    bool LoadFromFile(const char* path);
    bool LoadFromString(const std::string& json);

    const std::vector<Label3D>& All() const { return labels_; }
    std::vector<Label3D>&       MutableAll() { return labels_; }

    // Index by scene path, or -1.
    int Find(const std::string& path) const;

private:
    std::vector<Label3D> labels_;
};

// One glyph quad in label space: +Y up, text in the XY plane facing +Z.
struct LabelQuad {
    float x0 = 0.0f, x1 = 0.0f;
    float yTop = 0.0f, yBottom = 0.0f;
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool  outline = false;
    int   page = 0;   // FontAtlas page of the glyph
};

// Whether a label is on screen, given the nodes scripts have shown or hidden.
// Its own flag must be set; every ancestor hidden at load must have been shown;
// and no ancestor (nor the label) may since have been hidden. The same
// is_visible_in_tree rule the room's props follow.
bool Label3DVisible(const Label3D& l,
                    const std::unordered_map<std::string, bool>& nodeOverrides);

// Atlas face keys, as tools/godot/export_fonts.gd writes them.
std::string Label3DFaceKey(const Label3D& l);
std::string Label3DOutlineFaceKey(const Label3D& l);

// Appends the label's quads, outline pass first, as Godot draws it. Returns
// false, appending nothing, when the label's face is not in the atlas: a label
// that silently drew nothing would read as a hidden node, not a missing font.
bool LayoutLabel3D(const FontAtlas& atlas, const Label3D& l, std::vector<LabelQuad>& out);

}  // namespace assets
