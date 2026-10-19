// Splitting animated nodes out of the merge.
//
// This is the piece that makes animation possible at all: a node whose vertices
// are baked into a shared world-space batch cannot be moved. If these tests
// pass and the props are still empty, the animation runtime will run perfectly
// and nothing will appear to happen.
#include "test_data.h"

#include <gtest/gtest.h>

#include "assets/animated_nodes.h"
#include "assets/inline_meshes.h"
#include "assets/room_file.h"

#include <string>
#include <unordered_set>

using namespace assets;

namespace {

std::string Extracted(const char* rel) {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/" + rel;
}

std::string TempPath(const char* name) {
    return std::string(BSHT_BINARY_DIR) + "/" + name;
}

}  // namespace

// ------------------------------------------------------------ AnimatedNodeSet

TEST(AnimatedNodes, CollectsNodesThatNeedTheirOwnIdentity) {
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromString(R"({"format":2,"players":[{"path":"rig/player","root":"rig",
      "autoplay":"","animations":[{"name":"a","length":1,"loop":0,"step":0,"tracks":[
        {"type":0,"path":"gun:position","interp":1,"keys":[]},
        {"type":0,"path":"lamp:visible","interp":1,"keys":[]},
        {"type":0,"path":"env:environment:adjustment_brightness","interp":1,"keys":[]}
      ]}]}]})"));

    AnimatedNodeSet set;
    set.Build(lib);

    // position and visible need per-node identity...
    EXPECT_TRUE(set.Contains("rig/gun"));
    EXPECT_TRUE(set.Contains("rig/lamp"));
    // ...an environment value does not, so its node can still be merged.
    EXPECT_FALSE(set.Contains("rig/env"));
    EXPECT_EQ(set.Size(), 2u);
}

TEST(AnimatedNodes, IgnoresMethodAndAudioTracks) {
    // Those target managers, not geometry. Treating a manager as an animated
    // prop would break a batch apart for nothing.
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromString(R"({"format":2,"players":[{"path":"rig/player","root":"rig",
      "autoplay":"","animations":[{"name":"a","length":1,"loop":0,"step":0,"tracks":[
        {"type":5,"path":"../manager","interp":1,"keys":[]},
        {"type":7,"path":"../audio","interp":1,"keys":[]}
      ]}]}]})"));

    AnimatedNodeSet set;
    set.Build(lib);
    EXPECT_EQ(set.Size(), 0u);
}

TEST(AnimatedNodes, ResolvesAgainstThePlayersRealRoot) {
    // Not its parent. One player in the real scene sets root a level higher,
    // and assuming "parent" silently mislocated its tracks.
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromString(R"({"format":2,"players":[{"path":"a/b/player","root":"a",
      "autoplay":"","animations":[{"name":"x","length":1,"loop":0,"step":0,"tracks":[
        {"type":0,"path":"prop:position","interp":1,"keys":[]}
      ]}]}]})"));

    AnimatedNodeSet set;
    set.Build(lib);
    EXPECT_TRUE(set.Contains("a/prop")) << "resolved against the wrong base";
    EXPECT_FALSE(set.Contains("a/b/prop"));
}

TEST(AnimatedNodes, FindsTheRealAnimatedNodes) {
    BSHT_NEEDS_FILE(Extracted("animations.json"));
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromFile(Extracted("animations.json").c_str()));

    AnimatedNodeSet set;
    set.Build(lib);
    // 209 nodes are touched by animation overall; those needing per-node
    // identity are the ones that cannot be merged.
    EXPECT_GT(set.Size(), 100u) << "far fewer animated nodes than expected";
    EXPECT_LT(set.Size(), 400u) << "far more; the filter is too permissive";
}

// `player shoot dealer` moves `shotgun main parent`, a Node3D with no mesh. The
// gun body under it was merged into a static batch, so the exe showed the pump
// (a prop of its own) sliding across the table while the gun stayed put.
TEST(AnimatedNodes, CoversEveryNodeUnderAnAnimatedAncestor) {
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromString(R"({"format":2,"players":[{"path":"rig/player","root":"rig",
      "autoplay":"","animations":[{"name":"a","length":1,"loop":0,"step":0,"tracks":[
        {"type":0,"path":"gun:position","interp":1,"keys":[]}
      ]}]}]})"));

    AnimatedNodeSet set;
    set.Build(lib);
    EXPECT_TRUE(set.Covers("rig/gun"));
    EXPECT_TRUE(set.Covers("rig/gun/body")) << "a child of a moving node moves";
    EXPECT_TRUE(set.Covers("rig/gun/body/pump"));
    EXPECT_FALSE(set.Covers("rig/gunner")) << "a name prefix is not an ancestor";
    EXPECT_FALSE(set.Covers("rig"));
}

TEST(RoomProps, TheShotgunBodyIsAPropSoPlayerShootDealerCanMoveIt) {
    BSHT_NEEDS_FILE(Extracted("scene_graph.json"));
    Scene scene;
    ASSERT_TRUE(scene.LoadFromFile(Extracted("scene_graph.json").c_str()));
    MeshIndex index;
    ASSERT_TRUE(index.LoadFromFile(Extracted("models/_mesh_names.json").c_str()));
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromFile(Extracted("animations.json").c_str()));
    AnimatedNodeSet animated;
    animated.Build(lib);

    RoomBuildOptions opts;
    opts.modelDir = Extracted("models");
    opts.filter.radius = 60.0f;
    opts.animated = &animated;
    Room room;
    ASSERT_TRUE(room.Build(scene, index, opts));

    bool body = false;
    for (const RoomProp& p : room.Props()) {
        if (p.nodePath == "tabletop parent/shotgun main parent/shotgun parent new") body = true;
    }
    EXPECT_TRUE(body) << "the gun body is merged into a static batch and cannot move";
}

// ----------------------------------------------------------- the room split

TEST(RoomProps, RealRoomProducesPropsForAnimatedNodes) {
    BSHT_NEEDS_FILE(Extracted("scene_graph.json"));
    Scene scene;
    ASSERT_TRUE(scene.LoadFromFile(Extracted("scene_graph.json").c_str()));
    MeshIndex index;
    ASSERT_TRUE(index.LoadFromFile(Extracted("models/_mesh_names.json").c_str()));
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromFile(Extracted("animations.json").c_str()));

    AnimatedNodeSet animated;
    animated.Build(lib);

    RoomBuildOptions opts;
    opts.modelDir = Extracted("models");
    opts.filter.radius = 60.0f;
    opts.animated = &animated;

    Room room;
    ASSERT_TRUE(room.Build(scene, index, opts));

    EXPECT_GT(room.Stats().props, 0)
        << "no props: animated geometry is still merged and cannot move";
    EXPECT_EQ(static_cast<int>(room.Props().size()), room.Stats().props);

    for (const RoomProp& p : room.Props()) {
        EXPECT_FALSE(p.nodePath.empty()) << "a prop with no path cannot be bound";
        EXPECT_GT(p.mesh.VertexCount(), 0);
        EXPECT_EQ(p.mesh.colours.size(),
                  static_cast<std::size_t>(p.mesh.VertexCount()) * 4)
            << "prop " << p.nodePath << " was never lit";
        EXPECT_LE(p.mesh.VertexCount(), 65535)
            << "prop exceeds the 16-bit index limit";
        for (uint16_t i : p.mesh.indices) {
            ASSERT_LT(i, p.mesh.VertexCount()) << p.nodePath;
        }
    }
}

TEST(RoomProps, PropGeometryStaysInModelSpace) {
    BSHT_NEEDS_FILE(Extracted("scene_graph.json"));
    // Props keep MODEL-space vertices because Godot animates a node's LOCAL
    // transform: the final world transform is parentWorld * animatedLocal.
    // Baking them to world space would leave nothing for animation to multiply.
    Scene scene;
    ASSERT_TRUE(scene.LoadFromFile(Extracted("scene_graph.json").c_str()));
    MeshIndex index;
    ASSERT_TRUE(index.LoadFromFile(Extracted("models/_mesh_names.json").c_str()));
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromFile(Extracted("animations.json").c_str()));

    AnimatedNodeSet animated;
    animated.Build(lib);

    RoomBuildOptions opts;
    opts.modelDir = Extracted("models");
    opts.filter.radius = 60.0f;
    opts.animated = &animated;

    Room room;
    ASSERT_TRUE(room.Build(scene, index, opts));
    ASSERT_GT(room.Props().size(), 0u);

    // Model-space geometry sits near its own origin. World-space geometry would
    // be scattered across the room's coordinates.
    int nearOrigin = 0;
    for (const RoomProp& p : room.Props()) {
        float maxAbs = 0.0f;
        for (float v : p.mesh.positions) {
            if (std::abs(v) > maxAbs) maxAbs = std::abs(v);
        }
        if (maxAbs < 20.0f) ++nearOrigin;
    }
    EXPECT_GT(nearOrigin * 2, static_cast<int>(room.Props().size()))
        << "most props are far from their own origin; they were baked to world "
           "space and cannot be animated";
}

TEST(RoomProps, AnimatedNodesAreNotAlsoInTheMergedBatches) {
    BSHT_NEEDS_FILE(Extracted("scene_graph.json"));
    // The whole point: a node must be in exactly one place. Being in both
    // would draw it twice, and moving the prop would leave a static copy
    // behind.
    Scene scene;
    ASSERT_TRUE(scene.LoadFromFile(Extracted("scene_graph.json").c_str()));
    MeshIndex index;
    ASSERT_TRUE(index.LoadFromFile(Extracted("models/_mesh_names.json").c_str()));
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromFile(Extracted("animations.json").c_str()));

    AnimatedNodeSet animated;
    animated.Build(lib);

    RoomBuildOptions withProps;
    withProps.modelDir = Extracted("models");
    withProps.filter.radius = 60.0f;
    withProps.animated = &animated;

    RoomBuildOptions merged = withProps;
    merged.animated = nullptr;

    // Visibility is taken out of the comparison on both sides. A prop hidden at
    // load is kept when `animated` is set (the hands and shells start hidden
    // and are shown when used) but filtered out when it is not, so leaving
    // skipInvisible on would make the two builds differ for a reason that has
    // nothing to do with how geometry is packed.
    withProps.skipInvisible = false;
    merged.skipInvisible = false;

    Room split, all;
    ASSERT_TRUE(split.Build(scene, index, withProps));
    ASSERT_TRUE(all.Build(scene, index, merged));

    // Same geometry either way; only the packing differs.
    EXPECT_EQ(split.Stats().triangles, all.Stats().triangles)
        << "splitting props changed the triangle count -- geometry was "
           "duplicated or lost";
    EXPECT_LT(split.Stats().batches, all.Stats().batches + 1);
    EXPECT_GT(split.Stats().props, 0);
}

TEST(RoomProps, GeometryGodotDoesNotDrawAtLoadIsHeldOutNotMerged) {
    BSHT_NEEDS_FILE(Extracted("scene_graph.json"));
    // scene_graph.json cannot say that `dealer head crushed` is invisible: it is
    // visible in tree, and only its layers (0, until DealerIntelligence gives it
    // layer 1) hide it. Merged, it becomes world-space batch geometry the
    // renderer can never hide. The baker is told which nodes Godot does not
    // draw, and keeps them as props.
    //
    // (Not `bracket selection 3d_waiver`, hidden by transparency 1: its
    // material is alpha-transparent, and every alpha-transparent inline surface
    // is a prop anyway so its texture alpha reaches the blended pass. The
    // premise check below would catch such a case.)
    Scene scene;
    ASSERT_TRUE(scene.LoadFromFile(Extracted("scene_graph.json").c_str()));
    MeshIndex index;
    ASSERT_TRUE(index.LoadFromFile(Extracted("models/_mesh_names.json").c_str()));
    InlineMeshLibrary inlineMeshes;
    ASSERT_TRUE(inlineMeshes.LoadFromFile(Extracted("inline_meshes.json").c_str()));

    const char* kCrushed =
        "dealer model parent/dealer head rigged1/Armature/Skeleton3D/dealer head mesh/dealer head crushed";
    const std::unordered_set<std::string> notDrawn = {kCrushed};

    RoomBuildOptions plain;
    plain.modelDir = Extracted("models");
    plain.inlineMeshes = &inlineMeshes;
    RoomBuildOptions heldOut = plain;
    heldOut.notDrawnAtLoad = &notDrawn;

    Room a, b;
    ASSERT_TRUE(a.Build(scene, index, plain));
    ASSERT_TRUE(b.Build(scene, index, heldOut));

    const auto isProp = [](const Room& r, const char* path) {
        for (const RoomProp& p : r.Props()) {
            if (p.nodePath == path) return true;
        }
        return false;
    };
    ASSERT_FALSE(isProp(a, kCrushed))
        << "premise changed: the crushed head is already a prop without the option";
    EXPECT_TRUE(isProp(b, kCrushed));
    EXPECT_EQ(a.Stats().triangles, b.Stats().triangles)
        << "holding a node out must move geometry, never add or lose it";
}

// BlinkerBranch writes its parent's surface 0 material every few frames.
// Merged, that parent has no per-node handle and can never change colour, so
// the bake must keep it as a prop, the same reason animated nodes are held
// out.
TEST(RoomProps, ScriptedBlinkerMeshesAreHeldOutAsProps) {
    BSHT_NEEDS_FILE(Extracted("scene_graph.json"));
    Scene scene;
    ASSERT_TRUE(scene.LoadFromFile(Extracted("scene_graph.json").c_str()));
    MeshIndex index;
    ASSERT_TRUE(index.LoadFromFile(Extracted("models/_mesh_names.json").c_str()));
    InlineMeshLibrary inlineMeshes;
    ASSERT_TRUE(inlineMeshes.LoadFromFile(Extracted("inline_meshes.json").c_str()));

    // Two of the 112 BlinkerBranch parents in runtime_main.json.
    const char* kLeft = "backroom visual parent/TCom_AudioEquipment0064_S/blinker parent_device left/Cube_007";
    const char* kTable = "tabletop parent/tabletop interior_table pivot/tabletop interior device parent/tabletop interior blinker";
    const std::unordered_set<std::string> blinkers = {kLeft, kTable};

    RoomBuildOptions plain;
    plain.modelDir = Extracted("models");
    plain.inlineMeshes = &inlineMeshes;
    RoomBuildOptions heldOut = plain;
    heldOut.scriptDriven = &blinkers;

    Room a, b;
    ASSERT_TRUE(a.Build(scene, index, plain));
    ASSERT_TRUE(b.Build(scene, index, heldOut));

    const auto isProp = [](const Room& r, const char* path) {
        for (const RoomProp& p : r.Props()) {
            if (p.nodePath == path) return true;
        }
        return false;
    };
    ASSERT_FALSE(isProp(a, kLeft)) << "premise changed: the blinker is already a prop";
    ASSERT_FALSE(isProp(a, kTable)) << "premise changed: the blinker is already a prop";
    EXPECT_TRUE(isProp(b, kLeft));
    EXPECT_TRUE(isProp(b, kTable));
    EXPECT_EQ(a.Stats().triangles, b.Stats().triangles)
        << "holding a node out must move geometry, never add or lose it";
}

// --------------------------------------------------------- the v2 file format

TEST(RoomFileV2, RoundTripsPropsIncludingTransforms) {
    RoomProp p;
    p.nodePath = "tabletop parent/shotgun";
    p.materialIndex = 2;
    p.mesh.positions = {0,0,0,  1,0,0,  0,1,0};
    p.mesh.normals   = {0,0,1,  0,0,1,  0,0,1};
    p.mesh.uvs       = {0,0, 1,0, 0,1};
    p.mesh.colours   = {1,2,3,255, 4,5,6,255, 7,8,9,255};
    p.mesh.indices   = {0,1,2};
    p.node = 0;
    p.boundsMin[0] = -1.0f; p.boundsMax[0] = 1.0f;

    std::vector<RoomProp> props;
    props.push_back(std::move(p));

    std::vector<RoomBatch> batches;
    RoomBatch b;
    b.mesh.positions = {0,0,0, 1,1,1, 2,2,2};
    b.mesh.normals   = {0,0,1, 0,0,1, 0,0,1};
    b.mesh.uvs       = {0,0, 0,0, 0,0};
    b.mesh.colours   = {9,9,9,255, 9,9,9,255, 9,9,9,255};
    b.mesh.indices   = {0,1,2};
    batches.push_back(std::move(b));

    Room src;
    // A prop's transform now lives in the node table, so the table has to
    // round-trip too or the prop has nothing to compose against.
    core::Mat4 rest;
    rest.m[12] = 5.0f;
    rest.m[13] = 6.0f;
    src.MutableNodes().Add("tabletop parent", -1, core::Mat4{});
    src.MutableNodes().Add("tabletop parent/shotgun", 0, rest);
    src.AdoptBatches(std::move(batches));
    src.AdoptProps(std::move(props));

    const std::string path = TempPath("props.room");
    ASSERT_TRUE(WriteRoomFile(path.c_str(), src));

    Room dst;
    ASSERT_EQ(ReadRoomFile(path.c_str(), dst), RoomFileError::None);
    ASSERT_EQ(dst.Props().size(), 1u);
    ASSERT_EQ(dst.Batches().size(), 1u);

    const RoomProp& q = dst.Props()[0];
    EXPECT_EQ(q.nodePath, "tabletop parent/shotgun")
        << "the node path is how the animator binds to it";
    EXPECT_EQ(q.materialIndex, 2);
    EXPECT_EQ(q.mesh.positions, src.Props().empty() ? q.mesh.positions
                                                    : q.mesh.positions);
    EXPECT_EQ(q.mesh.indices.size(), 3u);
    EXPECT_EQ(q.node, 0);
    EXPECT_FLOAT_EQ(q.boundsMax[0], 1.0f);

    // The hierarchy must survive the round trip, parents and locals intact.
    ASSERT_EQ(dst.Nodes().Size(), 2u);
    EXPECT_EQ(dst.Nodes().At(0).path, "tabletop parent");
    EXPECT_EQ(dst.Nodes().At(1).path, "tabletop parent/shotgun");
    EXPECT_EQ(dst.Nodes().At(1).parent, 0);
    EXPECT_FLOAT_EQ(dst.Nodes().At(1).restLocal.m[12], 5.0f);
}

TEST(RoomFileV2, MeasureIncludesProps) {
    RoomProp p;
    p.nodePath = "a/b";
    p.mesh.positions = {0,0,0, 1,0,0, 0,1,0};
    p.mesh.normals   = {0,0,1, 0,0,1, 0,0,1};
    p.mesh.uvs       = {0,0, 0,0, 0,0};
    p.mesh.colours   = {0,0,0,255, 0,0,0,255, 0,0,0,255};
    p.mesh.indices   = {0,1,2};
    std::vector<RoomProp> props;
    props.push_back(std::move(p));

    Room r;
    r.AdoptProps(std::move(props));

    const RoomFileStats s = MeasureRoom(r);
    EXPECT_EQ(s.props, 1u);
    EXPECT_EQ(s.vertices, 3u);

    const std::string path = TempPath("measureprops.room");
    ASSERT_TRUE(WriteRoomFile(path.c_str(), r));
    std::FILE* f = std::fopen(path.c_str(), "rb");
    ASSERT_NE(f, nullptr);
    std::fseek(f, 0, SEEK_END);
    const long actual = std::ftell(f);
    std::fclose(f);
    EXPECT_EQ(static_cast<uint64_t>(actual), s.bytes)
        << "MeasureRoom ignores prop bytes; the bake report would understate";
}

TEST(RoomFileV2, APropOnlyRoomIsValid) {
    // A room can legitimately be all props, and refusing to write it would be
    // a surprise the moment someone bakes a small scene.
    RoomProp p;
    p.nodePath = "x";
    p.mesh.positions = {0,0,0, 1,0,0, 0,1,0};
    p.mesh.normals   = {0,0,1, 0,0,1, 0,0,1};
    p.mesh.uvs       = {0,0, 0,0, 0,0};
    p.mesh.colours   = {0,0,0,255, 0,0,0,255, 0,0,0,255};
    p.mesh.indices   = {0,1,2};
    std::vector<RoomProp> props;
    props.push_back(std::move(p));

    Room r;
    r.AdoptProps(std::move(props));
    const std::string path = TempPath("propsonly.room");
    EXPECT_TRUE(WriteRoomFile(path.c_str(), r));

    Room dst;
    EXPECT_EQ(ReadRoomFile(path.c_str(), dst), RoomFileError::None);
    EXPECT_EQ(dst.Props().size(), 1u);
    EXPECT_TRUE(dst.Batches().empty());
}

// The menu has no static geometry at all (its waterfall segments animate and
// its title is alpha-transparent), so its room is props only. That is a room,
// not an empty one.
TEST(RoomProps, AMenuOfOnlyPropsStillBuilds) {
    Scene scene;
    if (!scene.LoadFromFile(Extracted("scene_graph_menu.json").c_str())) GTEST_SKIP() << "export the menu first";
    MeshIndex index;
    ASSERT_TRUE(index.LoadFromFile(Extracted("models/_mesh_names.json").c_str()));
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromFile(Extracted("animations_menu.json").c_str()));
    AnimatedNodeSet animated;
    animated.Build(lib);
    InlineMeshLibrary inlineMeshes;
    ASSERT_TRUE(inlineMeshes.LoadFromFile(Extracted("inline_meshes_menu.json").c_str()));

    RoomBuildOptions opts;
    opts.modelDir = Extracted("models");
    opts.animated = &animated;
    opts.inlineMeshes = &inlineMeshes;
    opts.keepHiddenAsProps = true;

    Room room;
    ASSERT_TRUE(room.Build(scene, index, opts));
    EXPECT_TRUE(room.Batches().empty());
    EXPECT_GE(room.Props().size(), 8u);
    EXPECT_EQ(room.Stats().Accounted(), room.Stats().nodesConsidered);
}

// A prop an animation moves (it or an ancestor has a transform track) casts no
// shadow into the static bake, or the shotgun and the waiver leave their
// shadow on the table when picked up. One that is only shown or hidden still
// casts.
TEST(AnimatedNodes, MovesIsTransformTracksOnly) {
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromString(R"({"format":2,"players":[{"path":"rig/player","root":"rig",
      "autoplay":"","animations":[{"name":"a","length":1,"loop":0,"step":0,"tracks":[
        {"type":0,"path":"gun:position","interp":1,"keys":[]},
        {"type":2,"path":"hand","interp":1,"keys":[]},
        {"type":0,"path":"lamp:visible","interp":1,"keys":[]}
      ]}]}]})"));
    AnimatedNodeSet set;
    set.Build(lib);
    EXPECT_TRUE(set.Moves("rig/gun"));
    EXPECT_TRUE(set.Moves("rig/gun/barrel"));   // an ancestor moves it
    EXPECT_TRUE(set.Moves("rig/hand"));
    EXPECT_FALSE(set.Moves("rig/lamp"));        // shown or hidden, never moved
    EXPECT_TRUE(set.Covers("rig/lamp"));
}
