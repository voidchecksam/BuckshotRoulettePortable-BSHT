// View-frustum culling.
//
// Pure maths in core/, so it is testable headless and usable from both the
// renderer and the offline tools. The camera sits at the table looking at the
// dealer, so much of the room is behind it or off to the sides; batches carry a
// world-space AABB from the bake, so rejecting one is six dot products and no
// pass over vertices.
//
// Planes are extracted with the Gribb-Hartmann method: the rows of the
// view-projection matrix combine directly into the six clip planes, so no
// separate camera basis is needed and the culling agrees with what the GPU
// clips.
#pragma once

#include "core/mat4.h"

#include <cmath>

namespace core {

// The world AABB of a model-space box under an affine transform (Arvo, Graphics
// Gems 1990): per output axis, start at the translation and add the smaller and
// larger product of each matrix entry with the box's two extents. Exact for the
// box's eight corners, so it never cuts one off.
inline void TransformAabb(const Mat4& t, const float lo[3], const float hi[3], float outLo[3],
                          float outHi[3]) {
    for (int i = 0; i < 3; ++i) {
        outLo[i] = outHi[i] = t.m[12 + i];
        for (int j = 0; j < 3; ++j) {
            const float a = t.m[j * 4 + i] * lo[j];
            const float b = t.m[j * 4 + i] * hi[j];
            outLo[i] += a < b ? a : b;
            outHi[i] += a < b ? b : a;
        }
    }
}

struct Plane {
    // ax + by + cz + d, normalised so `d` is a real distance.
    float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;

    float Distance(float x, float y, float z) const {
        return a * x + b * y + c * z + d;
    }
};

class Frustum {
public:
    // Column-major viewProj, matching Mat4 and GL.
    void FromViewProj(const Mat4& vp) {
        const float* m = vp.m;

        // row_i + row_3 gives the near-side plane of each axis, row_3 - row_i
        // the far side. In column-major storage row r element c is m[c*4 + r].
        auto set = [&](int i, float sign, int row) {
            planes_[i].a = m[0 * 4 + 3] + sign * m[0 * 4 + row];
            planes_[i].b = m[1 * 4 + 3] + sign * m[1 * 4 + row];
            planes_[i].c = m[2 * 4 + 3] + sign * m[2 * 4 + row];
            planes_[i].d = m[3 * 4 + 3] + sign * m[3 * 4 + row];
            Normalise(planes_[i]);
        };

        set(0, +1.0f, 0);   // left
        set(1, -1.0f, 0);   // right
        set(2, +1.0f, 1);   // bottom
        set(3, -1.0f, 1);   // top
        set(4, +1.0f, 2);   // near
        set(5, -1.0f, 2);   // far
    }

    // Conservative: returns true if the box might be visible. A false positive
    // costs one wasted draw; a false NEGATIVE punches a hole in the room, so
    // the test is deliberately the cheap inclusive one.
    bool IntersectsAabb(const float lo[3], const float hi[3]) const {
        for (const Plane& p : planes_) {
            // The AABB corner furthest along the plane normal. If even that is
            // behind the plane, every corner is.
            const float x = (p.a >= 0.0f) ? hi[0] : lo[0];
            const float y = (p.b >= 0.0f) ? hi[1] : lo[1];
            const float z = (p.c >= 0.0f) ? hi[2] : lo[2];
            if (p.Distance(x, y, z) < 0.0f) return false;
        }
        return true;
    }

    const Plane& GetPlane(int i) const { return planes_[i]; }

private:
    static void Normalise(Plane& p) {
        const float len = std::sqrt(p.a * p.a + p.b * p.b + p.c * p.c);
        if (len <= 1e-20f) return;
        const float inv = 1.0f / len;
        p.a *= inv;
        p.b *= inv;
        p.c *= inv;
        p.d *= inv;
    }

    Plane planes_[6];
};

}  // namespace core
