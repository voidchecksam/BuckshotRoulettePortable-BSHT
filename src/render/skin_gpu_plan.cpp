#include "render/skin_gpu_plan.h"

#include <algorithm>
#include <cstddef>

namespace render {

bool BuildSkinStream(const assets::Mesh& mesh, int binds, std::vector<SkinVertex>& out) {
    const std::size_t n = mesh.positions.size() / 3;
    if (binds < 0 || binds > kMaxGpuSkinBinds || n == 0 || mesh.joints.size() != n * 4 ||
        mesh.weights.size() != n * 4) {
        return false;
    }
    out.assign(n, SkinVertex{});
    for (std::size_t v = 0; v < n; ++v) {
        SkinVertex& sv = out[v];
        int kept = 0;
        float total = 0.0f;
        for (std::size_t k = 0; k < 4; ++k) {
            const float w = mesh.weights[v * 4 + k];
            const int j = mesh.joints[v * 4 + k];
            if (w <= 0.0f || j >= binds) continue;   // as assets::SkinPositions
            sv.joints[kept] = static_cast<float>(j);
            sv.weights[kept] = w;
            total += w;
            ++kept;
        }
        if (total <= 0.0f) {
            sv = SkinVertex{};
            sv.joints[0] = static_cast<float>(binds);   // the rest slot
            sv.weights[0] = 1.0f;
            continue;
        }
        for (int k = 0; k < kept; ++k) sv.weights[k] /= total;
    }
    return true;
}

void SkinClipPalette(const core::Mat4& viewProj, const core::Mat4& model, const std::vector<core::Mat4>& palette,
                     float* slots) {
    const core::Mat4 mvp = core::Mul(viewProj, model);
    for (std::size_t j = 0; j < palette.size(); ++j) {
        const core::Mat4 m = core::Mul(mvp, palette[j]);
        std::copy_n(m.m, 16, slots + j * 16);
    }
    std::copy_n(mvp.m, 16, slots + palette.size() * 16);
}

void SkinnedBounds(const float lo[3], const float hi[3], const std::vector<core::Mat4>& palette, float outLo[3],
                   float outHi[3]) {
    std::copy_n(lo, 3, outLo);
    std::copy_n(hi, 3, outHi);
    for (const core::Mat4& m : palette) {
        for (int corner = 0; corner < 8; ++corner) {
            const float p[3] = {(corner & 1) ? hi[0] : lo[0], (corner & 2) ? hi[1] : lo[1],
                                (corner & 4) ? hi[2] : lo[2]};
            float t[3];
            core::TransformPoint(m, p, t);
            for (int a = 0; a < 3; ++a) {
                outLo[a] = std::min(outLo[a], t[a]);
                outHi[a] = std::max(outHi[a], t[a]);
            }
        }
    }
}

namespace {

// The batched programs' seams (render/shaders/surface_batched.h).
constexpr const char* kSlotAttribute = "attribute vec2 a_uv2;";
constexpr const char* kSlotBlock =
    "    int s = int(a_uv2.x + 0.5);\n"
    "    mat4 mvp = mat4(u_mvp_cols[4 * s], u_mvp_cols[4 * s + 1], u_mvp_cols[4 * s + 2], u_mvp_cols[4 * s + 3]);\n"
    "    vec4 clip = mvp * vec4(a_pos, 1.0);\n";

// Plain products: vitaGL rewrites every `*` as vglMul and is not a full parser
// (external/src/vitaGL/source/utils/glsl_utils.c, glsl_inject_mul). It splits a sum
// of products at each `+` correctly; no products nested in parentheses. Its
// header also does `#define s x` and `#define t y`: no locals named x or y.
constexpr const char* kSkinAttributes = "attribute vec4 a_joints;\nattribute vec4 a_weights;";
constexpr const char* kSkinBlock =
    "    int s = 0;   // one prop: its colour scale is slot 0's\n"
    "    vec4 sk_p = vec4(a_pos, 1.0);\n"
    "    int sk_j0 = int(a_joints.x + 0.5);\n"
    "    int sk_j1 = int(a_joints.y + 0.5);\n"
    "    int sk_j2 = int(a_joints.z + 0.5);\n"
    "    int sk_j3 = int(a_joints.w + 0.5);\n"
    "    mat4 sk_m0 = mat4(u_mvp_cols[4 * sk_j0], u_mvp_cols[4 * sk_j0 + 1], u_mvp_cols[4 * sk_j0 + 2], "
    "u_mvp_cols[4 * sk_j0 + 3]);\n"
    "    mat4 sk_m1 = mat4(u_mvp_cols[4 * sk_j1], u_mvp_cols[4 * sk_j1 + 1], u_mvp_cols[4 * sk_j1 + 2], "
    "u_mvp_cols[4 * sk_j1 + 3]);\n"
    "    mat4 sk_m2 = mat4(u_mvp_cols[4 * sk_j2], u_mvp_cols[4 * sk_j2 + 1], u_mvp_cols[4 * sk_j2 + 2], "
    "u_mvp_cols[4 * sk_j2 + 3]);\n"
    "    mat4 sk_m3 = mat4(u_mvp_cols[4 * sk_j3], u_mvp_cols[4 * sk_j3 + 1], u_mvp_cols[4 * sk_j3 + 2], "
    "u_mvp_cols[4 * sk_j3 + 3]);\n"
    "    vec4 sk_c0 = sk_m0 * sk_p;\n"
    "    vec4 sk_c1 = sk_m1 * sk_p;\n"
    "    vec4 sk_c2 = sk_m2 * sk_p;\n"
    "    vec4 sk_c3 = sk_m3 * sk_p;\n"
    "    vec4 clip = sk_c0 * a_weights.x + sk_c1 * a_weights.y + sk_c2 * a_weights.z + sk_c3 * a_weights.w;\n";

}  // namespace

std::string SkinnedVertexSource(const char* batchedSrc) {
    std::string src(batchedSrc ? batchedSrc : "");
    const std::size_t attr = src.find(kSlotAttribute);
    if (attr == std::string::npos) return {};
    const std::size_t attrEnd = src.find('\n', attr);   // the line, with its comment
    src.replace(attr, (attrEnd == std::string::npos ? src.size() : attrEnd) - attr, kSkinAttributes);
    const std::size_t block = src.find(kSlotBlock);
    if (block == std::string::npos) return {};
    src.replace(block, std::char_traits<char>::length(kSlotBlock), kSkinBlock);
    return src;
}

}  // namespace render
