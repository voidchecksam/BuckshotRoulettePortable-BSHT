#include "app/loading_screen.h"

#include <algorithm>

namespace app {

namespace {

// Faces the UI already ships (assets/extracted/ui_fonts.json), so the screen
// needs no atlas of its own.
const char* kTitleFaceKey = "res://fonts/fake receipt.otf @ 56";
const char* kStageFaceKey = "res://fonts/fake receipt.otf @ 23";

// Where the two lines sit in the 960x540 design space: the title a little above
// centre, the stage line below it, both centred across the full width.
constexpr float kTitleY = 200.0f;
constexpr float kTitleH = 90.0f;
constexpr float kStageY = 320.0f;
constexpr float kStageH = 40.0f;
// The bar sits under the stage line, in the same muted colour.
constexpr float kBarY = 380.0f;
constexpr float kBarW = 360.0f;
constexpr float kBarH = 10.0f;
constexpr float kBarBorder = 2.0f;

const float kTitleColour[4] = {0.90f, 0.87f, 0.80f, 1.0f};
// Dimmer than the title: it is the subordinate line, and it is the one that
// changes, so it should read as status rather than as a second heading.
const float kStageColour[4] = {0.55f, 0.53f, 0.49f, 1.0f};

}  // namespace

// Defined in the HEADLESS half on purpose. The teardown is whatever InitGl
// installed, so a test binary can construct and destroy a LoadingScreen without
// linking a renderer.
LoadingScreen::~LoadingScreen() {
    if (destroyGl_) destroyGl_();
}

bool LoadingScreen::LoadFont(const std::string& dataRoot) {
    const std::string json   = dataRoot + "/ui_fonts.json";
    const std::string pixels = dataRoot + "/ui_font_atlas.bin";
    fontError_ = fonts_.Load(json.c_str(), pixels.c_str());
    if (fontError_ != assets::FontError::None) return false;
    titleFace_ = fonts_.Find(kTitleFaceKey);
    stageFace_ = fonts_.Find(kStageFaceKey);
    Rebuild();
    return FontReady();
}

bool LoadingScreen::LoadFontMetricsForTest(const std::string& jsonPath) {
    fontError_ = fonts_.Load(jsonPath.c_str(), nullptr);
    if (fontError_ != assets::FontError::None) return false;
    titleFace_ = fonts_.Find(kTitleFaceKey);
    stageFace_ = fonts_.Find(kStageFaceKey);
    Rebuild();
    return FontReady();
}

bool LoadingScreen::UseFonts(const assets::FontAtlas& resident) {
    borrowed_ = &resident;
    fontError_ = assets::FontError::None;
    titleFace_ = resident.Find(kTitleFaceKey);
    stageFace_ = resident.Find(kStageFaceKey);
    Rebuild();
    return FontReady();
}

void LoadingScreen::SetTitle(const std::string& title) {
    title_ = title;
    Rebuild();
}

void LoadingScreen::SetStage(const std::string& stage) {
    stage_ = stage;
    Rebuild();
}

void LoadingScreen::Rebuild() {
    items_.clear();
    if (!FontReady()) return;

    if (!title_.empty()) {
        LoadingItem item;
        Atlas().Layout(titleFace_, title_, 0.0f, kTitleY, kDesignWidth, kTitleH,
                      assets::AlignH::Centre, assets::AlignV::Centre, item.quads);
        for (int i = 0; i < 4; ++i) item.rgba[i] = kTitleColour[i];
        items_.push_back(std::move(item));
    }
    if (!stage_.empty()) {
        LoadingItem item;
        Atlas().Layout(stageFace_, stage_, 0.0f, kStageY, kDesignWidth, kStageH,
                      assets::AlignH::Centre, assets::AlignV::Centre, item.quads);
        for (int i = 0; i < 4; ++i) item.rgba[i] = kStageColour[i];
        items_.push_back(std::move(item));
    }
}

void LoadingScreen::SetProgress(float p) {
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    if (p > progress_) progress_ = p;
}

LoadingScreen::BarLayout LoadingScreen::Bar() const {
    BarLayout b;
    b.w = kBarW;
    b.h = kBarH;
    b.x = (kDesignWidth - kBarW) / 2.0f;
    b.y = kBarY;
    b.border = kBarBorder;
    b.fillW = progress_ * (kBarW - 2.0f * kBarBorder);
    for (int i = 0; i < 4; ++i) {
        b.frameRgba[i] = kStageColour[i];
        b.fillRgba[i] = kTitleColour[i];
    }
    return b;
}

std::vector<int> LoadingScreen::PagesNeeded() const {
    std::vector<int> pages;
    for (int face : {titleFace_, stageFace_}) {
        if (face < 0 || face >= static_cast<int>(Atlas().Faces().size())) continue;
        const assets::FontFace& f = Atlas().Faces()[static_cast<std::size_t>(face)];
        for (const assets::Glyph& g : f.glyphs) pages.push_back(g.page);
        for (const auto& [cp, g] : f.extra) pages.push_back(g.page);
    }
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    return pages;
}

}  // namespace app
