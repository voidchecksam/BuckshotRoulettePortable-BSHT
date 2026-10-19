#include "assets/probe_grid.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace assets {

bool ProbeGrid::Sample(const float pos[3], float out[3]) const {
    if (Empty() || rgb.size() < static_cast<std::size_t>(dims[0]) * dims[1] * dims[2] * 3) return false;
    int i0[3];
    int i1[3];
    float f[3];
    for (int a = 0; a < 3; ++a) {
        const float maxIndex = static_cast<float>(dims[a] - 1);
        const float g = std::clamp((pos[a] - origin[a]) / step, 0.0f, maxIndex);
        const float fl = std::floor(g);
        i0[a] = static_cast<int>(fl);
        i1[a] = std::min(i0[a] + 1, dims[a] - 1);
        f[a] = g - fl;
    }
    auto at = [&](int x, int y, int z) {
        return &rgb[(static_cast<std::size_t>(z) * dims[1] + y) * dims[0] * 3 + static_cast<std::size_t>(x) * 3];
    };
    for (int c = 0; c < 3; ++c) {
        const float c00 = at(i0[0], i0[1], i0[2])[c] * (1 - f[0]) + at(i1[0], i0[1], i0[2])[c] * f[0];
        const float c10 = at(i0[0], i1[1], i0[2])[c] * (1 - f[0]) + at(i1[0], i1[1], i0[2])[c] * f[0];
        const float c01 = at(i0[0], i0[1], i1[2])[c] * (1 - f[0]) + at(i1[0], i0[1], i1[2])[c] * f[0];
        const float c11 = at(i0[0], i1[1], i1[2])[c] * (1 - f[0]) + at(i1[0], i1[1], i1[2])[c] * f[0];
        const float c0 = c00 * (1 - f[1]) + c10 * f[1];
        const float c1 = c01 * (1 - f[1]) + c11 * f[1];
        out[c] = c0 * (1 - f[2]) + c1 * f[2];
    }
    return true;
}

bool ProbeGrid::Contains(const float pos[3]) const {
    if (Empty()) return false;
    for (int a = 0; a < 3; ++a) {
        const float g = (pos[a] - origin[a]) / step;
        if (g < 0.0f || g > static_cast<float>(dims[a] - 1)) return false;
    }
    return true;
}

void OffstageRatio(const ProbeGrid& grid, const float pivotNow[3], const float bakePivot[3], float out[3]) {
    if (!grid.Empty() && !grid.Contains(pivotNow)) {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    PropRatio(grid, pivotNow, bakePivot, out);
    for (int c = 0; c < 3; ++c) out[c] = std::min(out[c], 1.0f);
}

void PropRatio(const ProbeGrid& grid, const float pivotNow[3], const float bakePivot[3], float out[3]) {
    out[0] = out[1] = out[2] = 1.0f;
    float now[3];
    float baked[3];
    if (!grid.Sample(pivotNow, now) || !grid.Sample(bakePivot, baked)) return;
    for (int c = 0; c < 3; ++c) {
        if (baked[c] < kMinProbeLight) continue;
        out[c] = std::clamp(now[c] / baked[c], 0.0f, kMaxPropRatio);
    }
}

}  // namespace assets
