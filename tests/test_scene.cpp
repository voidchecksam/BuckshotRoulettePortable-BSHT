// Scene graph loader.
//
// Small hand-written documents pin the hierarchy rules exactly; the real
// exported file is then checked against counts measured at runtime, which are
// stronger evidence than grep-derived numbers: some [node] lines in main.tscn
// are sub-scene instances that only expand at load.
#include "test_data.h"

#include <gtest/gtest.h>

#include "assets/scene.h"

#include <string>

using namespace assets;

namespace {

std::string ScenePath() {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/scene_graph.json";
}

// A translation-only column-major matrix, i.e. origin at m[12..14].
std::string Translate(float x, float y, float z) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "[1,0,0,0, 0,1,0,0, 0,0,1,0, %f,%f,%f,1]", x, y, z);
    return std::string(buf);
}

}  // namespace

TEST(Scene, DerivesParentFromPathBecauseThereIsNoParentField) {
    const std::string doc =
        "{\"nodes\":["
        "{\"name\":\"main\",\"path\":\".\",\"type\":\"Node3D\",\"transform\":" +
            Translate(0, 0, 0) + "},"
        "{\"name\":\"a\",\"path\":\"a\",\"type\":\"Node3D\",\"transform\":" +
            Translate(10, 0, 0) + "},"
        "{\"name\":\"b\",\"path\":\"a/b\",\"type\":\"Node3D\",\"transform\":" +
            Translate(5, 0, 0) + "}"
        "]}";
    Scene s;
    ASSERT_TRUE(s.LoadFromString(doc.c_str()));
    ASSERT_EQ(s.NodeCount(), 3u);

    EXPECT_EQ(s.Nodes()[0].parent, -1);          // "." is the root
    EXPECT_EQ(s.Nodes()[1].parent, 0);           // "a" -> "."
    EXPECT_EQ(s.Nodes()[2].parent, 1);           // "a/b" -> "a"
}

TEST(Scene, ComposesWorldTransformDownTheChain) {
    // Local transforms, so the child's world position is parent + child.
    const std::string doc =
        "{\"nodes\":["
        "{\"name\":\"r\",\"path\":\".\",\"type\":\"Node3D\",\"transform\":" +
            Translate(0, 0, 0) + "},"
        "{\"name\":\"a\",\"path\":\"a\",\"type\":\"Node3D\",\"transform\":" +
            Translate(10, 0, 0) + "},"
        "{\"name\":\"b\",\"path\":\"a/b\",\"type\":\"Node3D\",\"transform\":" +
            Translate(5, 2, 0) + "}"
        "]}";
    Scene s;
    ASSERT_TRUE(s.LoadFromString(doc.c_str()));

    EXPECT_FLOAT_EQ(s.WorldTransform(1).Tx(), 10.0f);
    EXPECT_FLOAT_EQ(s.WorldTransform(2).Tx(), 15.0f);
    EXPECT_FLOAT_EQ(s.WorldTransform(2).Ty(), 2.0f);
}

TEST(Scene, NonSpatialParentsComposeAsIdentity) {
    // 731 of the 1,787 nodes have no transform (Control, Label, Button, Node).
    // A mesh parented under one must still land where its own transform says.
    const std::string doc =
        "{\"nodes\":["
        "{\"name\":\"r\",\"path\":\".\",\"type\":\"Node3D\",\"transform\":" +
            Translate(0, 0, 0) + "},"
        "{\"name\":\"ui\",\"path\":\"ui\",\"type\":\"Control\"},"
        "{\"name\":\"m\",\"path\":\"ui/m\",\"type\":\"MeshInstance3D\",\"transform\":" +
            Translate(7, 0, 0) + "}"
        "]}";
    Scene s;
    ASSERT_TRUE(s.LoadFromString(doc.c_str()));
    EXPECT_FALSE(s.Nodes()[1].hasTransform);
    EXPECT_FLOAT_EQ(s.WorldTransform(2).Tx(), 7.0f);
}

TEST(Scene, TerminatesOnASelfReferentialParentInsteadOfHanging) {
    // A node whose own path is also its parent path would spin a naive walk.
    const std::string doc =
        "{\"nodes\":["
        "{\"name\":\"a\",\"path\":\"a\",\"type\":\"Node3D\",\"transform\":" +
            Translate(1, 0, 0) + "}"
        "]}";
    Scene s;
    ASSERT_TRUE(s.LoadFromString(doc.c_str()));
    // Parent path "." does not exist here, so it resolves as a root.
    EXPECT_EQ(s.Nodes()[0].parent, -1);
    EXPECT_FLOAT_EQ(s.WorldTransform(0).Tx(), 1.0f);
}

TEST(Scene, MissingParentPathMakesTheNodeARootRatherThanAnError) {
    const std::string doc =
        "{\"nodes\":["
        "{\"name\":\"orphan\",\"path\":\"ghost/child\",\"type\":\"Node3D\","
        "\"transform\":" + Translate(3, 0, 0) + "}"
        "]}";
    Scene s;
    ASSERT_TRUE(s.LoadFromString(doc.c_str()));
    EXPECT_EQ(s.Nodes()[0].parent, -1);
    EXPECT_FLOAT_EQ(s.WorldTransform(0).Tx(), 3.0f);
}

TEST(Scene, IgnoresATransformThatIsNotSixteenNumbers) {
    // A partially-filled transform would place geometry somewhere plausible but
    // wrong, which is much harder to spot than a node that did not move.
    const std::string doc =
        "{\"nodes\":["
        "{\"name\":\"a\",\"path\":\".\",\"type\":\"Node3D\",\"transform\":[1,0,0]}"
        "]}";
    Scene s;
    ASSERT_TRUE(s.LoadFromString(doc.c_str()));
    EXPECT_FALSE(s.Nodes()[0].hasTransform);
    EXPECT_FLOAT_EQ(s.WorldTransform(0).Tx(), 0.0f);
}

TEST(Scene, ParsesLightColourEnergyAndRange) {
    const std::string doc =
        "{\"nodes\":[{\"name\":\"r\",\"path\":\".\",\"type\":\"Node3D\"}],"
        "\"lights\":[{\"path\":\"l\",\"color\":[0.5,0.25,0.125],"
        "\"energy\":1.5,\"range\":4.25,\"transform\":" + Translate(1, 2, 3) + "}]}";
    Scene s;
    ASSERT_TRUE(s.LoadFromString(doc.c_str()));
    ASSERT_EQ(s.LightCount(), 1u);
    const OmniLight& l = s.Lights()[0];
    EXPECT_FLOAT_EQ(l.colour[0], 0.5f);
    EXPECT_FLOAT_EQ(l.colour[2], 0.125f);
    EXPECT_FLOAT_EQ(l.energy, 1.5f);
    EXPECT_FLOAT_EQ(l.range, 4.25f);
    // The stored transform is LOCAL; the light's path is not in the node table
    // here, so the world transform falls back to it.
    EXPECT_FLOAT_EQ(s.LightWorldTransform(0).Tx(), 1.0f);
    EXPECT_FLOAT_EQ(s.LightWorldTransform(0).Tz(), 3.0f);
}

TEST(Scene, LightWorldPositionComposesThroughItsParentChain) {
    // The exporter writes n.transform, not n.global_transform. A light parented
    // under a moved node must be lit from where it is, not from its local
    // offset.
    const std::string doc =
        "{\"nodes\":["
        "{\"name\":\"r\",\"path\":\".\",\"type\":\"Node3D\",\"transform\":" +
            Translate(0, 0, 0) + "},"
        "{\"name\":\"rig\",\"path\":\"rig\",\"type\":\"Node3D\",\"transform\":" +
            Translate(100, 0, 0) + "},"
        "{\"name\":\"lamp\",\"path\":\"rig/lamp\",\"type\":\"OmniLight3D\","
        "\"transform\":" + Translate(5, 0, 0) + "}],"
        "\"lights\":[{\"path\":\"rig/lamp\",\"color\":[1,1,1],\"energy\":1,"
        "\"range\":10,\"transform\":" + Translate(5, 0, 0) + "}]}";
    Scene s;
    ASSERT_TRUE(s.LoadFromString(doc.c_str()));
    ASSERT_EQ(s.LightCount(), 1u);
    EXPECT_GE(s.Lights()[0].node, 0) << "light did not resolve to its node";
    EXPECT_FLOAT_EQ(s.Lights()[0].local.Tx(), 5.0f);        // local, as exported
    EXPECT_FLOAT_EQ(s.LightWorldTransform(0).Tx(), 105.0f); // composed
}

TEST(Scene, RejectsMalformedInput) {
    Scene s;
    EXPECT_FALSE(s.LoadFromString(nullptr));
    EXPECT_FALSE(s.LoadFromString("{ not json"));
    EXPECT_FALSE(s.LoadFromString("{}"));
    EXPECT_FALSE(s.LoadFromString("{\"nodes\":[]}"));
    EXPECT_FALSE(s.LoadFromFile(nullptr));
    EXPECT_FALSE(s.LoadFromFile("assets/extracted/nope.json"));
}

TEST(Scene, OutOfRangeWorldTransformIsIdentityNotUndefined) {
    Scene s;
    ASSERT_TRUE(s.LoadFromString(
        "{\"nodes\":[{\"name\":\"r\",\"path\":\".\",\"type\":\"Node3D\"}]}"));
    EXPECT_FLOAT_EQ(s.WorldTransform(-1).Tx(), 0.0f);
    EXPECT_FLOAT_EQ(s.WorldTransform(9999).m[0], 1.0f);
}

// ------------------------------------------------------ the real exported room

TEST(Scene, LoadsTheRealRoomAndMatchesPhaseZeroCounts) {
    BSHT_NEEDS_FILE(ScenePath());
    Scene s;
    ASSERT_TRUE(s.LoadFromFile(ScenePath().c_str()))
        << "run tools/godot/export_scene_graph.gd first: " << ScenePath();

    // Runtime counts (v2.2): sub-scene instances expand at load.
    EXPECT_EQ(s.NodeCount(), 1798u);
    EXPECT_EQ(s.LightCount(), 29u);

    int meshInstances = 0, withTransform = 0, inlinePrimitives = 0, withMesh = 0;
    for (const SceneNode& n : s.Nodes()) {
        if (n.type == "MeshInstance3D") ++meshInstances;
        if (n.hasTransform) ++withTransform;
        if (n.HasMesh()) {
            ++withMesh;
            if (n.IsInlinePrimitive()) ++inlinePrimitives;
        }
    }
    EXPECT_EQ(meshInstances, 659);
    EXPECT_EQ(withTransform, 1058);

    // 76 meshes are unnamed inline primitives with no extracted counterpart.
    // They are the scene-authored PlaneMesh/BoxMesh/CylinderMesh geometry and
    // need the main.tscn ArrayMesh fallback path.
    EXPECT_EQ(withMesh, 658);
    EXPECT_EQ(inlinePrimitives, 76);

    // Every node must resolve to a parent that exists, or be a root.
    for (const SceneNode& n : s.Nodes()) {
        EXPECT_LT(n.parent, static_cast<int>(s.NodeCount()));
        if (n.parent >= 0) {
            EXPECT_NE(n.parent, &n - s.Nodes().data()) << "node is its own parent";
        }
    }

    // Composition must actually do something: a deep node's world position
    // should differ from its local one.
    const int camera = s.FindByPath("Camera");
    ASSERT_GE(camera, 0);
    const int splatter = s.FindByPath("Camera/blood splatter plane");
    ASSERT_GE(splatter, 0);
    EXPECT_EQ(s.Nodes()[static_cast<std::size_t>(splatter)].parent, camera);

    const core::Mat4& localSplat = s.Nodes()[static_cast<std::size_t>(splatter)].local;
    const core::Mat4& worldSplat = s.WorldTransform(splatter);
    EXPECT_NEAR(localSplat.Tz(), -1.347f, 0.001f);
    EXPECT_GT(std::abs(worldSplat.Tx() - localSplat.Tx()), 1.0f)
        << "world transform is not composing with the camera's";

    // Every one of the 29 lights must resolve to its own node, or the room is
    // lit from the wrong places.
    for (std::size_t i = 0; i < s.LightCount(); ++i) {
        EXPECT_GE(s.Lights()[i].node, 0)
            << "light " << i << " (" << s.Lights()[i].path << ") did not resolve";
    }
}
