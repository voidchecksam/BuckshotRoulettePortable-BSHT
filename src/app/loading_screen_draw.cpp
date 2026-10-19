// The loading screen's drawing half, split out so the rest can be tested.
//
// Everything in loading_screen.cpp (faces, positions, what is on the draw
// list) links no GL and is what a test needs to drive. This file links GL and
// does nothing else, so the test binary never pulls in a renderer to prove a
// title is centred.
//
// It draws straight to the default framebuffer: a flat clear plus text needs
// no offscreen target, and the post pass does not exist yet when the first of
// these frames goes up.
#include "app/loading_screen.h"

#include "platform/gl_compat.h"
#include "render/sprite_batch.h"

namespace app {

struct LoadingScreen::Gl {
    render::SpriteBatch   batch;
    std::vector<render::SpriteTexture> pages;   // by atlas page; only PagesNeeded() are uploaded
    render::SpriteTexture white;   // 1x1, tinted: the loading bar's quads
    bool ready = false;
};

bool LoadingScreen::InitGl() {
    if (!gl_) {
        gl_ = new Gl();
        // Installed HERE, not on the success path: SpriteBatch::Init can fail
        // after compiling a program, and a deleter installed later would leave
        // the destructor a no-op over a live GL object.
        destroyGl_ = [this] { DestroyGl(); };
    }
    if (gl_->ready) return true;
    if (!gl_->batch.Init()) return false;
    {
        const uint8_t white[4] = {255, 255, 255, 255};
        gl_->white.Upload(white, 1, 1);
    }
    // Godot's glyph cache pins luminance to white and puts coverage in alpha, so
    // texel * colour tints text with no work in the fragment shader.
    if (FontReady() && !borrowedPages_) {
        gl_->pages.resize(fonts_.Pages().size());
        for (int p : PagesNeeded()) {
            if (p < 0 || p >= static_cast<int>(fonts_.Pages().size())) continue;
            const assets::FontPage& page = fonts_.Pages()[static_cast<std::size_t>(p)];
            if (page.pixels.empty()) continue;
            gl_->pages[static_cast<std::size_t>(p)].Upload(page.pixels.data(), page.width, page.height,
                                                          render::SpriteTexture::Format::LuminanceAlpha8);
        }
    }
    gl_->ready = true;
    return true;
}

void LoadingScreen::Show(const std::string& stage, float progress) {
    SetProgress(progress);
    Show(stage);
}

void LoadingScreen::ShowProgress(float progress) {
    SetProgress(progress);
    Show(stage_);
}

void LoadingScreen::Show(const std::string& stage) {
    SetStage(stage);

    // The clear and the swap happen even when there is no batch and no atlas.
    // Returning early here would leave whatever VitaGL last put up on screen --
    // its splash, which is the exact thing this exists to replace.
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (gl_ && gl_->ready && gl_->white.Valid()) {
        // The bar draws even without a font: it is the one sign of progress
        // that needs no atlas.
        const BarLayout b = Bar();
        const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        gl_->batch.Begin(static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight));
        gl_->batch.Draw(gl_->white, b.x, b.y, b.w, b.h, b.frameRgba);
        gl_->batch.Draw(gl_->white, b.x + b.border, b.y + b.border, b.w - 2.0f * b.border,
                        b.h - 2.0f * b.border, black);
        if (b.fillW > 0.0f) {
            gl_->batch.Draw(gl_->white, b.x + b.border, b.y + b.border, b.fillW,
                            b.h - 2.0f * b.border, b.fillRgba);
        }
        gl_->batch.End();
    }

    const std::vector<render::SpriteTexture>* pages = gl_ ? (borrowedPages_ ? borrowedPages_ : &gl_->pages) : nullptr;
    if (gl_ && gl_->ready && !pages->empty()) {
        gl_->batch.Begin(static_cast<int>(kDesignWidth), static_cast<int>(kDesignHeight));
        for (const LoadingItem& item : items_) {
            for (const assets::TextQuad& q : item.quads) {
                // Each glyph from ITS page; a page that is not resident draws nothing.
                if (q.page < 0 || q.page >= static_cast<int>(pages->size())) continue;
                const render::SpriteTexture& tex = (*pages)[static_cast<std::size_t>(q.page)];
                if (!tex.Valid()) continue;
                gl_->batch.DrawRegion(tex, q.x, q.y, q.w, q.h, q.u0, q.v0, q.u1, q.v1, item.rgba);
            }
        }
        gl_->batch.End();
    }

    if (capture_) capture_();
    if (swap_) swap_();
}

void LoadingScreen::DestroyGl() {
    if (!gl_) return;
    gl_->batch.Destroy();
    for (render::SpriteTexture& t : gl_->pages) t.Destroy();
    gl_->white.Destroy();
    delete gl_;
    gl_ = nullptr;
    // destroyGl_ is not cleared: the destructor runs DestroyGl through it, and
    // clearing it would free the callable while it runs. The null check above
    // makes a second call a no-op.
}

// Lives in the DRAWING half because it calls InitGl: in the headless half it
// would drag GL into every binary that links bsht_game, which is the split
// this file exists to keep.
void BeginLoading(LoadingScreen& screen, const std::string& dataRoot,
                  const std::string& title, std::function<void()> swap,
                  const std::function<void(const std::string&)>& log) {
    if (!screen.LoadFont(dataRoot) && log) {
        log(std::string("fonts: ") + assets::FontErrorText(screen.LastFontError()) + " (" +
            screen.LastFontNote() + ") -- the loading screen and menu have no text");
    }
    screen.SetTitle(title);
    screen.SetSwap(std::move(swap));
    screen.InitGl();
}

void BeginLoadingWith(LoadingScreen& screen, const assets::FontAtlas& fonts,
                      const std::vector<render::SpriteTexture>& pages, const std::string& title,
                      std::function<void()> swap) {
    screen.UseFonts(fonts);
    screen.BorrowPages(&pages);
    screen.SetTitle(title);
    screen.SetSwap(std::move(swap));
    screen.InitGl();
}

}  // namespace app
