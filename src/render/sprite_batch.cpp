#include "render/sprite_batch.h"

#include "render/shaders/sprite.h"

#include <cstddef>
#include <cstdio>
#include <utility>

namespace render {

namespace {

// 6 vertices per quad (two triangles). No index buffer: at this scale the
// indices would cost more bandwidth than the duplicated vertices save, and it
// keeps the dynamic upload to a single glBufferData.
constexpr int kVertsPerQuad = 6;

// Enough for the whole UI at once without a mid-frame flush. 65 controls is the
// real ceiling; 512 leaves room for text without ever reallocating during a
// frame, which on a 333 MHz core is worth more than the 24 KB.
constexpr std::size_t kMaxQuads = 512;

void PackColour(const float rgba[4], uint8_t out[4]) {
    for (int i = 0; i < 4; ++i) {
        float v = rgba[i];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        out[i] = static_cast<uint8_t>(v * 255.0f + 0.5f);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// SpriteTexture

SpriteTexture::~SpriteTexture() { Destroy(); }

SpriteTexture::SpriteTexture(SpriteTexture&& other) noexcept
    : id_(other.id_), width_(other.width_), height_(other.height_) {
    other.id_ = 0;
    other.width_ = 0;
    other.height_ = 0;
}

SpriteTexture& SpriteTexture::operator=(SpriteTexture&& other) noexcept {
    if (this != &other) {
        Destroy();
        id_ = other.id_;
        width_ = other.width_;
        height_ = other.height_;
        other.id_ = 0;
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

void SpriteTexture::Destroy() {
    if (id_) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }
    width_ = 0;
    height_ = 0;
}

bool SpriteTexture::Upload(const uint8_t* pixels, int width, int height,
                           Format format) {
    if (!pixels || width <= 0 || height <= 0) return false;
    Destroy();

    glGenTextures(1, &id_);
    if (!id_) return false;
    glBindTexture(GL_TEXTURE_2D, id_);

    // GL_NEAREST throughout. The UI is drawn at 1:1 from a source authored at
    // the same resolution, and this game's whole look is deliberately crunchy;
    // filtering would soften the brackets for nothing.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // CLAMP_TO_EDGE is required, not preferred: GLES2 does not support REPEAT
    // on non-power-of-two textures, and the brackets are 265x102.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Rows are tightly packed (see GlCompatTightUnpack: desktop only).
    GlCompatTightUnpack();

    // GL_LUMINANCE_ALPHA is core in both GL 2.1 and GLES2, which is why the
    // font atlas can use it without an extension check.
    const GLenum gl = (format == Format::LuminanceAlpha8)
                          ? GL_LUMINANCE_ALPHA : GL_RGBA;
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(gl), width, height, 0,
                 gl, GL_UNSIGNED_BYTE, pixels);

    width_ = width;
    height_ = height;
    return true;
}

namespace {

void BeginSurfaceTexture(GLuint& id) {
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GlCompatTightUnpack();
}

}  // namespace

bool SpriteTexture::UploadSurfaceRgba8(const uint8_t* pixels, int width, int height) {
    if (!pixels || width <= 0 || height <= 0) return false;
    Destroy();
    BeginSurfaceTexture(id_);
    if (!id_) return false;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    width_ = width;
    height_ = height;
    return true;
}

bool SpriteTexture::UploadSurfaceDxt(const uint8_t* blocks, std::size_t bytes, int width, int height,
                                     bool alpha) {
    if (!blocks || width <= 0 || height <= 0) return false;
    Destroy();
    BeginSurfaceTexture(id_);
    if (!id_) return false;
    // vitaGL keeps one sticky error: drain it right before the upload, so
    // nothing earlier (the binds and parameters above included) can be blamed
    // on it and get a good page deleted.
    while (glGetError() != GL_NO_ERROR) {}
    glCompressedTexImage2D(GL_TEXTURE_2D, 0,
                           alpha ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT : GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
                           width, height, 0, static_cast<GLsizei>(bytes), blocks);
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::printf("surface page: glCompressedTexImage2D failed (0x%04X)\n", static_cast<unsigned>(err));
        Destroy();
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

// ---------------------------------------------------------------------------
// SpriteBatch

SpriteBatch::~SpriteBatch() { Destroy(); }

bool SpriteBatch::Init() {
    if (!shader_.Compile(shaders::kSpriteVertexSrc, shaders::kSpriteFragmentSrc,
                         "sprite")) {
        return false;
    }
    uInvHalfSize_ = glGetUniformLocation(shader_.Program(), "u_invHalfSize");
    uTex_ = glGetUniformLocation(shader_.Program(), "u_tex");

    glGenBuffers(1, &vbo_);
    if (!vbo_) return false;

    verts_.reserve(kMaxQuads * kVertsPerQuad);
    return true;
}

void SpriteBatch::Destroy() {
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    shader_.Destroy();
    verts_.clear();
}

void SpriteBatch::Begin(int designWidth, int designHeight) {
    if (designWidth <= 0 || designHeight <= 0) return;
    invHalf_[0] =  2.0f / static_cast<float>(designWidth);
    invHalf_[1] = -2.0f / static_cast<float>(designHeight);
    verts_.clear();
    currentTexture_ = 0;
    drawCalls_ = 0;
    quadsQueued_ = 0;
    active_ = true;

    // State is set here, not in End(). A texture change flushes mid-batch, and
    // with several textures in play that happens on an ordinary frame; those
    // flushes would draw with whatever blend and depth state the 3D pass left
    // behind. The UI is a transparent overlay: blend on, depth test off, depth
    // writes off, because writing depth would punch holes in later draws.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
}

void SpriteBatch::Draw(const SpriteTexture& tex, float x, float y, float w,
                       float h, const float rgba[4]) {
    DrawRegion(tex, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, rgba);
}

void SpriteBatch::DrawRegion(const SpriteTexture& tex, float x, float y,
                             float w, float h, float u0, float v0, float u1,
                             float v1, const float rgba[4]) {
    if (!active_ || !tex.Valid()) return;

    // A fully transparent quad still costs a full rect of blended fragments and
    // shows nothing. Fill rate is the budget, so it is dropped here.
    if (rgba[3] <= 0.0f) return;
    if (w <= 0.0f || h <= 0.0f) return;

    if (tex.Id() != currentTexture_) {
        Flush();
        currentTexture_ = tex.Id();
    } else if (verts_.size() + kVertsPerQuad > kMaxQuads * kVertsPerQuad) {
        Flush();
        currentTexture_ = tex.Id();
    }

    uint8_t col[4];
    PackColour(rgba, col);

    const float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    const SpriteVertex tl{{x0, y0}, {u0, v0}, {col[0], col[1], col[2], col[3]}};
    const SpriteVertex tr{{x1, y0}, {u1, v0}, {col[0], col[1], col[2], col[3]}};
    const SpriteVertex br{{x1, y1}, {u1, v1}, {col[0], col[1], col[2], col[3]}};
    const SpriteVertex bl{{x0, y1}, {u0, v1}, {col[0], col[1], col[2], col[3]}};

    verts_.push_back(tl);
    verts_.push_back(tr);
    verts_.push_back(br);
    verts_.push_back(tl);
    verts_.push_back(br);
    verts_.push_back(bl);
    ++quadsQueued_;
}

void SpriteBatch::Flush() {
    if (verts_.empty() || currentTexture_ == 0) {
        verts_.clear();
        return;
    }

    shader_.Use();
    glUniform2f(uInvHalfSize_, invHalf_[0], invHalf_[1]);
    glUniform1i(uTex_, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, currentTexture_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts_.size() * sizeof(SpriteVertex)),
                 verts_.data(), GL_STREAM_DRAW);

    const GLsizei stride = static_cast<GLsizei>(sizeof(SpriteVertex));
    glEnableVertexAttribArray(kAttribPosition);
    glVertexAttribPointer(kAttribPosition, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(SpriteVertex, pos)));
    glEnableVertexAttribArray(kAttribUv);
    glVertexAttribPointer(kAttribUv, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(SpriteVertex, uv)));
    glEnableVertexAttribArray(kAttribColour);
    glVertexAttribPointer(kAttribColour, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                          reinterpret_cast<const void*>(offsetof(SpriteVertex, col)));

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));
    ++drawCalls_;

    // Attributes are disabled again rather than left on. A later draw with
    // fewer attributes would otherwise read from a stale pointer, which is a
    // crash on hardware and usually not one on desktop.
    glDisableVertexAttribArray(kAttribPosition);
    glDisableVertexAttribArray(kAttribUv);
    glDisableVertexAttribArray(kAttribColour);

    verts_.clear();
}

void SpriteBatch::End() {
    if (!active_) return;
    Flush();

    // Restored so the next frame's 3D pass does not inherit a disabled depth
    // buffer, a failure that looks like broken geometry, not broken UI.
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    active_ = false;
}

}  // namespace render
