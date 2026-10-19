// The first thing on screen, before anything slow happens: the game's title and
// a stage line that advances through the real init steps, so the room read
// shows as progress rather than a hang.
//
// Split as debug/overlay is: everything here (faces, text layout, the draw
// list) is arithmetic a test can drive, and links no GL.
// loading_screen_draw.cpp links GL and does nothing else.
//
// It runs before app::GameApp exists, so it is not inside it. It owns the font
// atlas during loading and hands it to GameApp afterwards: the atlas is read
// from disk once.
#pragma once

#include "app/text_item.h"
#include "assets/font.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace render {
class SpriteTexture;
}

namespace app {

// The loading screen's name for app::TextItem, the text quad the achievements
// screen and the port's title draw as well.
using LoadingItem = TextItem;

class LoadingScreen {
public:
    // The coordinate space the positions below are in: the one
    // render::SpriteBatch::Begin is handed for the interface.
    static constexpr float kDesignWidth  = 960.0f;
    static constexpr float kDesignHeight = 540.0f;

    LoadingScreen() = default;
    ~LoadingScreen();

    LoadingScreen(const LoadingScreen&) = delete;
    LoadingScreen& operator=(const LoadingScreen&) = delete;

    // Reads <dataRoot>/ui_fonts.json and ui_font_atlas.bin. False when either is
    // missing: the screen then draws nothing and the boot carries on black,
    // which is what it did before this existed.
    bool LoadFont(const std::string& dataRoot);

    // Metrics only, no coverage page. What the layout tests use.
    bool LoadFontMetricsForTest(const std::string& jsonPath);

    // A scene change: lay out with GameApp's resident atlas instead of reading
    // a second copy from disk. It must outlive the screen. The boot screen runs
    // before GameApp exists and loads its own (LoadFont).
    bool UseFonts(const assets::FontAtlas& resident);

    // The entry point owns the title literal: it is the string the desktop
    // window opens with, and keeping it out of here keeps this file clear of
    // anything that reads like game content.
    void SetTitle(const std::string& title);

    // Rebuilds the draw list. Drawing is Show(); this is the half without GL.
    void SetStage(const std::string& stage);

    // The loading bar. Clamped to [0,1] and never runs backwards: a later
    // stage with a lower estimate holds the bar rather than rewinding it.
    void SetProgress(float p);
    float Progress() const { return progress_; }

    // Design-space rectangles: the frame (x, y, w, h), its border, and how
    // wide the fill is inside it. Laid out here so the drawing half only
    // submits quads.
    struct BarLayout {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        float border = 0.0f;
        float fillW = 0.0f;
        float frameRgba[4] = {};   // the stage line's colour
        float fillRgba[4] = {};    // the title's colour
    };
    BarLayout Bar() const;

    const std::vector<LoadingItem>& Items() const { return items_; }
    const std::string& Stage() const { return stage_; }
    bool FontReady() const { return titleFace_ >= 0 && stageFace_ >= 0; }
    // The atlas pages the screen's two faces draw from, sorted, unique: the
    // pages InitGl uploads.
    std::vector<int> PagesNeeded() const;

    const assets::FontAtlas& Fonts() const { return Atlas(); }

    // Moves the atlas out, for app::GameApp::Init. The screen stops drawing text
    // after this, so call it once loading is over. The faces go with it, so
    // FontReady() does not keep claiming a font over an emptied atlas.
    assets::FontAtlas TakeFonts() {
        titleFace_ = stageFace_ = -1;
        items_.clear();
        return std::move(fonts_);
    }

    // Why LoadFont failed, for the report: on the device the last logged line
    // is usually the whole answer, and "no text" is not an answer.
    assets::FontError LastFontError() const { return fontError_; }
    const std::string& LastFontNote() const { return fonts_.LoadNote(); }

    // --- the drawing half, loading_screen_draw.cpp -------------------------

    // The two entry points swap differently (window.Swap vs GlCompatSwap), so
    // the caller owns the swap, as it already owns AppConfig's log sink.
    void SetSwap(std::function<void()> swap) { swap_ = std::move(swap); }

    // Called after the draw and before the swap, so a frame grab reads the
    // buffer that is about to go up. Desktop's --loading-shot only.
    void SetCapture(std::function<void()> capture) { capture_ = std::move(capture); }

    // With UseFonts: draw from GameApp's uploaded pages instead of uploading
    // the screen's own (~6 MB of LA8 on the Vita). Before InitGl; the pages
    // must outlive the screen.
    void BorrowPages(const std::vector<render::SpriteTexture>* pages) { borrowedPages_ = pages; }
    bool InitGl();
    void Show(const std::string& stage);   // SetStage, then clear + draw + swap
    // As above, moving the bar to `progress` first (see SetProgress).
    void Show(const std::string& stage, float progress);
    // Redraws the current stage with the bar at `progress`: the room read's
    // callback, which runs many times per stage.
    void ShowProgress(float progress);
    void DestroyGl();

private:
    void Rebuild();

    assets::FontAtlas fonts_;
    const assets::FontAtlas* borrowed_ = nullptr;                        // UseFonts
    const std::vector<render::SpriteTexture>* borrowedPages_ = nullptr;  // BorrowPages
    const assets::FontAtlas& Atlas() const { return borrowed_ ? *borrowed_ : fonts_; }
    assets::FontError fontError_ = assets::FontError::None;
    int titleFace_ = -1;
    int stageFace_ = -1;

    std::string title_;
    std::string stage_;
    float progress_ = 0.0f;
    std::vector<LoadingItem> items_;

    std::function<void()> swap_;
    std::function<void()> capture_;

    // Opaque so this header pulls in no GL. Owned by loading_screen_draw.cpp.
    struct Gl;
    Gl* gl_ = nullptr;

    // InitGl installs the teardown, so ~LoadingScreen can be defined in the
    // HEADLESS half and still free GL state. Without this the destructor would
    // reference DestroyGl, and every test binary would have to link a renderer
    // to construct a class that lays text out.
    std::function<void()> destroyGl_;
};

// Brings a loading screen up: reads the atlas from `dataRoot`, names it, wires
// the platform's swap, reports a font failure through `log`, and readies GL.
// It does not draw: the caller installs any frame capture and then calls Show,
// so the first stage is the caller's choice. Both entry points use it.
void BeginLoading(LoadingScreen& screen, const std::string& dataRoot,
                  const std::string& title, std::function<void()> swap,
                  const std::function<void(const std::string&)>& log);

// The same for a scene change, with GameApp's resident atlas and uploaded
// pages: nothing is read from disk and no page is uploaded.
void BeginLoadingWith(LoadingScreen& screen, const assets::FontAtlas& fonts,
                      const std::vector<render::SpriteTexture>& pages, const std::string& title,
                      std::function<void()> swap);

}  // namespace app
