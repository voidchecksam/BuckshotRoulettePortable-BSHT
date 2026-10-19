#include "render/particle_renderer.h"

#include "assets/light_model.h"
#include "assets/raw_image.h"
#include "render/shaders/label3d.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace render {

ParticleRenderer::~ParticleRenderer() { Destroy(); }

// The texture file is a raw image (assets/raw_image.h).
bool ParticleRenderer::Init(const std::string& texturePath) {
    assets::RawImage img;
    if (!assets::ReadRawRgba(texturePath, img) || !texture_.Upload(img.rgba.data(), img.width, img.height)) {
        std::printf("particles: %s is missing or unreadable -- smoke is not drawn\n", texturePath.c_str());
        return false;
    }
    if (!shader_.Compile(shaders::kLabel3DVertexSrc, shaders::kLabel3DFragmentSrc, "particles")) return false;
    uMvp_ = glGetUniformLocation(shader_.Program(), "u_mvp");
    uTex_ = glGetUniformLocation(shader_.Program(), "u_tex");
    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    vbo_ = vbo;
    return vbo_ != 0;
}

void ParticleRenderer::Destroy() {
    if (vbo_) {
        GLuint vbo = vbo_;
        glDeleteBuffers(1, &vbo);
        vbo_ = 0;
    }
    shader_.Destroy();
}

void ParticleRenderer::Bind(const godot::SceneTree& tree, const assets::ProbeGrid* probes) {
    sim_.Bind(tree);
    probes_ = probes;
}

uint64_t ParticleRenderer::LiveMask() const {
    uint64_t mask = 0;
    for (const godot::ParticleQuad& q : sim_.Quads()) {
        if (q.system < 64) mask |= uint64_t{1} << q.system;
    }
    return mask;
}

std::string ParticleRenderer::LiveSummary() const {
    std::vector<int> live(sim_.Systems().size(), 0);
    for (const godot::ParticleQuad& q : sim_.Quads()) ++live[static_cast<std::size_t>(q.system)];
    std::string out;
    for (std::size_t s = 0; s < live.size(); ++s) {
        if (live[s] == 0) continue;
        if (!out.empty()) out += ", ";
        out += sim_.Systems()[s].node->get_path() + " " + std::to_string(live[s]);
    }
    return out;
}

void ParticleRenderer::Draw(const core::Mat4& view, const core::Mat4& viewProj,
                            const assets::light::PointLight* lights, int lightCount) {
    drawn_ = 0;
    sim_.Step();
    if (!shader_.Valid() || !vbo_ || !texture_.Valid() || sim_.Quads().empty()) return;

    // Each system's static light once (probe grid, linear, at the emitter);
    // the live animated lights per quad, where the particle is. Albedo x
    // light through the main grade; alpha is the albedo's.
    const auto& systems = sim_.Systems();
    std::vector<std::array<float, 3>> staticLight(systems.size());
    for (std::size_t s = 0; s < systems.size(); ++s) {
        const godot::GPUParticles3D::Look& L = systems[s].node->get_look();
        float light[3] = {1.0f, 1.0f, 1.0f};
        if (!L.unshaded && probes_ && !probes_->Empty()) {
            const core::Mat4& w = systems[s].node->get_global_transform();
            const float at[3] = {w.m[12], w.m[13], w.m[14]};
            probes_->Sample(at, light);
        }
        staticLight[s] = {light[0], light[1], light[2]};
    }
    const auto quadColour = [&](const godot::ParticleQuad& q) {
        const std::size_t s = static_cast<std::size_t>(q.system);
        const godot::GPUParticles3D::Look& L = systems[s].node->get_look();
        float light[3] = {staticLight[s][0], staticLight[s][1], staticLight[s][2]};
        if (!L.unshaded) {
            for (int i = 0; i < lightCount; ++i) assets::light::AddPointLight(lights[i], q.pos, light);
        }
        const float lin[3] = {L.albedo[0] * light[0], L.albedo[1] * light[1], L.albedo[2] * light[2]};
        float out[3];
        assets::light::GradePixel(lin, assets::light::Grade{}, out);
        std::array<uint8_t, 4> c{};
        for (int k = 0; k < 3; ++k) c[static_cast<std::size_t>(k)] = static_cast<uint8_t>(std::clamp(out[k], 0.0f, 1.0f) * 255.0f + 0.5f);
        const float alpha = assets::light::HdrBlendAlpha(lin, std::clamp(L.albedo[3], 0.0f, 1.0f));
        c[3] = static_cast<uint8_t>(alpha * 255.0f + 0.5f);
        return c;
    };

    // Far to near (blending), keeping the nearest kMaxQuads.
    const float eyeZ[3] = {view.m[2], view.m[6], view.m[10]};
    std::vector<std::pair<float, std::size_t>> order;
    order.reserve(sim_.Quads().size());
    for (std::size_t i = 0; i < sim_.Quads().size(); ++i) {
        const float* p = sim_.Quads()[i].pos;
        order.push_back({p[0] * eyeZ[0] + p[1] * eyeZ[1] + p[2] * eyeZ[2] + view.m[14], i});
    }
    std::sort(order.begin(), order.end());   // view z: most negative = farthest
    if (order.size() > static_cast<std::size_t>(kMaxQuads)) {
        order.erase(order.begin(), order.end() - kMaxQuads);
    }

    // Camera right and up: the view matrix's first two rows.
    const float right[3] = {view.m[0], view.m[4], view.m[8]};
    const float up[3] = {view.m[1], view.m[5], view.m[9]};
    verts_.clear();
    for (const auto& [z, i] : order) {
        const godot::ParticleQuad& q = sim_.Quads()[i];
        const float h = q.size * 0.5f;
        const std::array<uint8_t, 4> col = quadColour(q);
        const float cx[4] = {-1, 1, 1, -1}, cy[4] = {-1, -1, 1, 1};
        Vertex corner[4];
        for (int k = 0; k < 4; ++k) {
            for (int a = 0; a < 3; ++a) corner[k].pos[a] = q.pos[a] + (right[a] * cx[k] + up[a] * cy[k]) * h;
            corner[k].uv[0] = cx[k] * 0.5f + 0.5f;
            corner[k].uv[1] = 0.5f - cy[k] * 0.5f;
            for (int c = 0; c < 4; ++c) corner[k].col[c] = col[static_cast<std::size_t>(c)];
        }
        for (int k : {0, 1, 2, 0, 2, 3}) verts_.push_back(corner[k]);
    }
    drawn_ = static_cast<int>(order.size());

    shader_.Use();
    glUniformMatrix4fv(uMvp_, 1, GL_FALSE, viewProj.m);
    glUniform1i(uTex_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_.Id());
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(Vertex)), verts_.data(),
                 GL_DYNAMIC_DRAW);
    const GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
    glEnableVertexAttribArray(kAttribPosition);
    glVertexAttribPointer(kAttribPosition, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex, pos)));
    glEnableVertexAttribArray(kAttribUv);
    glVertexAttribPointer(kAttribUv, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(kAttribColour);
    glVertexAttribPointer(kAttribColour, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<const void*>(offsetof(Vertex, col)));
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));
    glDisableVertexAttribArray(kAttribPosition);
    glDisableVertexAttribArray(kAttribUv);
    glDisableVertexAttribArray(kAttribColour);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

}  // namespace render
