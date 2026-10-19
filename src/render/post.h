// The offscreen render target and the upscale that composites it.
//
// 3D renders into a 720x408 target and composites to the native 960x544: the
// hardware ladder measured 960x544 dropping frames and 720x408 holding 60
// under the full budget. Fill rate is the constraint, and 720x408 is 56% of
// the pixels. The interface draws at native resolution on top, so text stays
// crisp.
//
// There is no screen effect chain. The original's only screen effect is
// posterize 7, and it is baked into every surface page.
#pragma once

#include "platform/gl_compat.h"

#include "render/shader.h"

namespace render {

class Post {
public:
    ~Post();

    Post() = default;
    Post(const Post&) = delete;
    Post& operator=(const Post&) = delete;

    // `sceneW/H` is the offscreen 3D resolution; `outW/H` the window.
    bool Create(int sceneW, int sceneH, int outW, int outH);
    void Destroy();

    // Binds the offscreen target and sets the viewport to the scene size.
    void BeginScene();

    // Unbinds, then upscales the scene over the output rect: one fetch per
    // pixel, no maths.
    void Composite();

    // Where the composite lands on screen. Godot's stretch mode `viewport` with
    // aspect `keep` centres the image behind black bars, so on a 960x544 screen
    // the 960x540 game sits 2 pixels down. The entry point computes it with
    // platform::ViewportRectFor, which is also what pointer input is mapped
    // through, so what is drawn and what is hit agree. Defaults to the whole
    // output.
    void SetOutputRect(int x, int y, int w, int h) {
        outX_ = x;
        outY_ = y;
        outW_ = w;
        outH_ = h;
    }

    bool Valid() const { return fbo_ != 0 && blit_.Valid(); }
    int SceneWidth()  const { return sceneW_; }
    int SceneHeight() const { return sceneH_; }

private:
    bool CreateTarget();
    bool CreateQuad();

    GLuint fbo_       = 0;
    GLuint colour_    = 0;
    GLuint depth_     = 0;
    GLuint quadVbo_   = 0;

    Shader blit_;

    int sceneW_ = 0, sceneH_ = 0;
    int outX_ = 0, outY_ = 0;
    int outW_ = 0, outH_ = 0;
};

}  // namespace render
