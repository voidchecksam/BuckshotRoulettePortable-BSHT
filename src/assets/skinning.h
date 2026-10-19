// Linear blend skinning on the CPU: Godot's skin (scene shader SKIN: up to 4
// bones per vertex, weights summed) applied to the bind-pose positions.
// `palette[j]` is bone_global_pose(bind j's bone) * bind_pose(j)
// (skeleton_3d.cpp:296). Headless, so it is unit-tested; the renderer re-runs
// it whenever a skeleton's pose changes and re-uploads the positions.
#pragma once

#include "assets/glb.h"
#include "core/mat4.h"

#include <vector>

namespace assets {

// Writes 3 floats per vertex. A vertex whose weights sum to 0, or whose
// joint is out of range, keeps its bind position.
inline void SkinPositions(const Mesh& mesh, const std::vector<core::Mat4>& palette, std::vector<float>& out) {
    const std::size_t n = mesh.positions.size() / 3;
    out.resize(n * 3);
    for (std::size_t v = 0; v < n; ++v) {
        const float* p = &mesh.positions[v * 3];
        float acc[3] = {0.0f, 0.0f, 0.0f};
        float total = 0.0f;
        for (int k = 0; k < 4; ++k) {
            const float w = mesh.weights[v * 4 + static_cast<std::size_t>(k)];
            const std::size_t j = mesh.joints[v * 4 + static_cast<std::size_t>(k)];
            if (w <= 0.0f || j >= palette.size()) continue;
            float t[3];
            core::TransformPoint(palette[j], p, t);
            for (int a = 0; a < 3; ++a) acc[a] += w * t[a];
            total += w;
        }
        for (int a = 0; a < 3; ++a) out[v * 3 + static_cast<std::size_t>(a)] = total > 0.0f ? acc[a] / total : p[a];
    }
}

}  // namespace assets
