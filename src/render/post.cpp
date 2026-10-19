#include "render/post.h"

#include "render/shaders/post.h"

#include <cstddef>
#include <cstdio>

namespace render {

namespace {

struct QuadVertex {
    float pos[3];
    float uv[2];
};

}  // namespace

Post::~Post() { Destroy(); }

void Post::Destroy() {
    if (quadVbo_) { glDeleteBuffers(1, &quadVbo_); quadVbo_ = 0; }
    if (colour_)  { glDeleteTextures(1, &colour_); colour_ = 0; }
    if (depth_)   { glDeleteRenderbuffers(1, &depth_); depth_ = 0; }
    if (fbo_)     { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
    blit_.Destroy();
}

bool Post::CreateTarget() {
    glGenTextures(1, &colour_);
    glBindTexture(GL_TEXTURE_2D, colour_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, sceneW_, sceneH_, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    // LINEAR so the upscale to native softens rather than blocks up.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenRenderbuffers(1, &depth_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_);
    // GL_DEPTH_COMPONENT16 is the GLES2 baseline. The Vita reports
    // GL_OES_depth24, but depending on an extension for the main render target
    // would leave no fallback, and the probe measured the depth attachment as
    // costing nothing measurable either way.
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, sceneW_, sceneH_);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, colour_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth_);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::printf("post: framebuffer incomplete (0x%x)\n",
                    static_cast<unsigned>(status));
        return false;
    }
    return true;
}

bool Post::CreateQuad() {
    // Two triangles covering clip space, drawn without indices.
    const QuadVertex verts[6] = {
        {{-1, -1, 0}, {0, 0}}, {{1, -1, 0}, {1, 0}}, {{1, 1, 0}, {1, 1}},
        {{-1, -1, 0}, {0, 0}}, {{1, 1, 0}, {1, 1}},  {{-1, 1, 0}, {0, 1}},
    };
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(verts)), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return quadVbo_ != 0;
}

bool Post::Create(int sceneW, int sceneH, int outW, int outH) {
    Destroy();
    if (sceneW <= 0 || sceneH <= 0 || outW <= 0 || outH <= 0) return false;

    sceneW_ = sceneW;
    sceneH_ = sceneH;
    outW_ = outW;
    outH_ = outH;

    if (!CreateTarget() || !CreateQuad() ||
        !blit_.Compile(kBlitVertexSrc, kBlitFragmentSrc, "blit")) {
        Destroy();
        return false;
    }
    return true;
}

void Post::BeginScene() {
    if (!fbo_) return;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, sceneW_, sceneH_);
}

void Post::Composite() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(outX_, outY_, outW_, outH_);
    if (!blit_.Valid()) return;

    // Depth off: one fullscreen pass with nothing to occlude it.
    glDisable(GL_DEPTH_TEST);
    blit_.Use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colour_);
    blit_.SetInt("u_scene", 0);

    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    const GLsizei stride = static_cast<GLsizei>(sizeof(QuadVertex));
    glEnableVertexAttribArray(kAttribPosition);
    glVertexAttribPointer(kAttribPosition, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(QuadVertex, pos)));
    glEnableVertexAttribArray(kAttribUv);
    glVertexAttribPointer(kAttribUv, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(QuadVertex, uv)));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(kAttribPosition);
    glDisableVertexAttribArray(kAttribUv);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnable(GL_DEPTH_TEST);
}

}  // namespace render
