#include "render/label3d_renderer.h"

#include <map>

#include "render/shaders/label3d.h"

#include <cstddef>
#include <cstring>

namespace render {
namespace {

uint8_t PackUnit(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return static_cast<uint8_t>(v * 255.0f + 0.5f);
}

}  // namespace

Label3DRenderer::~Label3DRenderer() { Destroy(); }

bool Label3DRenderer::Init() {
    if (!shader_.Compile(shaders::kLabel3DVertexSrc, shaders::kLabel3DFragmentSrc, "label3d")) {
        return false;
    }
    uMvp_ = glGetUniformLocation(shader_.Program(), "u_mvp");
    uTex_ = glGetUniformLocation(shader_.Program(), "u_tex");
    glGenBuffers(1, &vbo_);
    return vbo_ != 0;
}

void Label3DRenderer::Destroy() {
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    shader_.Destroy();
}

void Label3DRenderer::Bind(const assets::Labels3D* labels, const assets::FontAtlas* atlas,
                           const std::vector<SpriteTexture>* pages) {
    labels_ = labels;
    atlas_ = atlas;
    pages_ = pages;
    layouts_.clear();
    nodes_.clear();
    live_.clear();
    dirty_ = true;
    missingFaces_ = 0;
    if (!labels_ || !atlas_) return;
    layouts_.resize(labels_->All().size());
    nodes_.assign(labels_->All().size(), nullptr);
    live_.resize(labels_->All().size());
    for (std::size_t i = 0; i < labels_->All().size(); ++i) {
        live_[i].text = labels_->All()[i].text;
        Layout(i);
    }
}

int Label3DRenderer::BindTree(const godot::SceneTree& tree) {
    if (!labels_) return 0;
    tree_ = &tree;
    boundGeneration_ = tree.free_generation();
    int unresolved = 0;
    for (std::size_t i = 0; i < labels_->All().size(); ++i) {
        nodes_[i] = tree.get_node_as<godot::Label3D>(labels_->All()[i].path);
        if (!nodes_[i]) ++unresolved;
    }
    // Force the first Sync to take every node's state.
    for (LiveState& s : live_) s.text.assign(1, '\x01');
    dirty_ = true;
    return unresolved;
}

// Laid out opaque from the label's current text; transparency is applied per
// frame, so a fade never re-runs layout.
void Label3DRenderer::Layout(std::size_t i) {
    assets::Label3D l = labels_->All()[i];
    l.text = live_[i].text;
    l.transparency = 0.0f;
    layouts_[i].clear();
    if (!assets::LayoutLabel3D(*atlas_, l, layouts_[i]) && !l.text.empty()) ++missingFaces_;
}

void Label3DRenderer::Invalidate() {
    for (std::size_t i = 0; i < live_.size() && i < layouts_.size(); ++i) Layout(i);
    dirty_ = true;
}

void Label3DRenderer::Sync() {
    // A script freed nodes: re-resolve by path, so a freed label is dropped.
    if (tree_ && tree_->free_generation() != boundGeneration_) {
        boundGeneration_ = tree_->free_generation();
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (nodes_[i]) nodes_[i] = tree_->get_node_as<godot::Label3D>(labels_->All()[i].path);
            if (!nodes_[i] && live_[i].visible) {
                live_[i].visible = false;
                dirty_ = true;
            }
        }
    }
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const godot::Label3D* n = nodes_[i];
        if (!n) continue;
        LiveState& s = live_[i];
        if (n->get_xl_text() != s.text) {
            s.text = n->get_xl_text();
            Layout(i);
            dirty_ = true;
        }
        const bool visible = n->is_visible_in_tree();
        const float transparency = n->get_transparency();
        const core::Mat4& world = n->get_global_transform();
        const godot::Color c = n->get_modulate();
        const float modulate[4] = {c.r, c.g, c.b, c.a};
        if (visible != s.visible || transparency != s.transparency ||
            std::memcmp(modulate, s.modulate, sizeof(modulate)) != 0 ||
            std::memcmp(world.m, s.world.m, sizeof(world.m)) != 0) {
            s.visible = visible;
            s.transparency = transparency;
            std::memcpy(s.modulate, modulate, sizeof(modulate));
            s.world = world;
            dirty_ = true;
        }
    }
}

void Label3DRenderer::Draw(const core::Mat4& viewProj) {
    if (!Bound() || !shader_.Valid() || !vbo_) return;
    Sync();
    if (dirty_) Rebuild();
    if (uploadedVerts_ == 0) return;

    shader_.Use();
    glUniformMatrix4fv(uMvp_, 1, GL_FALSE, viewProj.m);
    glUniform1i(uTex_, 0);
    glActiveTexture(GL_TEXTURE0);

    // Label3D is alpha-blended and double-sided; it tests depth so the room
    // occludes it, and does not write depth so glyphs never cut each other.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
    glEnableVertexAttribArray(kAttribPosition);
    glVertexAttribPointer(kAttribPosition, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, pos)));
    glEnableVertexAttribArray(kAttribUv);
    glVertexAttribPointer(kAttribUv, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(kAttribColour);
    glVertexAttribPointer(kAttribColour, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, col)));

    for (const PageRun& r : runs_) {
        if (r.page < 0 || r.page >= static_cast<int>(pages_->size())) continue;
        const SpriteTexture& tex = (*pages_)[static_cast<std::size_t>(r.page)];
        if (!tex.Valid()) continue;
        glBindTexture(GL_TEXTURE_2D, tex.Id());
        glDrawArrays(GL_TRIANGLES, r.first, r.count);
    }

    glDisableVertexAttribArray(kAttribPosition);
    glDisableVertexAttribArray(kAttribUv);
    glDisableVertexAttribArray(kAttribColour);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Label3DRenderer::Rebuild() {
    dirty_ = false;
    drawnLabels_ = 0;
    drawnQuads_ = 0;
    verts_.clear();
    runs_.clear();
    std::map<int, std::vector<Vertex>> byPage;
    for (std::size_t i = 0; i < nodes_.size() && i < layouts_.size(); ++i) {
        // PORT: a label with no node in the runtime scene is not drawn.
        if (!nodes_[i] || layouts_[i].empty()) continue;
        const LiveState& s = live_[i];
        if (!s.visible) continue;

        float alpha = 1.0f - s.transparency;
        if (alpha <= 0.0f) continue;
        if (alpha > 1.0f) alpha = 1.0f;

        const float* m = s.world.m;
        auto place = [m](float x, float y, float out[3]) {
            // Column-major: m[col * 4 + row]. z = 0 in label space.
            out[0] = m[0] * x + m[4] * y + m[12];
            out[1] = m[1] * x + m[5] * y + m[13];
            out[2] = m[2] * x + m[6] * y + m[14];
        };

        for (const assets::LabelQuad& q : layouts_[i]) {
            // The layout baked the export's colours. A glyph takes the node's
            // live modulate instead; an outline keeps its outline_modulate.
            const float* rgba = q.outline ? q.rgba : s.modulate;
            uint8_t col[4] = {PackUnit(rgba[0]), PackUnit(rgba[1]), PackUnit(rgba[2]),
                              PackUnit(rgba[3] * alpha)};
            if (col[3] == 0) continue;
            Vertex tl{}, tr{}, br{}, bl{};
            place(q.x0, q.yTop, tl.pos);
            place(q.x1, q.yTop, tr.pos);
            place(q.x1, q.yBottom, br.pos);
            place(q.x0, q.yBottom, bl.pos);
            tl.uv[0] = q.u0; tl.uv[1] = q.v0;
            tr.uv[0] = q.u1; tr.uv[1] = q.v0;
            br.uv[0] = q.u1; br.uv[1] = q.v1;
            bl.uv[0] = q.u0; bl.uv[1] = q.v1;
            for (Vertex* v : {&tl, &tr, &br, &bl}) {
                for (int c = 0; c < 4; ++c) v->col[c] = col[c];
            }
            // Godot's index order: 0 1 2, 0 2 3.
            std::vector<Vertex>& bucket = byPage[q.page];
            bucket.push_back(tl); bucket.push_back(tr); bucket.push_back(br);
            bucket.push_back(tl); bucket.push_back(br); bucket.push_back(bl);
            ++drawnQuads_;
        }
        ++drawnLabels_;
    }
    for (const auto& [page, bucket] : byPage) {
        runs_.push_back({page, static_cast<GLint>(verts_.size()), static_cast<GLsizei>(bucket.size())});
        verts_.insert(verts_.end(), bucket.begin(), bucket.end());
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(Vertex)),
                 verts_.empty() ? nullptr : verts_.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    uploadedVerts_ = static_cast<GLsizei>(verts_.size());
}

}  // namespace render
