// The 2D UI: control rects, bracket textures and gamepad focus links.
//
// Godot lays Control nodes out from anchors, offsets, container rules and theme
// minimum sizes. Reimplementing that is a lot of code whose only job is to
// reproduce rectangles that never change, so Godot measures them once, after
// layout has settled, and they ship as data. The port positions nothing; it
// draws what it is given.
//
// The focus links are what make the port playable on a pad. The game ships
// explicit focus_neighbor links wired by hand in the scene: the author's own
// navigation graph, which gives the port the same feel on a d-pad as the
// original has on a controller. Where a link is absent Godot picks a
// neighbour by geometry at runtime, which NearestInDirection reproduces.
//
// Headless: no GL. The layout, the focus graph and the pixel pool are all
// testable without a window.
#pragma once

#include "assets/font.h"

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

enum class UiLayoutError {
    None = 0,
    FileNotFound,
    BadJson,
    NoControls,
    PixelsMissing,
    PixelsTruncated,
    Inconsistent,
};

const char* UiLayoutErrorText(UiLayoutError e);

// Mirrors Godot's Control.FocusMode. Only controls that can take focus are
// navigation targets: a decorative TextureRect must never be selectable.
enum class FocusMode : int {
    None  = 0,
    Click = 1,
    All   = 2,
};

struct UiTexture {
    std::string key;            // the res:// path it came from
    int         width  = 0;
    int         height = 0;
    uint32_t    offset = 0;     // byte index into the shared RGBA pool
    uint32_t    bytes  = 0;

    // Pixels are RGBA8, top row first, exactly as Godot hands them over.
    bool Valid() const {
        return width > 0 && height > 0 &&
               bytes == static_cast<uint32_t>(width) *
                        static_cast<uint32_t>(height) * 4u;
    }
};

struct UiControl {
    std::string path;           // relative to the scene root, the join key
    std::string name;
    std::string type;           // Button / TextureRect / Label / Control / ColorRect
    std::string text;           // Label and Button captions

    float x = 0.0f, y = 0.0f;
    float w = 0.0f, h = 0.0f;   // already scaled (get_global_rect)
    // The global transform's scale: text is laid out in the unscaled box and
    // scaled with the control, about its top left.
    float scale[2] = {1.0f, 1.0f};

    bool visible        = false;   // this node's own flag
    bool visibleInTree  = false;   // ...and every ancestor's

    float modulate[4]     = {1, 1, 1, 1};
    float selfModulate[4] = {1, 1, 1, 1};

    FocusMode focusMode = FocusMode::None;

    int texture = -1;           // index into Textures(), or -1

    // ColorRect fill. `hasColour` is false for layouts exported before the
    // field existed, and such a rect is not drawn rather than drawn white.
    float colour[4] = {1, 1, 1, 1};
    bool  hasColour = false;

    // Resolved control indices, or -1 where the scene left it to geometry.
    int focusTop = -1, focusBottom = -1, focusLeft = -1, focusRight = -1;

    // -----------------------------------------------------------------------
    // Button behaviour, read from the ButtonClass_Main instance in the scene.
    //
    // The script's defaults are the opposite of the real values: ButtonClass
    // declares `ui_opacity_inactive = 1` and `ui_opacity_active = 0.78`, but
    // nearly every button overrides them to 0 and 1 (bracket hidden until
    // focused, then fully opaque). The declared defaults would leave every
    // bracket on screen.
    bool  isButton = false;
    bool  isActive = false;   // scene may disable a button outright
    bool  isDynamic = false;  // false means it never changes its highlight
    float opacityInactive = 1.0f;
    float opacityActive   = 1.0f;

    // The control whose alpha this button drives, or -1.
    int highlight = -1;

    // 6 of the 68 buttons highlight GEOMETRY instead: SetUI drives
    // GeometryInstance3D.transparency rather than a CanvasItem's alpha. That
    // path is how the items on the table light up, and its target is a 3D node
    // that joins to scene_graph.json, not to anything in this table.
    bool        is3d = false;
    std::string highlight3d;

    // The alpha the highlight should have given this button's focus state.
    float HighlightAlpha(bool focused) const {
        if (!isDynamic) return opacityInactive;
        return focused ? opacityActive : opacityInactive;
    }

    // -----------------------------------------------------------------------
    // Label text.
    //
    // `fontKey` is "res://fonts/fake receipt.otf @ 23", the string
    // export_fonts.gd keys its faces by: which font a Label resolves to comes
    // from a theme, an override or the project default and is not visible in
    // the .tscn. Both exporters ask the live node so they cannot disagree.
    std::string fontKey;
    int   fontSize = 0;
    float fontColour[4] = {1, 1, 1, 1};

    // Godot's HorizontalAlignment / VerticalAlignment values.
    int alignH = 0;
    int alignV = 0;
    // Godot's TextServer::OverrunBehavior (1: TRIM_CHAR, the ending's stats).
    int overrun = 0;
    // Pixels between lines: LabelSettings, else the theme (export_ui.gd).
    int lineSpacing = kDefaultLineSpacing;

    bool IsLabel() const { return !fontKey.empty() && !text.empty(); }

    // A control can take focus only if the scene says so. Godot's rule.
    bool CanFocus() const { return focusMode == FocusMode::All; }

    float CentreX() const { return x + w * 0.5f; }
    float CentreY() const { return y + h * 0.5f; }

    bool Contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

enum class Direction { Up, Down, Left, Right };

class UiLayout {
public:
    // `pixelsPath` may be null, which loads the layout without the pixel pool.
    // Tests and the baker's accounting pass do not need 2.4 MB of RGBA.
    UiLayoutError Load(const char* layoutPath, const char* pixelsPath);

    const std::vector<UiControl>& Controls() const { return controls_; }
    const std::vector<UiTexture>& Textures() const { return textures_; }

    int DesignWidth()  const { return designWidth_; }
    int DesignHeight() const { return designHeight_; }

    // -1 when absent. Paths are the scene-relative join key.
    int Find(const std::string& path) const;
    // The index of the texture loaded from `key` (its res:// path), or -1.
    int FindTexture(const std::string& key) const;

    // Every control whose path starts with `prefix`, in scene order. This is
    // how a screen ("bracket ui/item grabbing") is addressed as a unit.
    std::vector<int> FindByPrefix(const std::string& prefix) const;

    // The explicit link if the scene wired one, otherwise the geometric
    // neighbour. Returns -1 when there is nothing to move to, which leaves
    // focus where it is rather than dropping it.
    int Neighbour(int from, Direction d) const;

    // Godot's fallback: among focusable controls lying in `d`, the nearest.
    // Exposed separately so a test can prove the explicit link is preferred.
    //
    // Visibility is not consulted here. `visibleInTree` is the state at scene
    // load, when none of the focusable controls is on screen (every bracket is
    // hidden until its screen opens), so filtering on it would return nothing.
    // What is on screen is runtime state the caller owns.
    int NearestInDirection(int from, Direction d) const;

    // Restricted to `candidates`, which is how a screen navigates within itself
    // rather than jumping to a bracket belonging to another scene.
    int NearestInDirection(int from, Direction d,
                           const std::vector<int>& candidates) const;

    // Neighbour restricted the same way: an explicit link is still preferred,
    // but only when it lands inside the open screen.
    int Neighbour(int from, Direction d, const std::vector<int>& candidates) const;

    // Pointer to a texture's first pixel, or null if the index or its buffer is
    // bad, or the pixels were released. Each texture has a buffer of its own.
    const uint8_t* TexturePixels(int textureIndex) const;

    // Frees every texture's pixels once they are on the GPU (GameApp does,
    // right after the upload); the controls and texture entries stay.
    void ReleasePixels() { std::vector<std::vector<uint8_t>>().swap(chunks_); }

private:
    // The link the scene wired for `d`, or -1. Shared so the plain and the
    // restricted Neighbour cannot disagree about what the scene said.
    int ExplicitLink(int from, Direction d) const;

    // True when `to` lies in direction `d` of `from`, with its ranking score.
    // One implementation for both overloads.
    bool Scores(int from, int to, Direction d, float* outScore) const;

    std::vector<UiControl> controls_;
    std::vector<UiTexture> textures_;
    std::vector<std::vector<uint8_t>> chunks_;   // texture i's RGBA8, read on its own
    int designWidth_  = 0;
    int designHeight_ = 0;
};

}  // namespace assets
