#include "render/mesh_gpu.h"

#include "render/shader.h"

#include <utility>
#include <vector>

namespace render {

MeshGpu::~MeshGpu() { Destroy(); }

MeshGpu::MeshGpu(MeshGpu&& other) noexcept
    : vbo_(other.vbo_), ibo_(other.ibo_), indexCount_(other.indexCount_), skinVbo_(other.skinVbo_),
      vertexCount_(other.vertexCount_), cpu_(std::move(other.cpu_)) {
    other.vbo_ = 0;
    other.ibo_ = 0;
    other.indexCount_ = 0;
    other.skinVbo_ = 0;
    other.vertexCount_ = 0;
}

MeshGpu& MeshGpu::operator=(MeshGpu&& other) noexcept {
    if (this != &other) {
        Destroy();
        vbo_ = other.vbo_;
        ibo_ = other.ibo_;
        indexCount_ = other.indexCount_;
        skinVbo_ = other.skinVbo_;
        vertexCount_ = other.vertexCount_;
        cpu_ = std::move(other.cpu_);
        other.vbo_ = 0;
        other.ibo_ = 0;
        other.indexCount_ = 0;
        other.skinVbo_ = 0;
        other.vertexCount_ = 0;
    }
    return *this;
}

void MeshGpu::Destroy() {
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (ibo_) { glDeleteBuffers(1, &ibo_); ibo_ = 0; }
    if (skinVbo_) { glDeleteBuffers(1, &skinVbo_); skinVbo_ = 0; }
    indexCount_ = 0;
    vertexCount_ = 0;
    cpu_.clear();
}

void AppendVertices(const assets::Mesh& mesh, std::vector<Vertex>& out, float slot) {
    const int vertexCount = mesh.VertexCount();
    if (vertexCount <= 0) return;
    const bool haveUvs = mesh.HasUvs() &&
                         mesh.uvs.size() >= static_cast<std::size_t>(vertexCount) * 2;
    const bool haveCols = mesh.HasColours() &&
                          mesh.colours.size() >= static_cast<std::size_t>(vertexCount) * 4;
    const bool haveNrms = mesh.normals.size() >= static_cast<std::size_t>(vertexCount) * 3;
    const bool haveUv2 = mesh.uv2.size() >= static_cast<std::size_t>(vertexCount) * 2;

    const std::size_t base = out.size();
    out.resize(base + static_cast<std::size_t>(vertexCount));
    for (int i = 0; i < vertexCount; ++i) {
        Vertex& v = out[base + static_cast<std::size_t>(i)];
        const std::size_t i3 = static_cast<std::size_t>(i) * 3;

        v.pos[0] = mesh.positions[i3 + 0];
        v.pos[1] = mesh.positions[i3 + 1];
        v.pos[2] = mesh.positions[i3 + 2];

        v.nrm[0] = haveNrms ? mesh.normals[i3 + 0] : 0.0f;
        v.nrm[1] = haveNrms ? mesh.normals[i3 + 1] : 0.0f;
        v.nrm[2] = haveNrms ? mesh.normals[i3 + 2] : 1.0f;

        v.uv[0] = haveUvs ? mesh.uvs[static_cast<std::size_t>(i) * 2 + 0] : 0.0f;
        v.uv[1] = haveUvs ? mesh.uvs[static_cast<std::size_t>(i) * 2 + 1] : 0.0f;

        if (slot >= 0.0f) {
            v.uv2[0] = slot;   // prop batching's palette slot (no shader reads uv2 otherwise)
            v.uv2[1] = 0.0f;
        } else {
            v.uv2[0] = haveUv2 ? mesh.uv2[static_cast<std::size_t>(i) * 2 + 0] : 0.0f;
            v.uv2[1] = haveUv2 ? mesh.uv2[static_cast<std::size_t>(i) * 2 + 1] : 0.0f;
        }
        v.dyn[0] = v.dyn[1] = v.dyn[2] = v.dyn[3] = 0;

        if (haveCols) {
            const std::size_t i4 = static_cast<std::size_t>(i) * 4;
            v.col[0] = mesh.colours[i4 + 0];
            v.col[1] = mesh.colours[i4 + 1];
            v.col[2] = mesh.colours[i4 + 2];
            v.col[3] = mesh.colours[i4 + 3];
        } else {
            // Unbaked geometry draws at full brightness rather than black, so a
            // missing bake is visible as "too bright" instead of invisible.
            v.col[0] = v.col[1] = v.col[2] = v.col[3] = 255;
        }
    }
}

void EnableVertexLayout() {
    const GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
    glEnableVertexAttribArray(kAttribPosition);
    glVertexAttribPointer(kAttribPosition, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, pos)));
    glEnableVertexAttribArray(kAttribNormal);
    glVertexAttribPointer(kAttribNormal, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, nrm)));
    glEnableVertexAttribArray(kAttribUv);
    glVertexAttribPointer(kAttribUv, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, uv)));
    // GL_TRUE: bytes are normalised to [0,1] in the shader, so the baked colour
    // arrives as a float colour without a divide in the vertex program.
    glEnableVertexAttribArray(kAttribColour);
    glVertexAttribPointer(kAttribColour, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, col)));
    glEnableVertexAttribArray(kAttribUv2);
    glVertexAttribPointer(kAttribUv2, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, uv2)));
    glEnableVertexAttribArray(kAttribDyn);
    glVertexAttribPointer(kAttribDyn, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, dyn)));
}

// Leaving attribute arrays enabled across draws is a classic source of
// crashes when a later draw has fewer attributes than the previous one.
void DisableVertexLayout() {
    glDisableVertexAttribArray(kAttribPosition);
    glDisableVertexAttribArray(kAttribNormal);
    glDisableVertexAttribArray(kAttribUv);
    glDisableVertexAttribArray(kAttribColour);
    glDisableVertexAttribArray(kAttribUv2);
    glDisableVertexAttribArray(kAttribDyn);
}

bool MeshGpu::Upload(const assets::Mesh& mesh, bool dynamic) {
    Destroy();

    const int vertexCount = mesh.VertexCount();
    if (vertexCount <= 0 || mesh.indices.empty()) return false;

    std::vector<Vertex> verts;
    verts.reserve(static_cast<std::size_t>(vertexCount));
    AppendVertices(mesh, verts);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                 verts.data(), dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    if (dynamic) cpu_ = verts;

    glGenBuffers(1, &ibo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint16_t)),
                 mesh.indices.data(), GL_STATIC_DRAW);

    indexCount_ = static_cast<GLsizei>(mesh.indices.size());
    vertexCount_ = vertexCount;

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return true;
}

bool MeshGpu::UploadSkinStream(const std::vector<SkinVertex>& stream) {
    if (vbo_ == 0 || stream.size() != static_cast<std::size_t>(vertexCount_)) return false;
    if (!skinVbo_) glGenBuffers(1, &skinVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, skinVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(stream.size() * sizeof(SkinVertex)), stream.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

// Each attribute keeps the buffer bound when its pointer was set (GLES2; vitaGL
// stores it per attribute, custom_shaders.c glVertexAttribPointer), so the bone
// stream is set up first and Draw() binds the vertices after it.
void MeshGpu::DrawSkinned() const {
    if (!Valid() || !skinVbo_) return;
    const GLsizei stride = static_cast<GLsizei>(sizeof(SkinVertex));
    glBindBuffer(GL_ARRAY_BUFFER, skinVbo_);
    glEnableVertexAttribArray(kAttribJoints);
    glVertexAttribPointer(kAttribJoints, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(SkinVertex, joints)));
    glEnableVertexAttribArray(kAttribWeights);
    glVertexAttribPointer(kAttribWeights, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(SkinVertex, weights)));
    Draw();
    glDisableVertexAttribArray(kAttribJoints);
    glDisableVertexAttribArray(kAttribWeights);
}

bool MeshGpu::UpdatePositions(const float* positions, std::size_t vertexCount) {
    if (cpu_.empty() || vertexCount != cpu_.size() || vbo_ == 0) return false;
    for (std::size_t i = 0; i < vertexCount; ++i) {
        cpu_[i].pos[0] = positions[i * 3];
        cpu_[i].pos[1] = positions[i * 3 + 1];
        cpu_[i].pos[2] = positions[i * 3 + 2];
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpu_.size() * sizeof(Vertex)), cpu_.data(),
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void MeshGpu::Draw() const {
    if (!Valid()) return;
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    EnableVertexLayout();
    // GL_UNSIGNED_SHORT, never GL_UNSIGNED_INT: the Vita does not report
    // GL_OES_element_index_uint.
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_SHORT, nullptr);
    DisableVertexLayout();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

}  // namespace render
