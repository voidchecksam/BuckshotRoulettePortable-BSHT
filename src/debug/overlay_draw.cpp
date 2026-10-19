// The overlay's drawing half, split out so the rest can be tested.
//
// Everything in overlay.cpp (paging, the cursor, which action a button press
// means) is where the indices live and what a test needs to drive. It links no
// GL. This file does, and does nothing else, so the test binary never pulls in
// a renderer to prove the cursor cannot run off the end of a list.
#include "debug/overlay.h"

#include "render/sprite_batch.h"

#include <cstdio>

namespace debug {

namespace {

constexpr int kVisibleRows = 16;
constexpr float kLineHeight = 24.0f;
constexpr float kMarginX = 12.0f;
constexpr float kHeaderY = 10.0f;
constexpr float kFirstRowY = 58.0f;

// The smallest face the UI ships, so the overlay needs no extra atlas: it draws
// from the glyphs already resident.
const char* kFaceKey = "res://fonts/fake receipt.otf @ 16";

const float kWhite[4]  = {1.0f, 1.0f, 1.0f, 1.0f};
const float kDim[4]    = {0.62f, 0.62f, 0.62f, 1.0f};
const float kGood[4]   = {0.55f, 0.95f, 0.55f, 1.0f};
const float kBad[4]    = {1.0f, 0.5f, 0.45f, 1.0f};
const float kCursor[4] = {1.0f, 0.85f, 0.3f, 1.0f};
const float kScrim[4]  = {0.0f, 0.0f, 0.0f, 0.78f};

}  // namespace

void Overlay::Line(render::SpriteBatch& batch, const assets::FontAtlas& fonts,
                   const std::vector<render::SpriteTexture>& pages, int face, float x, float y,
                   const std::string& text, const float rgba[4]) {
    if (face < 0 || text.empty()) return;
    glyphs_.clear();
    fonts.Layout(face, text, x, y, static_cast<float>(designW_) - x,
                 kLineHeight, assets::AlignH::Left, assets::AlignV::Top,
                 glyphs_);
    for (const assets::TextQuad& g : glyphs_) {
        // Each glyph from its own page: the fonts span several pages.
        if (g.page < 0 || g.page >= static_cast<int>(pages.size()) ||
            !pages[static_cast<std::size_t>(g.page)].Valid()) {
            continue;
        }
        batch.DrawRegion(pages[static_cast<std::size_t>(g.page)], g.x, g.y, g.w, g.h, g.u0, g.v0, g.u1, g.v1, rgba);
        ++quads_;
    }
}

void Overlay::Draw(render::SpriteBatch& batch, const assets::FontAtlas& fonts,
                   const std::vector<render::SpriteTexture>& fontPages,
                   const render::SpriteTexture& white, const DebugState& s) {
    quads_ = 0;
    if (!visible_) return;

    const int face = fonts.Find(kFaceKey);
    if (face < 0) return;   // no face, nothing to draw, never a crash

    // A scrim, so text stays readable over a bright table. One quad.
    if (white.Valid()) {
        batch.Draw(white, 0.0f, 0.0f, static_cast<float>(designW_),
                   static_cast<float>(designH_), kScrim);
        ++quads_;
    }

    char buf[256];
    const int rows = RowCount(s);

    std::snprintf(buf, sizeof(buf), "DEBUG  %s  (%d/%d)  L/R page  X act  SELECT close",
                  PageName(page_), static_cast<int>(page_) + 1,
                  static_cast<int>(Page::Count));
    Line(batch, fonts, fontPages, face, kMarginX, kHeaderY, buf, kCursor);

    if (!s.buildId.empty()) {
        Line(batch, fonts, fontPages, face, kMarginX, kHeaderY + kLineHeight,
             s.buildId, kDim);
    }

    float y = kFirstRowY;
    switch (page_) {
        case Page::Summary: {
            const FrameCensus& f = s.frame;
            std::snprintf(buf, sizeof(buf), "%.1f fps  %.2f ms  worst %.2f",
                          f.fps, f.frameMs, f.worstMs);
            Line(batch, fonts, fontPages, face, kMarginX, y, buf, kWhite);
            y += kLineHeight;

            std::snprintf(buf, sizeof(buf), "draw %d/%d batches  %d/%d tris",
                          f.drawnBatches, f.totalBatches, f.drawnTris,
                          f.totalTris);
            Line(batch, fonts, fontPages, face, kMarginX, y, buf, kWhite);
            y += kLineHeight;

            std::snprintf(buf, sizeof(buf), "props %d hidden of %d  (%d start hidden)",
                          f.hiddenProps, s.room.props, s.room.propsHiddenAtLoad);
            Line(batch, fonts, fontPages, face, kMarginX, y, buf, kWhite);
            y += kLineHeight;

            std::snprintf(buf, sizeof(buf), "anim playing=%d  %d moving NOW",
                          f.animPlaying ? 1 : 0, f.movingTargets);
            Line(batch, fonts, fontPages, face, kMarginX, y,
                 buf, f.movingTargets > 0 ? kGood : kBad);
            y += kLineHeight;

            // The cumulative figure, clearly separated. Read early in a run it
            // is legitimately zero, and being told a zero means "nothing
            // animates" sent the last hardware trip down the wrong path.
            std::snprintf(buf, sizeof(buf),
                          "     %d/%d targets animated so far this run",
                          f.everAnimated, f.totalTargets);
            Line(batch, fonts, fontPages, face, kMarginX, y,
                 buf, f.everAnimated > 0 ? kGood : kDim);
            y += kLineHeight;

            std::snprintf(buf, sizeof(buf), "anim played %d  missing %d",
                          f.animsPlayed, f.missingAnims);
            Line(batch, fonts, fontPages, face, kMarginX, y,
                 buf, f.missingAnims == 0 ? kWhite : kBad);
            y += kLineHeight;

            std::snprintf(buf, sizeof(buf), "tracks %d bound  %d unresolved (%d nodes)",
                          s.animation.tracksBound, s.animation.tracksUnresolved,
                          s.animation.unresolvedNodes);
            Line(batch, fonts, fontPages, face, kMarginX, y, buf, kWhite);
            y += kLineHeight;

            std::snprintf(buf, sizeof(buf), "stranded props %d", s.room.propsStranded);
            Line(batch, fonts, fontPages, face, kMarginX, y, buf,
                 s.room.propsStranded == 0 ? kGood : kBad);
            y += kLineHeight;

            std::snprintf(buf, sizeof(buf), "ui %d quads / %d calls   overlay %d quads",
                          f.uiQuads, f.uiDrawCalls, quads_);
            Line(batch, fonts, fontPages, face, kMarginX, y, buf, kDim);
            break;
        }

        case Page::Props: {
            for (int i = 0; i < kVisibleRows; ++i) {
                const int idx = scroll_ + i;
                if (idx >= rows) break;
                const PropRow& r = s.props[static_cast<std::size_t>(idx)];
                // The path's TAIL, because the interesting part of
                // "dealer hands main1/dealer hands parent/hand parent left/..."
                // is the end and the screen is 960 wide.
                std::string name = r.path;
                if (name.size() > 46) name = "..." + name.substr(name.size() - 43);
                std::snprintf(buf, sizeof(buf), "%c %-46s %s%s %4d",
                              idx == cursor_ ? '>' : ' ', name.c_str(),
                              r.visibleAtLoad ? "vis" : "hid",
                              r.hasTarget ? "+anim" : "     ", r.triangles);
                Line(batch, fonts, fontPages, face, kMarginX, y, buf,
                     idx == cursor_ ? kCursor : (r.hasTarget ? kWhite : kDim));
                y += kLineHeight;
            }
            break;
        }

        case Page::Animation: {
            for (int i = 0; i < kVisibleRows; ++i) {
                const int idx = scroll_ + i;
                if (idx >= rows) break;
                const AnimRow& r = s.anims[static_cast<std::size_t>(idx)];
                std::snprintf(buf, sizeof(buf), "%c %-38s %3d/%-3d tr %2d vis %.1fs",
                              idx == cursor_ ? '>' : ' ', r.name.c_str(),
                              r.bound, r.tracks, r.visibleTracks, r.length);
                Line(batch, fonts, fontPages, face, kMarginX, y, buf,
                     idx == cursor_ ? kCursor : (r.bound > 0 ? kWhite : kDim));
                y += kLineHeight;
            }
            break;
        }

        case Page::Screens: {
            for (int i = 0; i < kVisibleRows; ++i) {
                const int idx = scroll_ + i;
                if (idx >= rows) break;
                const ScreenCensus& c = s.ui.screens[static_cast<std::size_t>(idx)];
                std::string name = c.prefix;
                if (name.size() > 44) name = "..." + name.substr(name.size() - 41);
                std::snprintf(buf, sizeof(buf), "%c %-44s %3dc %2df %2dl",
                              idx == cursor_ ? '>' : ' ', name.c_str(),
                              c.controls, c.focusable, c.labels);
                Line(batch, fonts, fontPages, face, kMarginX, y, buf,
                     idx == cursor_ ? kCursor : (c.Exists() ? kWhite : kBad));
                y += kLineHeight;
            }
            break;
        }

        case Page::Count:
            break;
    }
}

}  // namespace debug
