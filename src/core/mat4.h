// A 4x4 matrix, column-major, matching both glTF and OpenGL.
//
// Column-major means the translation sits at m[12], m[13], m[14], not at m[3],
// m[7], m[11]. Getting that backwards transposes the whole scene, and the
// symptom (geometry in roughly the right place but rotated oddly) is easy to
// misread as a bad export.
//
// Lives in core/ so both assets/ and render/ can use it without either
// depending on the other, and so it stays free of platform headers.
#pragma once

#include <cmath>

namespace core {

struct Mat4 {
    // Identity by default: a node with no transform must compose as a no-op.
    float m[16] = {1, 0, 0, 0,
                   0, 1, 0, 0,
                   0, 0, 1, 0,
                   0, 0, 0, 1};

    float  operator[](int i) const { return m[i]; }
    float& operator[](int i)       { return m[i]; }

    float Tx() const { return m[12]; }
    float Ty() const { return m[13]; }
    float Tz() const { return m[14]; }
};

// The inverse of an affine transform (Transform3D::affine_inverse): the 3x3
// basis inverted by cofactors, then the translation mapped back through it.
inline Mat4 InverseAffine(const Mat4& t) {
    const float a = t.m[0], b = t.m[4], c = t.m[8];
    const float d = t.m[1], e = t.m[5], f = t.m[9];
    const float g = t.m[2], h = t.m[6], i = t.m[10];
    const float co00 = e * i - f * h;
    const float co01 = f * g - d * i;
    const float co02 = d * h - e * g;
    const float det = a * co00 + b * co01 + c * co02;
    const float s = det != 0.0f ? 1.0f / det : 0.0f;
    Mat4 r;
    // rows of the inverse basis, stored column-major
    r.m[0] = co00 * s;          r.m[4] = (c * h - b * i) * s; r.m[8]  = (b * f - c * e) * s;
    r.m[1] = co01 * s;          r.m[5] = (a * i - c * g) * s; r.m[9]  = (c * d - a * f) * s;
    r.m[2] = co02 * s;          r.m[6] = (b * g - a * h) * s; r.m[10] = (a * e - b * d) * s;
    r.m[3] = 0.0f;              r.m[7] = 0.0f;                r.m[11] = 0.0f;
    const float tx = t.m[12], ty = t.m[13], tz = t.m[14];
    r.m[12] = -(r.m[0] * tx + r.m[4] * ty + r.m[8] * tz);
    r.m[13] = -(r.m[1] * tx + r.m[5] * ty + r.m[9] * tz);
    r.m[14] = -(r.m[2] * tx + r.m[6] * ty + r.m[10] * tz);
    r.m[15] = 1.0f;
    return r;
}

// Standard column-major product: applies `b` first, then `a`.
// For a scene graph that means Mul(parentWorld, childLocal).
inline Mat4 Mul(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

// Transforms a point (w = 1), so translation applies.
inline void TransformPoint(const Mat4& t, const float in[3], float out[3]) {
    const float x = in[0], y = in[1], z = in[2];
    out[0] = t.m[0] * x + t.m[4] * y + t.m[8]  * z + t.m[12];
    out[1] = t.m[1] * x + t.m[5] * y + t.m[9]  * z + t.m[13];
    out[2] = t.m[2] * x + t.m[6] * y + t.m[10] * z + t.m[14];
}

// Transforms a direction (w = 0), so translation does not apply. Normals must
// use this: TransformPoint on a normal adds the node's position to it, and the
// lighting goes wrong in a way that looks like bad normals in the source.
inline void TransformDirection(const Mat4& t, const float in[3], float out[3]) {
    const float x = in[0], y = in[1], z = in[2];
    out[0] = t.m[0] * x + t.m[4] * y + t.m[8]  * z;
    out[1] = t.m[1] * x + t.m[5] * y + t.m[9]  * z;
    out[2] = t.m[2] * x + t.m[6] * y + t.m[10] * z;
}

// Right-handed perspective projection, mapping depth to [-1, 1] as GL expects.
// fovYDeg is the FULL vertical field of view, not the half-angle.
inline Mat4 Perspective(float fovYDeg, float aspect, float nearZ, float farZ) {
    Mat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
    const float f = 1.0f / std::tan(fovYDeg * 3.14159265358979323846f / 360.0f);
    r.m[0]  = f / (aspect > 0.0f ? aspect : 1.0f);
    r.m[5]  = f;
    r.m[10] = (farZ + nearZ) / (nearZ - farZ);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * farZ * nearZ) / (nearZ - farZ);
    return r;
}

inline Mat4 Translation(float x, float y, float z) {
    Mat4 r;
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

inline void Normalize3(float v[3]) {
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len <= 1e-20f) return;
    const float inv = 1.0f / len;
    v[0] *= inv;
    v[1] *= inv;
    v[2] *= inv;
}

// Right-handed view matrix. This is the inverse of the camera's world
// transform, so it is built from the transposed basis plus a negated, rotated
// translation, not from the camera transform directly.
inline Mat4 LookAt(const float eye[3], const float target[3], const float up[3]) {
    float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    Normalize3(f);

    float u[3] = {up[0], up[1], up[2]};
    Normalize3(u);

    float s[3] = {f[1] * u[2] - f[2] * u[1],
                  f[2] * u[0] - f[0] * u[2],
                  f[0] * u[1] - f[1] * u[0]};
    Normalize3(s);

    // Re-orthogonalise so a sloppy up vector cannot skew the basis.
    const float un[3] = {s[1] * f[2] - s[2] * f[1],
                         s[2] * f[0] - s[0] * f[2],
                         s[0] * f[1] - s[1] * f[0]};

    Mat4 r;
    r.m[0] = s[0];  r.m[4] = s[1];  r.m[8]  = s[2];
    r.m[1] = un[0]; r.m[5] = un[1]; r.m[9]  = un[2];
    r.m[2] = -f[0]; r.m[6] = -f[1]; r.m[10] = -f[2];
    r.m[3] = 0.0f;  r.m[7] = 0.0f;  r.m[11] = 0.0f;
    r.m[12] = -(s[0]*eye[0]  + s[1]*eye[1]  + s[2]*eye[2]);
    r.m[13] = -(un[0]*eye[0] + un[1]*eye[1] + un[2]*eye[2]);
    r.m[14] =  (f[0]*eye[0]  + f[1]*eye[1]  + f[2]*eye[2]);
    r.m[15] = 1.0f;
    return r;
}

}  // namespace core
