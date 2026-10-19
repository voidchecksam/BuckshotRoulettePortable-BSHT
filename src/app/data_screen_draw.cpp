// The data screen's GL half (app/data_screen.h).
#include "app/data_screen.h"

#include "platform/gl_compat.h"
#include "render/sprite_batch.h"

namespace app {
namespace {

constexpr int kDesignWidth = 960;
constexpr int kDesignHeight = 540;
constexpr int kScale = 2;   // 16-pixel glyphs: 60 characters fill the 960 width

}  // namespace

void ShowDataScreen(const GameDataCheck& check, const std::string& exitHint, const std::function<void()>& swap,
                    const std::function<bool()>& exitPressed, const std::function<void()>& capture) {
    render::SpriteBatch batch;
    render::SpriteTexture atlas;
    const bool ready = batch.Init() &&
                       atlas.Upload(DataScreenAtlas().data(), kDataAtlasWidth, kDataAtlasHeight);
    const std::vector<GlyphQuad> quads = LayoutDataScreen(DataScreenLines(check, exitHint), kDesignWidth, kDesignHeight, kScale);
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool first = true;
    do {
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (ready) {
            batch.Begin(kDesignWidth, kDesignHeight);
            for (const GlyphQuad& q : quads) batch.DrawRegion(atlas, q.x, q.y, q.w, q.h, q.u0, q.v0, q.u1, q.v1, white);
            batch.End();
        }
        if (first && capture) capture();
        first = false;
        if (swap) swap();
    } while (!exitPressed());
}

bool PassDataGate(const DataLayout& layout, const std::string& exitHint,
                  const std::function<void(const std::string&)>& log,
                  const std::function<void()>& swap, const std::function<bool()>& exitPressed,
                  const std::function<void()>& capture) {
    log(DescribeLayout(layout));
    if (!layout.gameFolder) return true;
    const GameDataCheck check = CheckGameData(layout.data);
    log("data: " + DescribeCheck(check));
    if (check.ok) return true;
    ShowDataScreen(check, exitHint, swap, exitPressed, capture);
    log("data: the data screen was closed");
    return false;
}

}  // namespace app
