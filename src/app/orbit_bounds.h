// The room's axis-aligned bounds: the debug orbit view circles them, and the
// far plane is sized from them. Maths, not platform code, so it lives here
// rather than in main_desktop.cpp.
#pragma once

#include <cmath>

namespace app {

struct Bounds {
    float lo[3] = { 1e30f,  1e30f,  1e30f};
    float hi[3] = {-1e30f, -1e30f, -1e30f};

    void Add(const float p[3]) {
        for (int i = 0; i < 3; ++i) {
            if (p[i] < lo[i]) lo[i] = p[i];
            if (p[i] > hi[i]) hi[i] = p[i];
        }
    }
    bool Valid() const { return lo[0] <= hi[0]; }
    void Centre(float out[3]) const {
        for (int i = 0; i < 3; ++i) out[i] = (lo[i] + hi[i]) * 0.5f;
    }
    // Half the bounding-box diagonal.
    float Radius() const {
        float sum = 0.0f;
        for (int i = 0; i < 3; ++i) {
            const float half = (hi[i] - lo[i]) * 0.5f;
            sum += half * half;
        }
        const float r = std::sqrt(sum);
        return r > 0.0f ? r : 1.0f;
    }
};

}  // namespace app
