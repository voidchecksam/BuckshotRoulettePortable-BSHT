// GPU skinning, the headless half. A skinned prop's vertices carry up to four
// palette slots and weights in a second, static stream; its palette holds each
// bind's MVP (viewProj x model x bone pose x bind pose) in the batched
// programs' slots, and the vertex shader blends them, so a pose change costs
// a few matrix products instead of re-skinning and re-uploading every vertex.
// Props with more binds than slots keep the CPU path (assets::SkinPositions +
// a position upload per pose).
//
//   std::vector<render::SkinVertex> stream;
//   if (render::BuildSkinStream(mesh, binds, stream)) { /* upload once */ }
//   render::SkinClipPalette(viewProj, model, palette, slots);   // per pose
#pragma once

#include "assets/glb.h"
#include "core/mat4.h"
#include "render/shaders/surface_batched.h"

#include <string>
#include <vector>

namespace render {

// The last slot is the rest pose: the prop's MVP with no bone, for vertices
// the CPU skin would leave at their bind position.
inline constexpr int kMaxGpuSkinBinds = kPropBatchSlots - 1;

struct SkinVertex {
    float joints[4];    // palette slots, as floats (GLES2 attributes)
    float weights[4];   // normalised; unused influences are 0
};

// Per vertex, the influences the CPU skin uses (weight > 0, joint < binds),
// packed first and normalised; none at all -> the rest slot at weight 1.
// False when the mesh has no joints/weights or `binds` exceeds the palette.
bool BuildSkinStream(const assets::Mesh& mesh, int binds, std::vector<SkinVertex>& out);

// slots[16 * j] = viewProj x model x palette[j]; slot palette.size() is
// viewProj x model. `slots` holds (palette.size() + 1) x 16 floats.
void SkinClipPalette(const core::Mat4& viewProj, const core::Mat4& model, const std::vector<core::Mat4>& palette,
                     float* slots);

// A conservative model-space box for the skinned mesh, from its bind-pose
// box: the union of that box and its image under every palette matrix. A
// linear-blend vertex is a convex mix of its bones' images, so it lies inside
// (any skin, CPU or GPU). It lets the renderer cull, and skip skinning, a
// skinned prop the camera cannot see.
void SkinnedBounds(const float lo[3], const float hi[3], const std::vector<core::Mat4>& palette, float outLo[3],
                   float outHi[3]);

// A batched vertex program rewritten to blend four bones: the slot attribute
// becomes a_joints/a_weights, the slot's MVP becomes the weighted sum. Plain
// text, no #ifdef: vitaGL's translator registers every `attribute` it sees
// before CG's preprocessor runs. Empty if the source lacks either seam.
std::string SkinnedVertexSource(const char* batchedSrc);

}  // namespace render
