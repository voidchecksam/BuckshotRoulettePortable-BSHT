// GPU skinning's headless half: the per-vertex bone stream, the clip-space
// palette, and the skinned programs' sources. The vertex shader's sum
// sum_k w_k * (palette[j_k] * p) is evaluated here on the CPU and must land
// where the CPU skin (assets::SkinPositions) under the prop's MVP lands.
#include "render/skin_gpu_plan.h"

#include "assets/skinning.h"
#include "render/shaders/surface_batched.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using render::SkinVertex;

core::Mat4 Affine(float rx, float ry, float tx, float ty, float tz, float s) {
    core::Mat4 m;
    const float cx = std::cos(rx), sx = std::sin(rx), cy = std::cos(ry), sy = std::sin(ry);
    const float r[9] = {cy, sx * sy, -cx * sy, 0.0f, cx, sx, sy, -sx * cy, cx * cy};   // columns
    for (int c = 0; c < 3; ++c) {
        for (int k = 0; k < 3; ++k) m.m[c * 4 + k] = r[c * 3 + k] * s;
    }
    m.m[12] = tx;
    m.m[13] = ty;
    m.m[14] = tz;
    return m;
}

core::Mat4 Perspective() {
    core::Mat4 p;
    const float f = 1.7f, n = 0.05f, fa = 100.0f;
    p.m[0] = f / 1.76f;
    p.m[5] = f;
    p.m[10] = (fa + n) / (n - fa);
    p.m[11] = -1.0f;
    p.m[14] = 2.0f * fa * n / (n - fa);
    p.m[15] = 0.0f;
    return p;
}

void Clip(const float* m, const float p[3], float out[4]) {
    for (int r = 0; r < 4; ++r) out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
}

assets::Mesh SkinnedMesh() {
    assets::Mesh mesh;
    mesh.positions = {0.1f, 0.2f, 0.3f,  -1.0f, 0.5f, 2.0f,  0.7f, -0.4f, 1.1f,  2.0f, 1.0f, -0.5f};
    // v0: one bone; v1: three bones whose weights sum to 0.8 (normalised);
    // v2: a zero weight and an out-of-range joint (both dropped);
    // v3: no usable weight at all (keeps its bind position).
    mesh.joints = {1, 0, 0, 0,   0, 1, 2, 0,   2, 0, 9, 1,   0, 0, 0, 0};
    mesh.weights = {1.0f, 0, 0, 0,   0.4f, 0.3f, 0.1f, 0,   0.6f, 0.0f, 0.5f, 0.2f,   0, 0, 0, 0};
    return mesh;
}

TEST(SkinGpuPlan, TheShaderSumLandsWhereTheCpuSkinLands) {
    const assets::Mesh mesh = SkinnedMesh();
    const std::vector<core::Mat4> palette{Affine(0.3f, -0.2f, 0.1f, 0.0f, 0.4f, 1.0f),
                                          Affine(-0.5f, 0.7f, -0.3f, 0.2f, 0.0f, 0.9f),
                                          Affine(0.1f, 1.2f, 0.5f, -0.6f, 0.3f, 1.1f)};
    const core::Mat4 viewProj = core::Mul(Perspective(), Affine(0.2f, 0.4f, 0.3f, -1.0f, -6.0f, 1.0f));
    const core::Mat4 model = Affine(0.0f, 2.1f, 1.0f, 0.5f, -0.2f, 4.1f);

    std::vector<SkinVertex> stream;
    ASSERT_TRUE(render::BuildSkinStream(mesh, static_cast<int>(palette.size()), stream));
    ASSERT_EQ(stream.size(), 4u);
    std::vector<float> slots((palette.size() + 1) * 16);
    render::SkinClipPalette(viewProj, model, palette, slots.data());

    std::vector<float> cpu;
    assets::SkinPositions(mesh, palette, cpu);
    const core::Mat4 mvp = core::Mul(viewProj, model);
    for (std::size_t v = 0; v < stream.size(); ++v) {
        float want[4];
        Clip(mvp.m, &cpu[v * 3], want);
        float got[4] = {0, 0, 0, 0};
        for (int k = 0; k < 4; ++k) {
            float c[4];
            Clip(&slots[static_cast<std::size_t>(stream[v].joints[k]) * 16], &mesh.positions[v * 3], c);
            for (int r = 0; r < 4; ++r) got[r] += stream[v].weights[k] * c[r];
        }
        for (int r = 0; r < 4; ++r) EXPECT_NEAR(got[r], want[r], 1e-4f) << "vertex " << v << " row " << r;
    }
}

TEST(SkinGpuPlan, AVertexWithoutUsableWeightsRidesTheRestSlot) {
    std::vector<SkinVertex> stream;
    ASSERT_TRUE(render::BuildSkinStream(SkinnedMesh(), 3, stream));
    EXPECT_FLOAT_EQ(stream[3].joints[0], 3.0f) << "slot binds = the prop's MVP alone";
    EXPECT_FLOAT_EQ(stream[3].weights[0], 1.0f);
    EXPECT_FLOAT_EQ(stream[2].weights[1] + stream[2].weights[2] + stream[2].weights[3], 0.25f)
        << "the out-of-range joint and the zero weight are dropped, the rest normalised";
}

TEST(SkinGpuPlan, RefusesWhatThePaletteCannotHold) {
    std::vector<SkinVertex> stream;
    EXPECT_FALSE(render::BuildSkinStream(SkinnedMesh(), render::kMaxGpuSkinBinds + 1, stream))
        << "the smoker's 25 binds stay on the CPU";
    assets::Mesh rigid = SkinnedMesh();
    rigid.weights.clear();
    EXPECT_FALSE(render::BuildSkinStream(rigid, 3, stream));
    EXPECT_EQ(render::kMaxGpuSkinBinds, render::kPropBatchSlots - 1) << "one slot is the rest pose";
}

TEST(SkinGpuPlan, TheSkinnedBoundsHoldEverySkinnedVertex) {
    const assets::Mesh mesh = SkinnedMesh();
    const std::vector<core::Mat4> palette{Affine(0.3f, -0.2f, 0.1f, 0.0f, 0.4f, 1.0f),
                                          Affine(-0.5f, 0.7f, -0.3f, 2.2f, 0.0f, 0.9f),
                                          Affine(0.1f, 1.2f, 0.5f, -0.6f, -3.3f, 1.1f)};
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        lo[i % 3] = std::min(lo[i % 3], mesh.positions[i]);
        hi[i % 3] = std::max(hi[i % 3], mesh.positions[i]);
    }
    float blo[3], bhi[3];
    render::SkinnedBounds(lo, hi, palette, blo, bhi);
    std::vector<float> skinned;
    assets::SkinPositions(mesh, palette, skinned);
    for (std::size_t i = 0; i < skinned.size(); ++i) {
        EXPECT_GE(skinned[i], blo[i % 3] - 1e-5f) << "component " << i;
        EXPECT_LE(skinned[i], bhi[i % 3] + 1e-5f) << "component " << i;
    }
    for (int a = 0; a < 3; ++a) {
        EXPECT_LE(blo[a], lo[a]) << "the bind box rides along: unweighted vertices stay put";
        EXPECT_GE(bhi[a], hi[a]);
    }
    const std::vector<core::Mat4> rest(3);   // identity
    render::SkinnedBounds(lo, hi, rest, blo, bhi);
    for (int a = 0; a < 3; ++a) {
        EXPECT_FLOAT_EQ(blo[a], lo[a]) << "at rest the bounds are the bind box";
        EXPECT_FLOAT_EQ(bhi[a], hi[a]);
    }
}

TEST(SkinGpuPlan, EachBatchedProgramHasASkinnedVariant) {
    for (const char* src : {render::kBatchedVertexSrc, render::kBatchedDynVertexSrc, render::kBatchedFogVertexSrc}) {
        const std::string skinned = render::SkinnedVertexSource(src);
        ASSERT_FALSE(skinned.empty());
        EXPECT_NE(skinned.find("attribute vec4 a_joints;"), std::string::npos);
        EXPECT_NE(skinned.find("attribute vec4 a_weights;"), std::string::npos);
        EXPECT_NE(skinned.find("a_weights.w"), std::string::npos);
        EXPECT_EQ(skinned.find("a_uv2"), std::string::npos) << "the slot attribute is gone";
        EXPECT_EQ(skinned.find("#if"), std::string::npos) << "vitaGL's translator scans attributes before CG's preprocessor";
        EXPECT_EQ(skinned.rfind("#version 120", 0), 0u);
    }
    EXPECT_TRUE(render::SkinnedVertexSource("#version 120\nvoid main() {}\n").empty()) << "a source without the seam";
}

}  // namespace
