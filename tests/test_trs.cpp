// Position / rotation_degrees (YXZ) / scale <-> matrix, pinned against the
// real room. The dealer vanished because only position was seeded from the rest
// matrix; this is the round trip that would have caught it.
#include "assets/room_file.h"
#include "core/trs.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

void ExpectMatNear(const core::Mat4& a, const core::Mat4& b, float eps,
                   const std::string& what) {
    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(a.m[i], b.m[i], eps) << what << " element " << i;
    }
}

core::Mat4 RoundTrip(const core::Mat4& m) {
    float p[3], r[3], s[3];
    core::DecomposeTRS(m, p, r, s);
    return core::ComposeTRS(p, r, s);
}

// True when the basis columns are mutually perpendicular; anything else is
// shear, which no position/rotation/scale can express.
bool Orthogonal(const core::Mat4& m) {
    const float* c0 = &m.m[0];
    const float* c1 = &m.m[4];
    const float* c2 = &m.m[8];
    auto dot = [](const float* a, const float* b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    auto len = [&](const float* a) { return std::sqrt(dot(a, a)); };
    const float l0 = len(c0), l1 = len(c1), l2 = len(c2);
    if (l0 < 1e-6f || l1 < 1e-6f || l2 < 1e-6f) return false;
    return std::fabs(dot(c0, c1)) / (l0 * l1) < 1e-3f &&
           std::fabs(dot(c0, c2)) / (l0 * l2) < 1e-3f &&
           std::fabs(dot(c1, c2)) / (l1 * l2) < 1e-3f;
}

}  // namespace

TEST(Trs, IdentityDecomposesToNothing) {
    float p[3], r[3], s[3];
    core::DecomposeTRS(core::Mat4{}, p, r, s);
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(p[i], 0.0f, 1e-6f);
        EXPECT_NEAR(r[i], 0.0f, 1e-4f);
        EXPECT_NEAR(s[i], 1.0f, 1e-6f);
    }
}

TEST(Trs, ComposesInGodotsYXZOrder) {
    const float p[3] = {1.0f, 2.0f, 3.0f};
    const float r[3] = {30.0f, 45.0f, 60.0f};
    const float s[3] = {2.0f, 3.0f, 4.0f};
    const core::Mat4 m = core::ComposeTRS(p, r, s);
    ExpectMatNear(RoundTrip(m), m, 1e-4f, "YXZ round trip");
}

TEST(Trs, KeepsTheDealersTurnAndScale) {
    // `dealer model parent` rest local, verbatim from scene_graph.json: a
    // half-turn about Y with a slight tilt, at 4.14x scale.
    core::Mat4 m;
    const float v[16] = {-4.135f, -0.489f, -0.239f, 0.0f,
                         -0.499f,  4.138f,  0.167f, 0.0f,
                          0.217f,  0.194f, -4.161f, 0.0f,
                         60.265f,  5.212f, -0.444f, 1.0f};
    for (int i = 0; i < 16; ++i) m.m[i] = v[i];

    float p[3], r[3], s[3];
    core::DecomposeTRS(m, p, r, s);
    EXPECT_NEAR(std::fabs(s[0]), 4.14f, 0.05f);
    EXPECT_NEAR(s[1], 4.14f, 0.05f);
    EXPECT_NEAR(s[2], 4.17f, 0.05f);
    EXPECT_GT(std::fabs(r[1]), 150.0f) << "the half-turn about Y was lost";
    ExpectMatNear(core::ComposeTRS(p, r, s), m, 0.05f, "dealer");
}

TEST(Trs, AMirroredBasisRoundTrips) {
    const float p[3] = {0.0f, 0.0f, 0.0f};
    const float r[3] = {10.0f, 20.0f, 30.0f};
    const float s[3] = {-1.5f, 2.0f, 0.5f};
    const core::Mat4 m = core::ComposeTRS(p, r, s);
    ExpectMatNear(RoundTrip(m), m, 1e-4f, "mirrored");
}

TEST(Trs, EveryUnshearedNodeInTheShippedRoomRoundTrips) {
    assets::Room room;
    const std::string path =
        std::string(BSHT_SOURCE_DIR) + "/assets/baked/full_world.room";
    if (assets::ReadRoomFile(path.c_str(), room) != assets::RoomFileError::None) {
        GTEST_SKIP() << "no baked room at " << path << " -- run bake_room";
    }

    int checked = 0, sheared = 0;
    for (const assets::TransformNode& n : room.Nodes().Nodes()) {
        if (!Orthogonal(n.restLocal)) {
            ++sheared;
            continue;
        }
        const core::Mat4 back = RoundTrip(n.restLocal);
        // Relative to the node's own scale: the room holds 4x-scaled parents.
        float mag = 1.0f;
        for (int i = 0; i < 12; ++i) mag = std::max(mag, std::fabs(n.restLocal.m[i]));
        ExpectMatNear(back, n.restLocal, 2e-3f * mag, n.path);
        ++checked;
    }
    EXPECT_GT(checked, 100) << "the room's node table is unexpectedly small";
    // Sheared bases are why the renderer keeps the rest matrix for anything
    // whose rotation and scale are not animated. Report the count.
    std::printf("trs: %d nodes round-trip, %d sheared kept verbatim\n", checked, sheared);
}

// godot:: stores rotation in RADIANS. The degrees overload exists only for the
// old animator path and must be exactly the radians one after conversion.
TEST(Trs, RadiansAndDegreesComposeTheSameMatrix) {
    const float pos[3] = {1.0f, -2.0f, 3.0f};
    const float rad[3] = {0.0f, 2.78904f, 1.5708f};
    const float scl[3] = {1.0f, 2.0f, 0.5f};
    const float k = 180.0f / 3.14159265358979323846f;
    const float deg[3] = {rad[0] * k, rad[1] * k, rad[2] * k};
    const core::Mat4 a = core::ComposeTRSRad(pos, rad, scl);
    const core::Mat4 b = core::ComposeTRS(pos, deg, scl);
    for (int i = 0; i < 16; ++i) EXPECT_NEAR(a.m[i], b.m[i], 1e-5f) << i;
}
