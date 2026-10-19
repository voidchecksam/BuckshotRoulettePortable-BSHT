// A mesh resident on the GPU.
//
// Interleaved into one VBO plus a 16-bit IBO. No vertex array objects: VAOs are
// not in GL 2.1 core and not in GLES2, so attribute state is set per draw. The
// Vita reports GL_OES_vertex_array_object, but relying on an extension for the
// basic draw path would leave the renderer with no fallback.
//
// Indices are 16-bit because the Vita does not report
// GL_OES_element_index_uint. See assets/glb.h.
#pragma once

#include <vector>

#include "platform/gl_compat.h"

#include "assets/glb.h"
#include "render/skin_gpu_plan.h"

namespace render {

// One interleaved vertex. Position and normal stay float; UV stays float; the
// baked colour is 4 unsigned bytes, which is a quarter the bandwidth of floats
// and is what the vertex fetch on SGX prefers.
struct Vertex {
    float   pos[3];
    float   nrm[3];
    float   uv[2];
    uint8_t col[4];
    // The surface page's UV set, and the animated lights' baked layers (one
    // light per channel, scaled by live energy in the shader).
    float   uv2[2];
    uint8_t dyn[4];
};

// `mesh`'s vertices appended in the GPU layout. `slot` >= 0 is prop batching's
// palette slot, written to uv2.x; no shader reads uv2 otherwise.
void AppendVertices(const assets::Mesh& mesh, std::vector<Vertex>& out, float slot = -1.0f);
// The Vertex layout's attribute arrays, for the bound VBO; and off again.
void EnableVertexLayout();
void DisableVertexLayout();

class MeshGpu {
public:
    MeshGpu() = default;
    ~MeshGpu();

    MeshGpu(const MeshGpu&) = delete;
    MeshGpu& operator=(const MeshGpu&) = delete;
    MeshGpu(MeshGpu&& other) noexcept;
    MeshGpu& operator=(MeshGpu&& other) noexcept;

    // `dynamic`: keep a CPU copy so UpdatePositions can re-upload (skinning;
    // the hardware probe measured per-frame VBO re-upload holding 60).
    bool Upload(const assets::Mesh& mesh, bool dynamic = false);
    // 3 floats per vertex, the same count as uploaded. False if not dynamic.
    bool UpdatePositions(const float* positions, std::size_t vertexCount);
    void Destroy();

    // Binds buffers, enables attributes, draws, then disables them again.
    // Leaving attribute arrays enabled across draws is a classic source of
    // crashes when a later draw has fewer attributes than the previous one.
    void Draw() const;

    // GPU skinning: the bone stream (render/skin_gpu_plan.h) in a second,
    // static buffer beside the vertices. False if its count differs from the
    // vertices'. DrawSkinned is Draw() with it bound to a_joints / a_weights.
    bool UploadSkinStream(const std::vector<SkinVertex>& stream);
    void DrawSkinned() const;

    bool Valid() const { return vbo_ != 0 && ibo_ != 0 && indexCount_ > 0; }
    GLsizei IndexCount() const { return indexCount_; }
    int TriangleCount() const { return static_cast<int>(indexCount_ / 3); }

private:
    GLuint  vbo_ = 0;
    GLuint  ibo_ = 0;
    GLsizei indexCount_ = 0;
    GLuint  skinVbo_ = 0;       // GPU-skinned meshes only
    int     vertexCount_ = 0;
    std::vector<Vertex> cpu_;   // dynamic meshes only
};

}  // namespace render
