// Textured 2D quads in one draw call per texture.
//
// Most of the interface's TextureRects are the same 104x104 bracket, so one
// texture bind covers nearly all of it; a draw per control would spend dozens
// of state changes on an interface that fits in two.
//
// Fill rate is the budget: every UI pixel is a blended fragment on top of an
// already-shaded scene, so a control the game has not made visible is never
// queued.
//
// Positions are in 960x540 design pixels. The shader maps them to clip space,
// so quads do not have to be rebuilt when the backbuffer changes size.
#pragma once

#include "platform/gl_compat.h"

#include "render/shader.h"

#include <cstdint>
#include <vector>

namespace render {

struct SpriteVertex {
    float   pos[2];
    float   uv[2];
    uint8_t col[4];
};

// An RGBA8 texture living on the GPU. Owns its name and destroys it once.
class SpriteTexture {
public:
    SpriteTexture() = default;
    ~SpriteTexture();

    SpriteTexture(const SpriteTexture&) = delete;
    SpriteTexture& operator=(const SpriteTexture&) = delete;
    SpriteTexture(SpriteTexture&& other) noexcept;
    SpriteTexture& operator=(SpriteTexture&& other) noexcept;

    // How the source bytes are laid out. LuminanceAlpha exists for the font
    // atlas: Godot's glyph cache pins luminance to white and puts coverage in
    // alpha, so `texel * colour` tints text with no work in the fragment
    // shader, at half the bytes of RGBA.
    enum class Format { Rgba8, LuminanceAlpha8 };

    // `pixels` is tightly packed, top row first, as Godot exports it.
    bool Upload(const uint8_t* pixels, int width, int height,
                Format format = Format::Rgba8);

    // A room surface page: bilinear and clamped, as Godot samples a world
    // texture, not the UI's nearest filter. The bake dilates every chart two
    // texels so the filter never reads unbaked space.
    bool UploadSurfaceRgba8(const uint8_t* pixels, int width, int height);
    // The same, from DXT1 blocks (`bytes` = width * height / 2), or DXT5
    // (`bytes` = width * height) when `alpha`.
    bool UploadSurfaceDxt(const uint8_t* blocks, std::size_t bytes, int width, int height, bool alpha);
    void Destroy();

    bool   Valid()  const { return id_ != 0; }
    GLuint Id()     const { return id_; }
    int    Width()  const { return width_; }
    int    Height() const { return height_; }

private:
    GLuint id_ = 0;
    int width_ = 0;
    int height_ = 0;
};

class SpriteBatch {
public:
    SpriteBatch() = default;
    ~SpriteBatch();

    SpriteBatch(const SpriteBatch&) = delete;
    SpriteBatch& operator=(const SpriteBatch&) = delete;

    bool Init();
    void Destroy();

    // `designWidth/Height` are the coordinate space quads are given in, not the
    // backbuffer size. Passing the design resolution letterboxes nothing and
    // scales the whole UI with the window.
    void Begin(int designWidth, int designHeight);

    // Queues one quad. A change of texture flushes, so callers that group by
    // texture get one draw for the whole group (for this game's UI, very
    // nearly one draw for everything).
    void Draw(const SpriteTexture& tex, float x, float y, float w, float h,
              const float rgba[4]);

    // Sub-rectangle of the texture, in normalised uv. The font atlas draws
    // through this: the glyph layout has already divided by the page size.
    void DrawRegion(const SpriteTexture& tex, float x, float y, float w, float h,
                    float u0, float v0, float u1, float v1, const float rgba[4]);

    void End();

    int DrawCalls() const { return drawCalls_; }
    int QuadsQueued() const { return quadsQueued_; }

private:
    void Flush();

    Shader shader_;
    GLuint vbo_ = 0;
    GLint  uInvHalfSize_ = -1;
    GLint  uTex_ = -1;

    std::vector<SpriteVertex> verts_;
    GLuint currentTexture_ = 0;

    float invHalf_[2] = {0.0f, 0.0f};
    int drawCalls_ = 0;
    int quadsQueued_ = 0;
    bool active_ = false;
};

}  // namespace render
