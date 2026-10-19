// The on-device debug overlay.
//
// A build on the device answers only the questions it was compiled to answer.
// The census covers what is known in advance; this lets the device be asked
// the rest: which props exist, whether one is hidden or missing, what an
// animation moves, what a screen contains.
//
// Pages, not a console: a scrolling log needs text entry and a lot of screen;
// pages need four directions and two shoulder buttons, which a Vita has.
// SELECT toggles, L/R cycle, the d-pad drives the current page, CIRCLE acts.
//
// It must never crash the game. Every index is clamped, every action goes
// through the same public API the game uses, and it draws nothing when closed.
//
// Fill rate is the budget on this hardware, and this is text blended over an
// already-shaded frame. It is off by default and reports its own quad count on
// the Summary page.
#pragma once

#include "assets/font.h"
#include "debug/state.h"
#include "platform/input.h"

#include <string>
#include <vector>

// Forward declared, not included. Draw() is the only member that touches the
// renderer, and pulling gl_compat.h into everything that wants to know whether
// the overlay is open would make "headless" a claim rather than a fact: the
// tests link no GL and drive Handle() directly.
namespace render {
class SpriteBatch;
class SpriteTexture;
class SceneRenderer;
}  // namespace render

namespace debug {

enum class Page : int {
    Summary = 0,
    Props,
    Animation,
    Screens,
    Count,
};

const char* PageName(Page p);

// What the overlay wants the host to do. It never reaches into the game
// itself: the host applies these, so the overlay cannot get the game into a
// state the game could not reach on its own.
struct OverlayAction {
    enum class Kind { None, PlayAnimation, TogglePropOverride, OpenScreen };
    Kind kind = Kind::None;

    std::string playerPath;   // PlayAnimation
    std::string name;         // PlayAnimation
    std::string nodePath;     // TogglePropOverride
    bool        visible = false;
    int         screen  = -1; // OpenScreen: index into DebugState::ui.screens
};

class Overlay {
public:
    // `layout` supplies the design resolution the sprite batch draws in, so the
    // overlay lands in the same coordinate space as the game's own interface.
    void SetDesignSize(int w, int h) { designW_ = w; designH_ = h; }

    bool Visible() const { return visible_; }

    // Consumes this frame's debug buttons and returns whatever the operator
    // asked for. Safe to call with an empty state: it clamps to nothing.
    OverlayAction Handle(const platform::FrameInput& in, const DebugState& s);

    // Draws into an already-Begun sprite batch, using `font` and the atlas's
    // pages (indexed by page; each glyph samples ITS page). Draws nothing when
    // closed.
    void Draw(render::SpriteBatch& batch, const assets::FontAtlas& fonts,
              const std::vector<render::SpriteTexture>& fontPages,
              const render::SpriteTexture& white, const DebugState& s);

    Page CurrentPage() const { return page_; }
    int  Cursor() const { return cursor_; }

    // Quads the last Draw queued, so the overlay's own cost is visible on the
    // page that reports cost.
    int QuadsDrawn() const { return quads_; }

private:
    // Rows the current page holds, so the cursor can be clamped without the
    // draw code and the input code disagreeing about the list length.
    int RowCount(const DebugState& s) const;

    void Line(render::SpriteBatch& batch, const assets::FontAtlas& fonts,
              const std::vector<render::SpriteTexture>& pages, int face, float x, float y,
              const std::string& text, const float rgba[4]);

    bool visible_ = false;
    Page page_    = Page::Summary;
    int  cursor_  = 0;
    int  scroll_  = 0;

    // Props the operator has forced on, so a second press turns one back off
    // instead of logging "force VISIBLE" on the same prop again.
    std::vector<std::string> forced_;
    int  quads_   = 0;
    int  designW_ = 960;
    int  designH_ = 544;

    std::vector<assets::TextQuad> glyphs_;
};

}  // namespace debug
