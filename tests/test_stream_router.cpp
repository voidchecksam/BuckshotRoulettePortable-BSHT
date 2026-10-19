// Every AudioStreamPlayer of a file the bank does not hold streams on one of
// the platform's stream voices. The router watches those players and says what
// the voices should do; the audio sink reports Godot's own length.
#include "game/stream_router.h"

#include "assets/audio_streams.h"
#include "godot/node.h"

#include "godot/scene_tree.h"

#include <gtest/gtest.h>

#include <memory>

#include <string>
#include <vector>

namespace {

using game::StreamRouter;

assets::AudioStreams Info() {
    assets::AudioStreams s;
    s.Parse(R"({"streams":[{"name":"car drive ambience.ogg","length":26.5,"loop":true},)"
            R"({"name":"god.ogg","length":13.5,"loop":false},)"
            R"({"name":"club ambience1.ogg","length":43.4,"loop":true},)"
            R"({"name":"crt_bootup2.ogg","length":67.2,"loop":true}]})");
    return s;
}

std::string Path(const std::string& name) { return "data/" + name; }

struct StreamRouterTest : ::testing::Test {
    assets::AudioStreams info = Info();
    StreamRouter router{&info, &Path};
};

TEST_F(StreamRouterTest, APlayStartsAVoiceWithTheStreamsLoopAndGain) {
    godot::AudioStreamPlayer car("speaker_car");
    car.set_stream("car drive ambience.ogg");
    car.set_volume_db(-6.0f);
    EXPECT_EQ(router.OnPlay(&car), StreamRouter::kNeverEnds) << "a looping stream never ends";
    car.set_playing(true);
    const std::vector<StreamRouter::Change> c = router.Poll();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].kind, StreamRouter::Kind::Start);
    EXPECT_EQ(c[0].path, "data/car drive ambience.ogg");
    EXPECT_TRUE(c[0].loop);
    EXPECT_NEAR(c[0].gain.l, 0.501f, 1e-3f);
    EXPECT_NEAR(c[0].gain.r, 0.501f, 1e-3f);
    EXPECT_GE(c[0].voice, 0);
    EXPECT_LT(c[0].voice, StreamRouter::kVoices);
    EXPECT_TRUE(router.Poll().empty()) << "nothing changed since";
}

// Every player in the game is 2D or 3D; the voice gets Godot's mix gain.
TEST_F(StreamRouterTest, ATwoDPlayerStartsAtGodotsSpatialGain) {
    godot::AudioStreamPlayer car("speaker_car");
    car.set_space(godot::AudioSpace::Flat2D);
    godot::Spatial2D s;
    s.panning_strength = 0.0f;
    car.set_spatial_2d(s);
    car.set_stream("car drive ambience.ogg");
    router.OnPlay(&car);
    car.set_playing(true);
    const std::vector<StreamRouter::Change> c = router.Poll();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_NEAR(c[0].gain.l, 0.362318f, 1e-4f);   // 0.5 * (1 - 550.7 / 2000)
    EXPECT_NEAR(c[0].gain.r, 0.362318f, 1e-4f);
}

// the true-death track is on the "music" bus with MusicManager's,
// so it is muffled at the table too (the bus's low-pass).
TEST_F(StreamRouterTest, AMusicBusPlayerAsksForTheMusicLowPass) {
    godot::AudioStreamPlayer death("seaker_main music_true death");
    death.set_bus("music");
    death.set_stream("car drive ambience.ogg");
    router.OnPlay(&death);
    death.set_playing(true);
    const std::vector<StreamRouter::Change> c = router.Poll();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_TRUE(c[0].musicBus);
    godot::AudioStreamPlayer car("speaker_car");
    car.set_stream("club ambience1.ogg");
    router.OnPlay(&car);
    car.set_playing(true);
    const std::vector<StreamRouter::Change> d = router.Poll();
    ASSERT_EQ(d.size(), 1u);
    EXPECT_FALSE(d[0].musicBus);
}

TEST_F(StreamRouterTest, AOneShotReportsItsRealLength) {
    godot::AudioStreamPlayer god("speaker_god");
    god.set_stream("god.ogg");
    EXPECT_FLOAT_EQ(router.OnPlay(&god), 13.5f);
}

TEST_F(StreamRouterTest, AnUnknownStreamStillPlaysWithAnUnknownLength) {
    godot::AudioStreamPlayer p("speaker");
    p.set_stream("missing.ogg");
    EXPECT_FLOAT_EQ(router.OnPlay(&p), 0.0f) << "no length known: ended at once, as before";
    p.set_playing(true);
    EXPECT_TRUE(router.Poll().empty()) << "no voice for a stream nobody exported";
}

TEST_F(StreamRouterTest, StoppingThePlayerStopsItsVoice) {
    godot::AudioStreamPlayer car("speaker_car");
    car.set_stream("car drive ambience.ogg");
    router.OnPlay(&car);
    car.set_playing(true);
    const int voice = router.Poll().at(0).voice;
    car.set_playing(false);
    const std::vector<StreamRouter::Change> c = router.Poll();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].kind, StreamRouter::Kind::Stop);
    EXPECT_EQ(c[0].voice, voice);
    EXPECT_TRUE(router.Poll().empty());
}

TEST_F(StreamRouterTest, AVolumeFadeBecomesGainChanges) {
    godot::AudioStreamPlayer car("speaker_car");
    car.set_stream("car drive ambience.ogg");
    router.OnPlay(&car);
    car.set_playing(true);
    router.Poll();
    car.set_volume_db(-80.0f);
    const std::vector<StreamRouter::Change> c = router.Poll();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].kind, StreamRouter::Kind::Gain);
    EXPECT_NEAR(c[0].gain.l, 0.0f, 1e-3f);
}

TEST_F(StreamRouterTest, PlayingAgainRestartsTheVoice) {
    godot::AudioStreamPlayer god("speaker_god");
    god.set_stream("god.ogg");
    router.OnPlay(&god);
    god.set_playing(true);
    const int voice = router.Poll().at(0).voice;
    router.OnPlay(&god);
    const std::vector<StreamRouter::Change> c = router.Poll();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].kind, StreamRouter::Kind::Start);
    EXPECT_EQ(c[0].voice, voice);
}

// More streams than voices: the oldest voice is taken over.
TEST_F(StreamRouterTest, MoreStreamsThanVoicesTakesOverTheOldest) {
    std::vector<std::unique_ptr<godot::AudioStreamPlayer>> players;
    const char* names[] = {"car drive ambience.ogg", "club ambience1.ogg", "crt_bootup2.ogg", "god.ogg"};
    std::vector<int> voices;
    for (int i = 0; i <= StreamRouter::kVoices; ++i) {
        players.push_back(std::make_unique<godot::AudioStreamPlayer>("p" + std::to_string(i)));
        players.back()->set_stream(names[i % 4]);
        router.OnPlay(players.back().get());
        players.back()->set_playing(true);
        for (const StreamRouter::Change& c : router.Poll()) {
            if (c.kind == StreamRouter::Kind::Start) voices.push_back(c.voice);
        }
    }
    ASSERT_EQ(voices.size(), static_cast<std::size_t>(StreamRouter::kVoices + 1));
    EXPECT_EQ(voices.back(), voices.front()) << "the newest takes the oldest's voice";
    // The player that lost its voice is no longer tracked: stopping it stops nothing.
    players.front()->set_playing(false);
    EXPECT_TRUE(router.Poll().empty());
}

// A scene change stops every voice.
TEST_F(StreamRouterTest, ResetStopsEveryVoice) {
    godot::AudioStreamPlayer car("speaker_car");
    car.set_stream("car drive ambience.ogg");
    router.OnPlay(&car);
    car.set_playing(true);
    router.Poll();
    router.Reset();
    const std::vector<StreamRouter::Change> c = router.Poll();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].kind, StreamRouter::Kind::Stop);
}

// A freed player is pruned without being read.
TEST_F(StreamRouterTest, PruneForgetsAFreedPlayer) {
    auto car = std::make_unique<godot::AudioStreamPlayer>("speaker_car");
    car->set_stream("car drive ambience.ogg");
    router.OnPlay(car.get());
    car->set_playing(true);
    router.Poll();
    const godot::AudioPlayback* gone = car.get();
    car.reset();
    router.Prune([gone](const godot::AudioPlayback* p) { return p != gone; });
    const std::vector<StreamRouter::Change> c = router.Poll();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].kind, StreamRouter::Kind::Stop);
}

}  // namespace

// The platform starts a stream a load later than play(), and the sink counts
// its length from play(): a Stop when Godot's length runs out would cut the
// file's tail. A one-shot that ran out finishes on its own.
TEST_F(StreamRouterTest, AOneShotThatRanOutFinishesOnItsOwn) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
    t.set_audio_sink([this](const godot::AudioPlayback& a, const godot::AudioPlayRequest&) {
        return godot::AudioPlayResult{router.OnPlay(&a), -1};
    });
    auto* god = static_cast<godot::AudioStreamPlayer*>(
        t.add_node(std::make_unique<godot::AudioStreamPlayer>("speaker_god"), root));
    god->set_stream("god.ogg");   // 13.5 s, not looping
    god->play();
    ASSERT_EQ(router.Poll().size(), 1u);
    for (int i = 0; i < 60 * 14; ++i) t.process_frame(1.0 / 60.0);   // past its length
    ASSERT_FALSE(god->is_playing());
    EXPECT_TRUE(router.Poll().empty()) << "no Stop: the voice plays the file's tail";
}

TEST_F(StreamRouterTest, AnExplicitStopOfAOneShotStillStopsItsVoice) {
    godot::AudioStreamPlayer god("speaker_god");
    god.set_stream("god.ogg");
    router.OnPlay(&god);
    god.set_playing(true);
    const int voice = router.Poll().at(0).voice;
    god.stop();
    const std::vector<StreamRouter::Change> c = router.Poll();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].kind, StreamRouter::Kind::Stop);
    EXPECT_EQ(c[0].voice, voice);
}

// The voice still playing a ran-out tail is the last an idle pick takes: a
// truly idle voice comes first.
TEST_F(StreamRouterTest, ANewStreamLeavesARanOutTailAlone) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
    t.set_audio_sink([this](const godot::AudioPlayback& a, const godot::AudioPlayRequest&) {
        return godot::AudioPlayResult{router.OnPlay(&a), -1};
    });
    auto* god = static_cast<godot::AudioStreamPlayer*>(
        t.add_node(std::make_unique<godot::AudioStreamPlayer>("speaker_god"), root));
    god->set_stream("god.ogg");
    god->play();
    const int tailVoice = router.Poll().at(0).voice;
    for (int i = 0; i < 60 * 14; ++i) t.process_frame(1.0 / 60.0);
    ASSERT_TRUE(router.Poll().empty());
    auto* club = static_cast<godot::AudioStreamPlayer*>(
        t.add_node(std::make_unique<godot::AudioStreamPlayer>("speaker_club"), root));
    club->set_stream("club ambience1.ogg");
    club->play();
    const std::vector<StreamRouter::Change> c = router.Poll();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_NE(c[0].voice, tailVoice) << "voices 1 and 2 were free";
}

// A scene change silences a ran-out tail too: Godot frees the player.
TEST_F(StreamRouterTest, ResetStopsARanOutTail) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
    t.set_audio_sink([this](const godot::AudioPlayback& a, const godot::AudioPlayRequest&) {
        return godot::AudioPlayResult{router.OnPlay(&a), -1};
    });
    auto* god = static_cast<godot::AudioStreamPlayer*>(
        t.add_node(std::make_unique<godot::AudioStreamPlayer>("speaker_god"), root));
    god->set_stream("god.ogg");
    god->play();
    const int tailVoice = router.Poll().at(0).voice;
    for (int i = 0; i < 60 * 14; ++i) t.process_frame(1.0 / 60.0);
    ASSERT_TRUE(router.Poll().empty());
    router.Reset();
    const std::vector<StreamRouter::Change> c = router.Poll();
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].kind, StreamRouter::Kind::Stop);
    EXPECT_EQ(c[0].voice, tailVoice);
}
