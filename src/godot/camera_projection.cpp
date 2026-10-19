#include "godot/camera_projection.h"

#include <cmath>

namespace godot {
namespace {

constexpr float kCmpEpsilon = 0.00001f;
constexpr float kPi = 3.1415926535897932384626433833f;

float Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vector3 Scale(const Vector3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
Vector3 Add(const Vector3& a, const Vector3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vector3 Sub(const Vector3& a, const Vector3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vector3 Normalized(const Vector3& v) {
    const float l = std::sqrt(Dot(v, v));
    return l == 0.0f ? Vector3{} : Scale(v, 1.0f / l);
}

struct Plane {
    Vector3 normal;
    float d = 0.0f;
    // plane.cpp Plane::normalize
    void normalize() {
        const float l = std::sqrt(Dot(normal, normal));
        if (l == 0.0f) {
            normal = {};
            d = 0.0f;
            return;
        }
        normal = Scale(normal, 1.0f / l);
        d /= l;
    }
};

// plane.cpp Plane::intersect_3
bool Intersect3(const Plane& p0, const Plane& p1, const Plane& p2, Vector3* r) {
    const float denom = Dot(Cross(p0.normal, p1.normal), p2.normal);
    if (std::fabs(denom) < kCmpEpsilon) return false;
    *r = Scale(Add(Add(Scale(Cross(p1.normal, p2.normal), p0.d), Scale(Cross(p2.normal, p0.normal), p1.d)),
                   Scale(Cross(p0.normal, p1.normal), p2.d)),
               1.0f / denom);
    return true;
}

// Projection::set_perspective (projection.cpp:252), flattened column-major as
// Projection::columns: m[c * 4 + r]. Camera3D's keep_aspect defaults to
// KEEP_HEIGHT, so the fov is vertical and not flipped.
void Perspective(float fovy_degrees, float aspect, float z_near, float z_far, float m[16]) {
    const float radians = (fovy_degrees / 2.0f) * kPi / 180.0f;
    const float delta_z = z_far - z_near;
    const float sine = std::sin(radians);
    for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    if (delta_z == 0.0f || sine == 0.0f || aspect == 0.0f) return;
    const float cotangent = std::cos(radians) / sine;
    m[0] = cotangent / aspect;
    m[5] = cotangent;
    m[10] = -(z_far + z_near) / delta_z;
    m[11] = -1.0f;
    m[14] = -2.0f * z_near * z_far / delta_z;
    m[15] = 0.0f;
}

// Projection::get_viewport_half_extents (projection.cpp:428)
Vector2 ViewportHalfExtents(const float m[16]) {
    Plane near_plane{{m[3] + m[2], m[7] + m[6], m[11] + m[10]}, -m[15] - m[14]};
    near_plane.normalize();
    Plane right_plane{{m[3] - m[0], m[7] - m[4], m[11] - m[8]}, -m[15] + m[12]};
    right_plane.normalize();
    Plane top_plane{{m[3] - m[1], m[7] - m[5], m[11] - m[9]}, -m[15] + m[13]};
    top_plane.normalize();
    Vector3 res;
    Intersect3(near_plane, right_plane, top_plane, &res);
    return {res.x, res.y};
}

// Camera3D::get_camera_transform: the global transform, orthonormalized
// (Basis::orthonormalize, Gram-Schmidt on the columns); h_offset and v_offset
// are 0 for every camera in main.tscn.
struct CameraXform {
    Vector3 x, y, z, origin;
    Vector3 xform(const Vector3& v) const {
        return Add(Add(Add(Scale(x, v.x), Scale(y, v.y)), Scale(z, v.z)), origin);
    }
    Vector3 basis_xform(const Vector3& v) const { return Add(Add(Scale(x, v.x), Scale(y, v.y)), Scale(z, v.z)); }
};

CameraXform CameraTransform(const Camera3D& camera) {
    const core::Mat4 g = camera.get_camera_transform();   // camera_3d.cpp:166, offsets included
    Vector3 x{g.m[0], g.m[1], g.m[2]};
    Vector3 y{g.m[4], g.m[5], g.m[6]};
    Vector3 z{g.m[8], g.m[9], g.m[10]};
    x = Normalized(x);
    y = Normalized(Sub(y, Scale(x, Dot(x, y))));
    z = Normalized(Sub(Sub(z, Scale(x, Dot(x, z))), Scale(y, Dot(y, z))));
    return {x, y, z, {g.m[12], g.m[13], g.m[14]}};
}

}  // namespace

Vector3 project_ray_origin(const Camera3D& camera, Vector2 /*screen*/, Vector2 viewport) {
    if (viewport.y == 0.0f) return {};
    return CameraTransform(camera).origin;
}

Vector3 project_position(const Camera3D& camera, Vector2 screen, float z_depth, Vector2 viewport) {
    const core::Mat4 g = camera.get_camera_transform();   // camera_3d.cpp:166, offsets included
    if (z_depth == 0.0f) return {g.m[12], g.m[13], g.m[14]};
    float m[16];
    Perspective(camera.get_fov(), viewport.x / viewport.y, z_depth, camera.get_far(), m);
    const Vector2 vp_he = ViewportHalfExtents(m);
    const float px = ((screen.x / viewport.x) * 2.0f - 1.0f) * vp_he.x;
    const float py = ((1.0f - (screen.y / viewport.y)) * 2.0f - 1.0f) * vp_he.y;
    return CameraTransform(camera).xform({px, py, -z_depth});
}

}  // namespace godot
