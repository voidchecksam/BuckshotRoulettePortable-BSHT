// Animation data model: what the loader reads out of animations.json.
//
// Sampling is Godot's (godot/animation.*, checked against the engine by
// parity_anim_sample); these pin the parse, and that every real track samples
// to finite numbers through that sampler.
#include "test_data.h"

#include <gtest/gtest.h>

#include "assets/animation.h"
#include "godot/animation.h"

#include <cmath>
#include <set>
#include <string>

using namespace assets;

namespace {

std::string RealPath() {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/animations.json";
}

}  // namespace

TEST(Animation, ParsesTaggedValuesOfEveryKind) {
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromString(R"({"format":2,"players":[{"path":"p","autoplay":"",
      "animations":[{"name":"a","length":1.0,"loop":0,"step":0.0,"tracks":[
        {"type":0,"path":"n:visible","interp":1,"keys":[
           {"t":0.0,"k":"b","v":true}]},
        {"type":0,"path":"n:light_energy","interp":1,"keys":[
           {"t":0.0,"k":"f","v":2.5}]},
        {"type":0,"path":"n:position","interp":1,"keys":[
           {"t":0.0,"k":"v3","v":[1,2,3]}]},
        {"type":0,"path":"n:modulate","interp":1,"keys":[
           {"t":0.0,"k":"c","v":[0.1,0.2,0.3,1.0]}]},
        {"type":0,"path":"n:layers","interp":1,"keys":[
           {"t":0.0,"k":"i","v":7}]}
      ]}]}]})"));

    const Animation* a = lib.Find("p", "a");
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->tracks.size(), 5u);
    EXPECT_TRUE (a->tracks[0].keys[0].value.AsBool());
    EXPECT_FLOAT_EQ(a->tracks[1].keys[0].value.AsFloat(), 2.5f);
    EXPECT_FLOAT_EQ(a->tracks[2].keys[0].value.f[2], 3.0f);
    EXPECT_FLOAT_EQ(a->tracks[3].keys[0].value.f[3], 1.0f);
    EXPECT_EQ(a->tracks[4].keys[0].value.AsInt(), 7);
}

TEST(Animation, SplitsNodePathFromProperty) {
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromString(R"({"format":2,"players":[{"path":"p","autoplay":"",
      "animations":[{"name":"a","length":1.0,"loop":0,"step":0.0,"tracks":[
        {"type":0,"path":"shotgun parent/shotgun:position","interp":1,"keys":[]},
        {"type":5,"path":"../manager","interp":1,"keys":[]}
      ]}]}]})"));
    const Animation* a = lib.Find("p", "a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->tracks[0].nodePath, "shotgun parent/shotgun");
    EXPECT_EQ(a->tracks[0].property, "position");
    // A method track has no property half.
    EXPECT_EQ(a->tracks[1].nodePath, "../manager");
    EXPECT_TRUE(a->tracks[1].property.empty());
}

TEST(Animation, ParsesMethodTracksWithArguments) {
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromString(R"({"format":2,"players":[{"path":"p","autoplay":"",
      "animations":[{"name":"a","length":1.0,"loop":0,"step":0.0,"tracks":[
        {"type":5,"path":"../mgr","interp":1,"keys":[
          {"t":0.85,"m":"PanFilter","a":[]},
          {"t":3.5,"m":"SetupShell","a":[{"k":"i","v":2},{"k":"b","v":true}]}]}
      ]}]}]})"));
    const Animation* a = lib.Find("p", "a");
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->tracks[0].keys.size(), 2u);
    EXPECT_EQ(a->tracks[0].keys[0].method, "PanFilter");
    EXPECT_FLOAT_EQ(a->tracks[0].keys[0].time, 0.85f);
    EXPECT_EQ(a->tracks[0].keys[1].method, "SetupShell");
    ASSERT_EQ(a->tracks[0].keys[1].args.size(), 2u);
    EXPECT_EQ(a->tracks[0].keys[1].args[0].AsInt(), 2);
    EXPECT_TRUE(a->tracks[0].keys[1].args[1].AsBool());
}

TEST(Animation, RejectsMalformedInput) {
    AnimationLibrary lib;
    EXPECT_FALSE(lib.LoadFromString(nullptr));
    EXPECT_FALSE(lib.LoadFromString("{}"));
    EXPECT_FALSE(lib.LoadFromString(R"({"format":2,"players":[]})"));
    EXPECT_FALSE(lib.LoadFromFile("nope.json"));
    EXPECT_EQ(lib.Find("p", "a"), nullptr);
    EXPECT_EQ(lib.FindPlayer(nullptr), nullptr);
}

TEST(Animation, EmptyTrackHasNoLength) {
    Track none;
    EXPECT_TRUE(none.Empty());
    EXPECT_FLOAT_EQ(none.Length(), 0.0f);
}

// Godot uses a track's update mode, its loop wrap and each key's transition when
// sampling; the exporter dropped all three, so a discrete track played as if
// continuous and every eased key played linear.
TEST(AnimationLibraryV2, ReadsUpdateModeLoopWrapAndKeyTransition) {
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromString(R"({"format":2,"players":[{"path":"p","root":".","autoplay":"",
      "animations":[{"name":"a","length":1,"loop":0,"step":0.1,"tracks":[
        {"type":0,"path":"n:visible","interp":1,"update":1,"loop_wrap":false,
         "keys":[{"t":0,"tr":1,"k":"b","v":false},{"t":0.5,"tr":0.25,"k":"b","v":true}]}
      ]}]}]})"));
    const Track& t = lib.Players()[0].animations[0].tracks[0];
    EXPECT_EQ(t.updateMode, UpdateMode::Discrete);
    EXPECT_FALSE(t.loopWrap);
    EXPECT_FLOAT_EQ(t.keys[0].transition, 1.0f);
    EXPECT_FLOAT_EQ(t.keys[1].transition, 0.25f);
}

TEST(AnimationLibraryV2, RefusesAnExportWithoutTheFormatStamp) {
    // An old animations.json would load with every track continuous and every
    // key linear: plausible motion, wrong motion. Fail loudly instead.
    AnimationLibrary lib;
    EXPECT_FALSE(lib.LoadFromString(
        R"({"players":[{"path":"p","root":".","autoplay":"","animations":[]}]})"));
}

// ------------------------------------------------- the real exported data

TEST(Animation, LoadsTheRealLibraryAndMatchesTheExport) {
    BSHT_NEEDS_FILE(RealPath());
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromFile(RealPath().c_str()))
        << "run tools/godot/export_animations.gd first";

    const AnimationStats& s = lib.Stats();
    EXPECT_EQ(s.players, 60);
    EXPECT_EQ(s.animations, 197);
    EXPECT_EQ(s.tracks, 1027);
    EXPECT_EQ(s.keys, 4327);
    EXPECT_EQ(s.unsupportedTracks, 0)
        << "an unsupported track type appeared; it would silently never play";

    // Animations call game code through METHOD keys; counted from the real data.
    std::set<std::string> methods;
    for (const AnimationPlayerData& p : lib.Players()) {
        for (const Animation& a : p.animations) {
            for (const Track& t : a.tracks) {
                if (t.type != TrackType::Method) continue;
                for (const Key& k : t.keys) methods.insert(k.method);
            }
        }
    }
    EXPECT_EQ(s.methodKeys, 40);   // v2.2 adds PipelineManager.Toggle
    EXPECT_EQ(methods.size(), 34u)
        << "the METHOD dispatch table changed size; trap 4 needs rechecking";

    // 43 AUDIO *tracks* carry 58 AUDIO *keys*. A count of track types
    // reports the former; conflating the two is the same mistake as counting
    // glTF meshes when the renderable unit is a primitive.
    EXPECT_EQ(s.audioKeys, 58);

    int audioTracks = 0;
    for (const AnimationPlayerData& p : lib.Players()) {
        for (const Animation& a : p.animations) {
            for (const Track& t : a.tracks) {
                if (t.type == TrackType::Audio) ++audioTracks;
            }
        }
    }
    EXPECT_EQ(audioTracks, 43) << "audio track count drifted from the export";
}

TEST(Animation, EveryRealValueTrackSamplesWithoutProducingNaN) {
    BSHT_NEEDS_FILE(RealPath());
    // The cheapest possible protection against a bad key breaking playback on
    // a device with no debugger: sample every value track in the game through
    // the sampler AnimationPlayer uses.
    AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromFile(RealPath().c_str()));

    int sampled = 0;
    for (const AnimationPlayerData& p : lib.Players()) {
        for (const Animation& a : p.animations) {
            for (std::size_t ti = 0; ti < a.tracks.size(); ++ti) {
                const Track& t = a.tracks[ti];
                if (t.type != TrackType::Value || t.Empty()) continue;
                const double len = a.length > 0.0f ? a.length : 1.0;
                for (int i = 0; i <= 8; ++i) {
                    Value v;
                    if (!godot::value_track_interpolate(a, static_cast<int>(ti),
                                                        len * i / 8.0, v)) {
                        continue;
                    }
                    for (int c = 0; c < 4; ++c) {
                        ASSERT_TRUE(std::isfinite(v.f[c]))
                            << "NaN sampling " << a.name << " track " << t.path;
                    }
                    ++sampled;
                }
            }
        }
    }
    EXPECT_GT(sampled, 5000);
}
