// Godot 4.1.4's spatial mix, transcribed: what each AudioStreamPlayer kind
// sends to the left and right channels. Expected values are worked by hand
// from the engine source cited in godot/audio_spatial.h.
#include "godot/audio_server.h"
#include "godot/audio_spatial.h"
#include "godot/node.h"
#include "godot/scene_tree.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

using godot::Gain2D;
using godot::Gain3D;
using godot::GainPlain;
using godot::Spatial2D;
using godot::Spatial3D;

constexpr float kTol = 1e-4f;
// project.godot: window 960x540, stretch mode "viewport" -> the visible rect.
constexpr godot::Vector2 kScreen{960.0f, 540.0f};

TEST(AudioSpatial, PlainPlayerIsItsVolumeOnBothSides) {
    const godot::StereoGain g = GainPlain(-6.0f);
    EXPECT_NEAR(g.l, 0.501187f, kTol);
    EXPECT_NEAR(g.r, 0.501187f, kTol);
}

// 82 of main's 88 2D players: at the canvas origin, panning_strength 0. The
// listener is the screen centre 550.7 px away (1 - 550.7/2000 = 0.7246), and
// an unpanned 2D player still splits l = 1 - 0.5, r = 0.5.
TEST(AudioSpatial, UnpannedTwoDPlayerAtTheOriginIsHalvedAndAttenuated) {
    Spatial2D s;
    s.panning_strength = 0.0f;
    const godot::StereoGain g = Gain2D(s, 5.0f, kScreen);
    EXPECT_NEAR(g.l, 0.644303f, kTol);
    EXPECT_NEAR(g.r, 0.644303f, kTol);
}

// panning_strength 1 x the project's 2d strength 0.5 x 0.5: x = -480 px from
// the centre pans to 0.375.
TEST(AudioSpatial, PannedTwoDPlayerLeansTowardItsSide) {
    Spatial2D s;   // Godot's defaults: panning_strength 1
    const godot::StereoGain g = Gain2D(s, 0.0f, kScreen);
    EXPECT_NEAR(g.l, 0.452898f, kTol);
    EXPECT_NEAR(g.r, 0.271739f, kTol);
}

TEST(AudioSpatial, TwoDPlayerBeyondMaxDistanceIsSilent) {
    Spatial2D s;
    s.max_distance = 500.0f;
    const godot::StereoGain g = Gain2D(s, 0.0f, kScreen);
    EXPECT_EQ(g.l, 0.0f);
    EXPECT_EQ(g.r, 0.0f);
}

// The health counter's speakers (volume -12.124, unit_size 10) one unit in
// front of the camera: +20 dB of distance gain, clamped to max_db 3. Straight
// ahead SPCAP gives each speaker sqrt(1/2).
TEST(AudioSpatial, NearThreeDPlayerClampsToMaxDb) {
    Spatial3D s;
    s.panning_strength = 0.57f;
    const godot::StereoGain g = Gain3D(s, -12.124f, {0.0f, 0.0f, -1.0f});
    EXPECT_NEAR(g.l, 1.412538f * 0.707107f, kTol);
    EXPECT_NEAR(g.r, 1.412538f * 0.707107f, kTol);
    EXPECT_FLOAT_EQ(g.shelf, 1.0f) << "a gain of 1 or more leaves the shelf flat";
}

// A source to the listener's right (tightness = 0.5 * 2 * 0.57).
TEST(AudioSpatial, ThreeDPlayerToTheRightFavoursTheRightSpeaker) {
    Spatial3D s;
    s.panning_strength = 0.57f;
    s.max_db = 0.0f;   // isolate the pan: the gain clamps to exactly 1
    const godot::StereoGain g = Gain3D(s, 0.0f, {1.0f, 0.0f, 0.0f});
    EXPECT_NEAR(g.l, 0.343811f, kTol);
    EXPECT_NEAR(g.r, 0.939039f, kTol);
}

// The dealer-crash speaker (+8.405 dB) 40 units away: 20 log10(10 / 40).
TEST(AudioSpatial, FarThreeDPlayerFallsOffWithInverseDistance) {
    Spatial3D s;
    s.panning_strength = 0.57f;
    const godot::StereoGain g = Gain3D(s, 8.405f, {0.0f, 0.0f, -40.0f});
    EXPECT_NEAR(g.l, 0.657945f * 0.707107f, kTol);
    EXPECT_NEAR(g.r, 0.657945f * 0.707107f, kTol);
    // :438 db_att = (1 - 0.658) * -24 = -8.21 dB for the attenuation shelf.
    EXPECT_NEAR(g.shelf, 0.388628f, kTol);
    EXPECT_FLOAT_EQ(g.shelf_cutoff_hz, 5000.0f);
}

TEST(AudioSpatial, ThreeDMaxDistanceFadesLinearlyAndCutsOff) {
    Spatial3D s;
    s.max_db = 0.0f;
    s.max_distance = 4.0f;
    s.unit_size = 100.0f;   // distance gain clamped away: only the fade is left
    const godot::StereoGain half = Gain3D(s, 0.0f, {0.0f, 0.0f, -2.0f});
    EXPECT_NEAR(half.l, 0.5f * 0.707107f, kTol);
    const godot::StereoGain out = Gain3D(s, 0.0f, {0.0f, 0.0f, -5.0f});
    EXPECT_EQ(out.l, 0.0f);
    EXPECT_EQ(out.r, 0.0f);
}

// The players in a tree, heard from the current camera: what the audio sink,
// MusicRouter and StreamRouter ask for (godot::OutputGain).
struct SpeakerTree : ::testing::Test {
    godot::SceneTree t;
    godot::Node* root = nullptr;
    godot::Camera3D* cam = nullptr;

    void SetUp() override {
        root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
        cam = static_cast<godot::Camera3D*>(t.add_node(std::make_unique<godot::Camera3D>("Camera"), root));
        cam->set_current(true);   // at the origin, looking down -Z
    }
};

TEST_F(SpeakerTree, ThreeDSpeakerIsHeardFromTheCurrentCamera) {
    auto* speaker = static_cast<godot::AudioStreamPlayer3D*>(
        t.add_node(std::make_unique<godot::AudioStreamPlayer3D>("speaker_health counter round wave"), root));
    speaker->set_position({0.0f, 0.0f, -1.0f});
    speaker->set_volume_db(-12.124f);
    godot::Spatial3D s;
    s.panning_strength = 0.57f;
    speaker->set_spatial_3d(s);
    const godot::StereoGain g = godot::OutputGain(*speaker);
    EXPECT_NEAR(g.l, 1.412538f * 0.707107f, kTol);
    EXPECT_NEAR(g.r, 1.412538f * 0.707107f, kTol);
}

TEST_F(SpeakerTree, ThreeDSpeakerFollowsTheCameraWhenItTurns) {
    auto* speaker = static_cast<godot::AudioStreamPlayer3D*>(
        t.add_node(std::make_unique<godot::AudioStreamPlayer3D>("speaker"), root));
    speaker->set_position({0.0f, 0.0f, -1.0f});
    godot::Spatial3D s;
    s.panning_strength = 0.57f;
    s.max_db = 0.0f;
    speaker->set_spatial_3d(s);
    cam->set_rotation({0.0f, 1.5707963f, 0.0f});   // turned left: -Z now lies to its right
    const godot::StereoGain g = godot::OutputGain(*speaker);
    EXPECT_NEAR(g.l, 0.343811f, kTol);
    EXPECT_NEAR(g.r, 0.939039f, kTol);
}

TEST_F(SpeakerTree, TwoDSpeakerIgnoresTheCamera) {
    auto* speaker = static_cast<godot::AudioStreamPlayer*>(
        t.add_node(std::make_unique<godot::AudioStreamPlayer>("speaker_gunshot"), root));
    speaker->set_space(godot::AudioSpace::Flat2D);
    speaker->set_volume_db(5.0f);
    godot::Spatial2D s;
    s.panning_strength = 0.0f;
    speaker->set_spatial_2d(s);
    const godot::StereoGain g = godot::OutputGain(*speaker);
    EXPECT_NEAR(g.l, 0.644303f, kTol);
    EXPECT_NEAR(g.r, 0.644303f, kTol);
}

TEST_F(SpeakerTree, PlainPlayerIsItsVolume) {
    auto* speaker = static_cast<godot::AudioStreamPlayer*>(
        t.add_node(std::make_unique<godot::AudioStreamPlayer>("plain"), root));
    speaker->set_volume_db(-6.0f);
    const godot::StereoGain g = godot::OutputGain(*speaker);
    EXPECT_NEAR(g.l, 0.501187f, kTol);
    EXPECT_NEAR(g.r, 0.501187f, kTol);
}

// No camera (a scene between changes): Godot mixes a 3D player once per
// listening camera, so with none its volume vector stays zero.
TEST(SpeakerNoCamera, ThreeDSpeakerWithoutACameraIsSilent) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
    auto* speaker = static_cast<godot::AudioStreamPlayer3D*>(
        t.add_node(std::make_unique<godot::AudioStreamPlayer3D>("speaker"), root));
    const godot::StereoGain g = godot::OutputGain(*speaker);
    EXPECT_EQ(g.l, 0.0f);
    EXPECT_EQ(g.r, 0.0f);
}

}  // namespace

// A player owns its playbacks as Godot's does.
namespace {

struct PlaybackTree : ::testing::Test {
    godot::SceneTree t;
    godot::Node* root = nullptr;
    std::vector<godot::AudioPlayRequest> requests;
    std::vector<int> stopped;
    int nextVoice = 100;
    float nextLength = 1.0f;

    void SetUp() override {
        root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
        t.set_audio_sink([this](const godot::AudioPlayback&, const godot::AudioPlayRequest& r) {
            requests.push_back(r);
            return godot::AudioPlayResult{nextLength, nextVoice++};
        });
        t.set_audio_stop_sink([this](int voice) { stopped.push_back(voice); });
    }
    godot::AudioStreamPlayer* Speaker(int polyphony) {
        auto* p = static_cast<godot::AudioStreamPlayer*>(
            t.add_node(std::make_unique<godot::AudioStreamPlayer>("speaker"), root));
        p->set_stream("click.ogg");
        p->set_max_polyphony(polyphony);
        return p;
    }
};

// AudioStreamPlayer::play: at max_polyphony live playbacks the oldest is
// stopped (with Godot's fade) before the new one starts, ramped in.
TEST_F(PlaybackTree, ADirectPlayPastMaxPolyphonyCutsTheOldest) {
    godot::AudioStreamPlayer* p = Speaker(1);
    p->play();
    p->play();
    ASSERT_EQ(requests.size(), 2u);
    EXPECT_TRUE(requests[1].ramp_in);
    ASSERT_EQ(stopped.size(), 1u);
    EXPECT_EQ(stopped[0], 100);
}

TEST_F(PlaybackTree, APolyphonicPlayerLetsPlaysOverlap) {
    godot::AudioStreamPlayer* p = Speaker(60);
    p->play();
    p->play();
    EXPECT_TRUE(stopped.empty());
}

TEST_F(PlaybackTree, StopFadesOutEveryLivePlayback) {
    godot::AudioStreamPlayer* p = Speaker(60);
    p->play();
    p->play();
    p->stop();
    EXPECT_EQ(stopped.size(), 2u);
}

// A playback that has run its length is gone: nothing left to stop.
TEST_F(PlaybackTree, AnEndedPlaybackIsNotStoppedAgain) {
    godot::AudioStreamPlayer* p = Speaker(1);
    p->play();
    for (int i = 0; i < 90; ++i) t.process_frame(1.0 / 60.0);   // 1.5 s > the 1 s length
    p->play();
    EXPECT_TRUE(stopped.empty());
}

// animation_player.cpp:882-912: an audio key starts the player (ramped) if it
// is not playing, else joins its polyphonic playback at full volume; the key's
// start_offset and end_offset travel with it.
TEST_F(PlaybackTree, AnimationSoundsJoinThePlayersPolyphonicPlayback) {
    godot::AudioStreamPlayer* p = Speaker(1);
    p->play_from_animation("round blinker wave.ogg", 0.0645f, 0.0f);
    p->play_from_animation("round blinker wave.ogg", 0.0645f, 0.2f);
    ASSERT_EQ(requests.size(), 2u);
    EXPECT_TRUE(requests[0].ramp_in);
    EXPECT_FALSE(requests[1].ramp_in);
    EXPECT_FLOAT_EQ(requests[0].from, 0.0645f);
    EXPECT_FLOAT_EQ(requests[1].end_offset, 0.2f);
    EXPECT_TRUE(stopped.empty()) << "a polyphonic add never cuts";
    p->stop();
    EXPECT_EQ(stopped.size(), 2u);
    p->play_from_animation("round blinker wave.ogg", 0.0f, 0.0f);
    EXPECT_TRUE(requests.back().ramp_in) << "stopped: the next key starts the player again";
}

// AudioStreamPlaybackPolyphonic::mix returns a full buffer while active: the
// player stays playing after its sub-sounds end, so the round indicator's
// blink (a 0.54 s sound on a 0.6 s loop) ramps only on its first key.
TEST_F(PlaybackTree, ThePolyphonicPlaybackOutlivesItsSounds) {
    godot::AudioStreamPlayer* p = Speaker(1);
    p->play_from_animation("round blinker wave.ogg", 0.0645f, 0.0f);
    for (int i = 0; i < 90; ++i) t.process_frame(1.0 / 60.0);   // past the 1 s sound
    p->play_from_animation("round blinker wave.ogg", 0.0645f, 0.0f);
    ASSERT_EQ(requests.size(), 2u);
    EXPECT_FALSE(requests[1].ramp_in);
}

}  // namespace

// A player's live volume and pitch reach the voices it is playing, once per
// change (SpeakerController3D's fades and pitch drop; a 3D speaker re-panned
// as the camera moves).
namespace {

struct LiveTree : PlaybackTree {
    struct Update { int voice; float l; float r; float pitch; };
    std::vector<Update> updates;
    void SetUp() override {
        PlaybackTree::SetUp();
        t.set_audio_update_sink([this](int voice, godot::StereoGain g, float pitch) {
            updates.push_back({voice, g.l, g.r, pitch});
        });
    }
};

TEST_F(LiveTree, APlayingSpeakersNewPitchAndVolumeReachItsVoice) {
    godot::AudioStreamPlayer* p = Speaker(1);
    p->play();
    t.process_frame(1.0 / 60.0);
    EXPECT_TRUE(updates.empty()) << "nothing changed";
    p->set_pitch_scale(0.5f);
    p->set_volume_db(-6.0f);
    t.process_frame(1.0 / 60.0);
    ASSERT_EQ(updates.size(), 1u);
    EXPECT_EQ(updates[0].voice, 100);
    EXPECT_FLOAT_EQ(updates[0].pitch, 0.5f);
    EXPECT_NEAR(updates[0].l, 0.501187f, 1e-4f);
    t.process_frame(1.0 / 60.0);
    EXPECT_EQ(updates.size(), 1u) << "sent once per change";
}

}  // namespace

// Polyphonic playback and animation sounds.
namespace {

// AudioStreamPlaybackPolyphonic::mix plays each sub-stream at its own
// pitch_scale (play_stream's default, 1) and drops the player's: an
// animation's sound ignores the speaker's pitch (the rack foley at a dealer
// kill, while SpeakerController3D drops that speaker's pitch to 0.1).
TEST_F(LiveTree, AnAnimationsSoundIgnoresTheSpeakersPitch) {
    godot::AudioStreamPlayer* p = Speaker(1);
    p->set_pitch_scale(0.1f);
    p->play_from_animation("player eject shell2.ogg", 0.0f, 0.0f);
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_TRUE(requests[0].polyphonic);
    p->set_volume_db(-6.0f);
    p->set_pitch_scale(0.05f);
    t.process_frame(1.0 / 60.0);
    ASSERT_EQ(updates.size(), 1u);
    EXPECT_FLOAT_EQ(updates[0].pitch, 1.0f) << "the volume reaches it, the pitch does not";
    EXPECT_NEAR(updates[0].l, 0.501187f, 1e-4f);
}

// Playbacks run on the audio clock, not the game's: at time_scale 0.2 (the
// dealer-kill slow motion) a 1 s sound is still gone after 1 s.
TEST_F(PlaybackTree, PlaybacksEndInRealTimeWhateverTheTimeScale) {
    godot::AudioStreamPlayer* p = Speaker(1);
    t.set_time_scale(0.2);
    p->play();
    for (int i = 0; i < 70; ++i) t.process_frame(1.0 / 60.0);
    p->play();
    EXPECT_TRUE(stopped.empty()) << "the first playback ended; nothing is left to cut";
}

// AudioStreamPlayer::set_stream begins with stop().
TEST_F(PlaybackTree, SettingAStreamStopsThePlayer) {
    godot::AudioStreamPlayer* p = Speaker(60);
    p->play();
    p->set_stream("other.ogg");
    EXPECT_EQ(stopped.size(), 1u);
}

// A cut playback's time does not keep the player "playing".
TEST_F(PlaybackTree, ACutPlaybackNoLongerCountsTowardsIsPlaying) {
    godot::AudioStreamPlayer* p = Speaker(1);
    p->play();                               // 1 s
    for (int i = 0; i < 30; ++i) t.process_frame(1.0 / 60.0);
    nextLength = 0.2f;
    p->play();                               // cuts the first; 0.2 s
    for (int i = 0; i < 30; ++i) t.process_frame(1.0 / 60.0);
    EXPECT_FALSE(p->is_playing());
}

// Godot's PREDELETE stops a freed player: its sounds fade, not play on.
TEST_F(PlaybackTree, AFreedSpeakerStopsItsSounds) {
    godot::AudioStreamPlayer* p = Speaker(60);
    p->play();
    p->queue_free();
    t.process_frame(1.0 / 60.0);
    EXPECT_EQ(stopped.size(), 1u);
}

}  // namespace

// v2.2's music option mutes buses 1, 2 and 4 (OptionsManager.ApplySettings_music);
// a player on a muted bus is silent, and Master players are untouched.
TEST_F(SpeakerTree, AMutedBusSilencesItsPlayers) {
    auto* music = static_cast<godot::AudioStreamPlayer*>(
        t.add_node(std::make_unique<godot::AudioStreamPlayer>("speaker_music"), root));
    music->set_bus("music");
    auto* click = static_cast<godot::AudioStreamPlayer*>(
        t.add_node(std::make_unique<godot::AudioStreamPlayer>("speaker_click"), root));
    godot::AudioServer& server = godot::AudioServer::get_singleton();
    EXPECT_EQ(server.get_bus_index("music"), 1);
    EXPECT_EQ(server.get_bus_index("music_resolve"), 4);
    server.set_bus_mute(1, true);
    EXPECT_FLOAT_EQ(godot::OutputGain(*music).l, 0.0f);
    EXPECT_FLOAT_EQ(godot::OutputGain(*click).l, 1.0f);
    server.set_bus_mute(1, false);
    EXPECT_FLOAT_EQ(godot::OutputGain(*music).l, 1.0f);
}

// v2.2's bus layout starts Master at 0.00282 dB (measured from Godot); reset()
// returns to it and unmutes every bus.
TEST(AudioServerLayout, ResetRestoresTheLayoutsVolumeAndMutes) {
    godot::AudioServer& server = godot::AudioServer::get_singleton();
    server.set_bus_volume_db(0, -6.0f);
    server.set_bus_mute(2, true);
    server.reset();
    EXPECT_NEAR(server.get_bus_volume_db(0), 0.00282288f, 1e-7f);
    EXPECT_FALSE(server.is_bus_mute(2));
}

// For the log: which buses are muted (music off mutes 1, 2 and 4), and a mask
// the host compares frame to frame.
TEST(AudioServerLayout, TheMutedBusesReadAsTextAndAMask) {
    godot::AudioServer& server = godot::AudioServer::get_singleton();
    server.reset();
    EXPECT_EQ(server.muted_text(), "none");
    EXPECT_EQ(server.mute_mask(), 0u);
    for (int bus : {1, 2, 4}) server.set_bus_mute(bus, true);
    EXPECT_EQ(server.muted_text(), "music, music_secondary, music_resolve");
    EXPECT_EQ(server.mute_mask(), (1u << 1) | (1u << 2) | (1u << 4));
    server.reset();
}
