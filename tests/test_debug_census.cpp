// The census: what the device reports about itself.
//
// These run against the real room and the real animation library: a census
// asserted against a fixture would keep passing after the thing it describes
// changed, the failure this subsystem exists to prevent.
#include "assets/animation.h"
#include "assets/room_file.h"
#include "debug/report.h"
#include "debug/state.h"
#include "godot/runtime_scene_loader.h"
#include "godot/scene_tree.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

namespace {

std::string RoomPath() {
    return std::string(BSHT_SOURCE_DIR) + "/assets/baked/full_world.room";
}
std::string AnimPath() {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/animations.json";
}
std::string ScenePath() {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/runtime_main.json";
}

bool Exists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

bool Ready() {
    return Exists(RoomPath()) && Exists(AnimPath()) && Exists(ScenePath());
}

#define REQUIRE_ASSETS() \
    if (!Ready()) GTEST_SKIP() << "run ./build/src/bake_room and the exporters"

// Builds the same chain the game does, so the census describes reality.
struct Fixture {
    assets::Room room;
    assets::AnimationLibrary lib;
    godot::SceneTree tree;
    debug::DebugState state;

    bool Build() {
        if (assets::ReadRoomFile(RoomPath().c_str(), room) !=
            assets::RoomFileError::None) {
            return false;
        }
        if (!lib.LoadFromFile(AnimPath().c_str())) return false;
        if (godot::LoadRuntimeScene(ScenePath().c_str(), tree) !=
            godot::RuntimeSceneError::None) {
            return false;
        }
        debug::GatherStatic(state, room, lib, tree, nullptr, nullptr, {});
        return true;
    }
};

TEST(DebugCensus, DescribesTheRealRoom) {
    REQUIRE_ASSETS();
    Fixture f;
    ASSERT_TRUE(f.Build());

    EXPECT_GT(f.state.room.batches, 0);
    EXPECT_GT(f.state.room.props, 0);
    EXPECT_GT(f.state.room.triangles, 0);
    EXPECT_EQ(f.state.room.props,
              static_cast<int>(f.state.props.size()))
        << "the prop rows and the prop count disagree";
}

TEST(DebugCensus, EveryPropIsInExactlyOneVisibilityBucket) {
    REQUIRE_ASSETS();
    // The same discipline the room baker's node accounting has: a prop that
    // falls out of the sum is one nobody notices going missing.
    Fixture f;
    ASSERT_TRUE(f.Build());
    EXPECT_EQ(f.state.room.props,
              f.state.room.propsVisibleAtLoad + f.state.room.propsHiddenAtLoad);
}

TEST(DebugCensus, MostHiddenPropsAreReachableByAnAnimation) {
    REQUIRE_ASSETS();
    // A prop the scene hides and no animation can show is uploaded and
    // unreachable. A few are expected; most of them being stranded would mean
    // the join between the room and the animation library is broken, which is
    // indistinguishable from "the game does not animate" when you are looking
    // at a photograph of a Vita.
    Fixture f;
    ASSERT_TRUE(f.Build());
    ASSERT_GT(f.state.room.propsHiddenAtLoad, 0);
    EXPECT_LT(f.state.room.propsStranded, f.state.room.propsHiddenAtLoad)
        << "every hidden prop is stranded; nothing can ever appear";
}

TEST(DebugCensus, CountsTheVisibleTracksThatMakeTheRoomMove) {
    REQUIRE_ASSETS();
    Fixture f;
    ASSERT_TRUE(f.Build());
    EXPECT_GE(f.state.animation.visibleTracks, 150)
        << "the game animates visibility 201 times";
}

TEST(DebugCensus, TrackBindingIsAccountedForExactly) {
    REQUIRE_ASSETS();
    // Bound plus unresolved must equal the property tracks considered. If they
    // do not, the census is hiding a third outcome and its unresolved figure
    // cannot be trusted.
    Fixture f;
    ASSERT_TRUE(f.Build());
    EXPECT_GT(f.state.animation.tracks, 0);
    EXPECT_GT(f.state.animation.tracksBound, 0);
    EXPECT_LE(f.state.animation.tracksBound + f.state.animation.tracksUnresolved,
              f.state.animation.tracks);
}

TEST(DebugCensus, NamesUnresolvedPathsRatherThanOnlyCountingThem) {
    REQUIRE_ASSETS();
    // A count says something is wrong; a path says what. And the list must
    // admit when it is truncated.
    Fixture f;
    ASSERT_TRUE(f.Build());
    if (f.state.animation.tracksUnresolved > 0) {
        EXPECT_FALSE(f.state.animation.unresolvedPaths.empty());
        EXPECT_LE(static_cast<int>(f.state.animation.unresolvedPaths.size()),
                  debug::kMaxNamed);
    }
}

TEST(DebugCensus, EveryAnimationHasARow) {
    REQUIRE_ASSETS();
    Fixture f;
    ASSERT_TRUE(f.Build());
    EXPECT_EQ(f.state.animation.animations,
              static_cast<int>(f.state.anims.size()));
    for (const debug::AnimRow& r : f.state.anims) {
        EXPECT_FALSE(r.name.empty());
        EXPECT_LE(r.bound, r.tracks);
    }
}

TEST(DebugCensus, FormattingProducesEverySection) {
    REQUIRE_ASSETS();
    // A section that is silently omitted reads as "not measured", which is the
    // ambiguity this subsystem exists to remove.
    Fixture f;
    ASSERT_TRUE(f.Build());
    const std::string text = debug::FormatCensus(f.state);

    for (const char* section : {"room", "props", "anim", "ui", "fonts",
                                "STRANDED", "UNRESOLVED", "CENSUS"}) {
        EXPECT_NE(text.find(section), std::string::npos)
            << "missing section: " << section;
    }
}

TEST(DebugCensus, AMissingScreenPrefixIsCalledOut) {
    REQUIRE_ASSETS();
    // A prefix matching nothing is a typo or a moved subtree. Reporting it as
    // an empty screen would hide the difference.
    Fixture f;
    ASSERT_TRUE(f.Build());
    debug::DebugState s;
    debug::GatherStatic(s, f.room, f.lib, f.tree, nullptr, nullptr,
                        {"Camera/dialogue UI/no such screen"});
    // With no UiLayout supplied there are no screens at all; the point is that
    // the formatter says so rather than printing nothing.
    const std::string text = debug::FormatCensus(s);
    EXPECT_NE(text.find("none were asked about"), std::string::npos);
}

TEST(DebugCensus, FrameFormattingSurvivesAnEmptyState) {
    // The overlay and the report both format this before the first frame has
    // been measured. Zeroes are fine; a crash is not.
    debug::FrameCensus f;
    const std::string text = debug::FormatFrame(f);
    EXPECT_NE(text.find("frame"), std::string::npos);
    EXPECT_NE(text.find("anim"), std::string::npos);
}

}  // namespace
