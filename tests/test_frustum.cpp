// Frustum culling.
//
// The asymmetry matters: a false positive costs one wasted draw call, a false
// NEGATIVE punches a hole in the room. So the tests care much more about things
// wrongly rejected than about things wrongly kept.
#include <gtest/gtest.h>

#include "core/frustum.h"
#include "core/mat4.h"

using namespace core;

namespace {

// Camera at the origin looking down -Z, which is where GL's default view points.
Frustum LookingDownNegZ(float fovY = 60.0f, float aspect = 1.6f,
                        float nearZ = 0.1f, float farZ = 100.0f) {
    const float eye[3] = {0.0f, 0.0f, 0.0f};
    const float target[3] = {0.0f, 0.0f, -1.0f};
    const float up[3] = {0.0f, 1.0f, 0.0f};
    const Mat4 vp = Mul(Perspective(fovY, aspect, nearZ, farZ),
                        LookAt(eye, target, up));
    Frustum f;
    f.FromViewProj(vp);
    return f;
}

// A small box centred on a point.
void BoxAt(float x, float y, float z, float half, float lo[3], float hi[3]) {
    lo[0] = x - half; lo[1] = y - half; lo[2] = z - half;
    hi[0] = x + half; hi[1] = y + half; hi[2] = z + half;
}

}  // namespace

TEST(Frustum, KeepsABoxDirectlyAhead) {
    const Frustum f = LookingDownNegZ();
    float lo[3], hi[3];
    BoxAt(0.0f, 0.0f, -10.0f, 1.0f, lo, hi);
    EXPECT_TRUE(f.IntersectsAabb(lo, hi));
}

TEST(Frustum, RejectsABoxDirectlyBehindTheCamera) {
    // The single biggest win at the table: the camera faces the dealer and a
    // large share of the room is behind it.
    const Frustum f = LookingDownNegZ();
    float lo[3], hi[3];
    BoxAt(0.0f, 0.0f, 40.0f, 1.0f, lo, hi);
    EXPECT_FALSE(f.IntersectsAabb(lo, hi));
}

TEST(Frustum, RejectsABoxFarOffToTheSide) {
    const Frustum f = LookingDownNegZ();
    float lo[3], hi[3];
    BoxAt(500.0f, 0.0f, -10.0f, 1.0f, lo, hi);
    EXPECT_FALSE(f.IntersectsAabb(lo, hi));
}

TEST(Frustum, RejectsABoxBeyondTheFarPlane) {
    const Frustum f = LookingDownNegZ(60.0f, 1.6f, 0.1f, 100.0f);
    float lo[3], hi[3];
    BoxAt(0.0f, 0.0f, -5000.0f, 1.0f, lo, hi);
    EXPECT_FALSE(f.IntersectsAabb(lo, hi));
}

TEST(Frustum, KeepsABoxStraddlingTheEdge) {
    // A box only partly inside must be KEPT. Rejecting it would clip geometry
    // the player can actually see at the screen border.
    const Frustum f = LookingDownNegZ();
    float lo[3], hi[3];
    // Centre it far to the side but make it enormous, so it reaches into view.
    BoxAt(60.0f, 0.0f, -10.0f, 58.0f, lo, hi);
    EXPECT_TRUE(f.IntersectsAabb(lo, hi));
}

TEST(Frustum, KeepsABoxEnclosingTheCamera) {
    // The room's walls enclose the viewer. A box containing the camera must
    // never be culled: that would delete the room the player is standing in.
    const Frustum f = LookingDownNegZ();
    const float lo[3] = {-50.0f, -50.0f, -50.0f};
    const float hi[3] = { 50.0f,  50.0f,  50.0f};
    EXPECT_TRUE(f.IntersectsAabb(lo, hi));
}

TEST(Frustum, KeepsADegenerateZeroSizeBoxInView) {
    const Frustum f = LookingDownNegZ();
    const float p[3] = {0.0f, 0.0f, -10.0f};
    EXPECT_TRUE(f.IntersectsAabb(p, p));
}

TEST(Frustum, PlanesAreNormalised) {
    // Un-normalised planes still cull correctly by sign, but the distances are
    // meaningless, which quietly breaks anything later that wants a real
    // distance from them.
    const Frustum f = LookingDownNegZ();
    for (int i = 0; i < 6; ++i) {
        const Plane& p = f.GetPlane(i);
        const float len = std::sqrt(p.a * p.a + p.b * p.b + p.c * p.c);
        EXPECT_NEAR(len, 1.0f, 1e-4f) << "plane " << i;
    }
}

TEST(Frustum, AgreesWithTheProjectionItWasBuiltFrom) {
    // Cross-check against the actual clip-space test rather than against
    // another hand-rolled frustum: a point inside the frustum must land inside
    // the [-w, w] clip volume.
    const float eye[3] = {3.0f, 2.0f, 8.0f};
    const float target[3] = {0.0f, 0.0f, 0.0f};
    const float up[3] = {0.0f, 1.0f, 0.0f};
    const Mat4 vp = Mul(Perspective(55.0f, 1.76f, 0.1f, 300.0f),
                        LookAt(eye, target, up));
    Frustum f;
    f.FromViewProj(vp);

    auto insideClip = [&vp](float x, float y, float z) {
        const float cx = vp.m[0]*x + vp.m[4]*y + vp.m[8]*z  + vp.m[12];
        const float cy = vp.m[1]*x + vp.m[5]*y + vp.m[9]*z  + vp.m[13];
        const float cz = vp.m[2]*x + vp.m[6]*y + vp.m[10]*z + vp.m[14];
        const float cw = vp.m[3]*x + vp.m[7]*y + vp.m[11]*z + vp.m[15];
        return cw > 0.0f && std::abs(cx) <= cw && std::abs(cy) <= cw &&
               std::abs(cz) <= cw;
    };

    int checked = 0;
    for (int i = -6; i <= 6; ++i) {
        for (int j = -6; j <= 6; ++j) {
            for (int k = -6; k <= 6; ++k) {
                const float x = static_cast<float>(i) * 3.0f;
                const float y = static_cast<float>(j) * 3.0f;
                const float z = static_cast<float>(k) * 3.0f;
                const float p[3] = {x, y, z};
                if (insideClip(x, y, z)) {
                    // Anything the GPU would keep, the frustum must keep.
                    ASSERT_TRUE(f.IntersectsAabb(p, p))
                        << "culled a point the projection accepts: "
                        << x << "," << y << "," << z;
                    ++checked;
                }
            }
        }
    }
    EXPECT_GT(checked, 20) << "test grid never landed inside the frustum";
}

// A prop's bounds follow its node. The world box at the rest pose would cull
// an item compartment an animation had moved while it is on screen (the
// player's grids would vanish when the camera faces the dealer).
TEST(TransformAabb, MovesAModelSpaceBoxToWhereTheNodeIsNow) {
    const float lo[3] = {-1.0f, -1.0f, -1.0f};
    const float hi[3] = {1.0f, 1.0f, 1.0f};
    float outLo[3];
    float outHi[3];
    TransformAabb(Translation(10.0f, -5.0f, 2.0f), lo, hi, outLo, outHi);
    EXPECT_FLOAT_EQ(outLo[0], 9.0f);
    EXPECT_FLOAT_EQ(outHi[0], 11.0f);
    EXPECT_FLOAT_EQ(outLo[1], -6.0f);
    EXPECT_FLOAT_EQ(outHi[1], -4.0f);
    EXPECT_FLOAT_EQ(outLo[2], 1.0f);
    EXPECT_FLOAT_EQ(outHi[2], 3.0f);
}

// Rotated 45 degrees about Y, a unit box's world extent grows to sqrt(2): the
// world box must contain every corner, never cut one off.
TEST(TransformAabb, ARotatedBoxStillContainsEveryCorner) {
    const float c = std::cos(0.7853981634f);
    const float s = std::sin(0.7853981634f);
    Mat4 rotY;   // column-major, as mat4.h
    rotY.m[0] = c;   rotY.m[2] = -s;
    rotY.m[8] = s;   rotY.m[10] = c;
    const float lo[3] = {-1.0f, -1.0f, -1.0f};
    const float hi[3] = {1.0f, 1.0f, 1.0f};
    float outLo[3];
    float outHi[3];
    TransformAabb(rotY, lo, hi, outLo, outHi);
    EXPECT_NEAR(outHi[0], std::sqrt(2.0f), 1e-5f);
    EXPECT_NEAR(outLo[2], -std::sqrt(2.0f), 1e-5f);
    EXPECT_NEAR(outHi[1], 1.0f, 1e-5f);
}
