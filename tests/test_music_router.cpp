// The batch tracks and the briefcase ducking. MusicManager.LoadTrack picks
// trackArray[batch] and ItemManager.GrabSpook snaps the music volume down and
// fades it back, but the SFX bank holds no music: the router watches the music
// player and says what the platform streamer should do.
#include "app/data_layout.h"
#include "game/music_router.h"

#include "godot/node.h"
#include "godot/scene_tree.h"

#include <memory>

#include <gtest/gtest.h>

namespace {

constexpr float kSilentDb = -80.0f;

// MusicManager's speaker is an AudioStreamPlayer2D at the canvas origin with
// panning_strength 0: Godot mixes the track at half on each side, 0.72 for the
// 550 px to the screen centre.
TEST(MusicRouter, TheTwoDMusicPlayerPlaysAtGodotsSpatialGain) {
    godot::AudioStreamPlayer player("speaker_main music");
    player.set_space(godot::AudioSpace::Flat2D);
    godot::Spatial2D s;
    s.panning_strength = 0.0f;
    player.set_spatial_2d(s);
    player.set_volume_db(7.377f);
    game::MusicRouter router("app0:data", "");
    router.Bind(&player);
    player.set_stream("music main_error.ogg");
    player.play();
    const game::MusicRouter::Change c = router.Poll();
    EXPECT_NEAR(c.gain.l, 0.362318f * 2.338f, 1e-3f);   // 10^(7.377/20) = 2.338
    EXPECT_NEAR(c.gain.r, 0.362318f * 2.338f, 1e-3f);
}

TEST(MusicRouter, PlayingATrackAsksForItsFileUnderTheMusicDirectory) {
    godot::AudioStreamPlayer player("speaker_music");
    game::MusicRouter router("app0:data", "");
    router.Bind(&player);

    player.set_stream("music main_error.ogg");
    player.play();
    const game::MusicRouter::Change c = router.Poll();
    EXPECT_EQ(c.kind, game::MusicRouter::Kind::Play);
    EXPECT_EQ(c.path, "app0:data/music main_error.ogg");
    EXPECT_NEAR(c.gain.l, 1.0f, 1e-6f);
    EXPECT_NEAR(c.gain.r, 1.0f, 1e-6f);
    // Nothing changed since: no second call to the streamer.
    EXPECT_EQ(router.Poll().kind, game::MusicRouter::Kind::None);
}

TEST(MusicRouter, TheDesktopLayoutKeepsItsOwnSubdirectory) {
    godot::AudioStreamPlayer player("speaker_music");
    game::MusicRouter router(".", "audio/music");
    router.Bind(&player);
    player.set_stream("music main_techno techno.ogg");
    player.play();
    EXPECT_EQ(router.Poll().path, "./audio/music/music main_techno techno.ogg");
}

// SpeakerController fades volume_db over several frames (the briefcase
// ducking); the streamer has to follow it after play() too.
TEST(MusicRouter, AVolumeFadeBecomesGainChanges) {
    godot::AudioStreamPlayer player("speaker_music");
    game::MusicRouter router("data", "");
    router.Bind(&player);
    player.set_stream("music main_room.ogg");
    player.play();
    ASSERT_EQ(router.Poll().kind, game::MusicRouter::Kind::Play);

    player.set_volume_db(kSilentDb);
    const game::MusicRouter::Change quiet = router.Poll();
    EXPECT_EQ(quiet.kind, game::MusicRouter::Kind::Gain);
    EXPECT_NEAR(quiet.gain.l, 0.0f, 1e-3f);

    player.set_volume_db(-6.0f);
    const game::MusicRouter::Change back = router.Poll();
    EXPECT_EQ(back.kind, game::MusicRouter::Kind::Gain);
    EXPECT_NEAR(back.gain.l, 0.5012f, 1e-3f);   // 10^(-6/20)
}

TEST(MusicRouter, StoppingTheTrackStopsTheStream) {
    godot::AudioStreamPlayer player("speaker_music");
    game::MusicRouter router("data", "");
    router.Bind(&player);
    player.set_stream("music main_room.ogg");
    player.play();
    router.Poll();
    player.stop();
    EXPECT_EQ(router.Poll().kind, game::MusicRouter::Kind::Stop);
    EXPECT_EQ(router.Poll().kind, game::MusicRouter::Kind::None);
}

TEST(MusicRouter, SwappingTheTrackPlaysTheNewOne) {
    godot::AudioStreamPlayer player("speaker_music");
    game::MusicRouter router("data", "");
    router.Bind(&player);
    player.set_stream("music main_techno techno.ogg");
    player.play();
    router.Poll();
    player.set_stream("music main_error.ogg");   // MusicManager.LoadTrack, batch 2
    player.play();
    const game::MusicRouter::Change c = router.Poll();
    EXPECT_EQ(c.kind, game::MusicRouter::Kind::Play);
    EXPECT_EQ(c.path, "data/music main_error.ogg");
}

// The loading screen starts the menu's track before the menu exists. When
// MenuManager plays it, the stream goes on at the player's gain instead of
// starting again from the top.
TEST(MusicRouter, AnAdoptedTrackIsNotStartedAgain) {
    godot::AudioStreamPlayer player("speaker_music");
    game::MusicRouter router("data", "");
    router.Bind(&player);
    router.AdoptRunning("data/music main_gate modded.ogg");
    player.set_stream("music main_gate modded.ogg");
    player.play();

    const game::MusicRouter::Change c = router.Poll();
    EXPECT_EQ(c.kind, game::MusicRouter::Kind::Adopt);
    EXPECT_EQ(c.path, "data/music main_gate modded.ogg");
    EXPECT_FLOAT_EQ(c.gain.l, godot::OutputGain(player).l);
    EXPECT_EQ(router.Poll().kind, game::MusicRouter::Kind::None);
}

TEST(MusicRouter, OnlyTheFirstTrackIsAdopted) {
    godot::AudioStreamPlayer player("speaker_music");
    game::MusicRouter router("data", "");
    router.Bind(&player);
    router.AdoptRunning("data/music main_gate modded.ogg");
    player.set_stream("music main_gate modded.ogg");
    player.play();
    router.Poll();
    player.set_stream("music main_error.ogg");
    player.play();
    EXPECT_EQ(router.Poll().kind, game::MusicRouter::Kind::Play);
}

TEST(MusicRouter, BindingAgainForgetsTheAdoption) {
    godot::AudioStreamPlayer player("speaker_music");
    game::MusicRouter router("data", "");
    router.Bind(&player);
    router.AdoptRunning("data/music main_gate modded.ogg");
    router.Bind(&player);   // a new scene's player
    player.set_stream("music main_gate modded.ogg");
    player.play();
    EXPECT_EQ(router.Poll().kind, game::MusicRouter::Kind::Play);
}

// The loading screen may have started another file (or none): only the same
// track goes on; anything else starts as usual.
TEST(MusicRouter, AdoptsOnlyTheTrackThatIsPlaying) {
    godot::AudioStreamPlayer player("speaker_music");
    game::MusicRouter router("data", "");
    router.Bind(&player);
    router.AdoptRunning("data/music main_error.ogg");
    player.set_stream("music main_gate modded.ogg");
    player.play();
    EXPECT_EQ(router.Poll().kind, game::MusicRouter::Kind::Play);
}

// The Vita's tracks carry underscores for spaces and live elsewhere; the file
// name is what has to match.
TEST(MusicRouter, AdoptsTheSameTrackUnderItsDeviceName) {
    godot::AudioStreamPlayer player("speaker_music");
    game::MusicRouter router("ux0:data/bsht/game", "");
    router.SetSpaceFreeNames(true);
    router.Bind(&player);
    router.AdoptRunning("ux0:data/bsht/game/music_main_gate_modded.ogg");
    player.set_stream("music main_gate modded.ogg");
    player.play();
    EXPECT_EQ(router.Poll().kind, game::MusicRouter::Kind::Adopt);
}

// The track the loading screen starts from bsht-extract's game folder is the
// one MenuManager then asks for, so it goes on rather than restarting.
TEST(MusicRouter, AdoptsTheGameFoldersMenuTrack) {
    const app::DataLayout layout = app::GameFolderLayout("ux0:data/bsht/game", "app0:data");
    godot::AudioStreamPlayer player("speaker_music");
    game::MusicRouter router(layout.musicDir, "");
    router.SetSpaceFreeNames(layout.spaceFreeMusic);
    router.Bind(&player);
    router.AdoptRunning(layout.menuMusic);
    player.set_stream("music main_gate modded.ogg");
    player.play();
    EXPECT_EQ(router.Poll().kind, game::MusicRouter::Kind::Adopt);
}

TEST(MusicRouter, WithNoPlayerBoundNothingHappens) {
    game::MusicRouter router("data", "");
    EXPECT_EQ(router.Poll().kind, game::MusicRouter::Kind::None);
}

}  // namespace

// The audio sink reports a length for one-shot SFX, and AudioPlayback::tick
// stops a player whose length has run out, so a music player given one would
// stop a frame after MusicManager pressed play, before the router polled it.
// A looping stream keeps playing until something calls stop(), as in Godot.
TEST(MusicRouter, TheTrackSurvivesTheFrameThatStartedIt) {
    godot::SceneTree tree;
    auto* player = godot::cast_to<godot::AudioStreamPlayer>(
        tree.add_node(std::make_unique<godot::AudioStreamPlayer>("speaker_music"), nullptr));
    ASSERT_NE(player, nullptr);
    game::MusicRouter router("data", "");
    router.Bind(player);
    // The sink app::GameApp installs: music reports the router's length.
    tree.set_audio_sink([&router](const godot::AudioPlayback& a, const godot::AudioPlayRequest&) {
        return godot::AudioPlayResult{router.IsMusic(&a) ? router.OnPlay() : 0.0f, -1};
    });

    player->set_stream("music main_techno techno.ogg");
    player->play();
    for (int f = 0; f < 120; ++f) tree.process_frame(1.0 / 60.0);

    EXPECT_TRUE(player->is_playing()) << "the music stopped by itself";
    EXPECT_EQ(router.Poll().kind, game::MusicRouter::Kind::Play);
}

// FilterController pans the music bus's cutoff (156 Hz at the table, open in
// the lobby). The router carries that to the mixer, which is what makes the
// music muffled in one room and bright in the next.
TEST(MusicRouter, TheFilterCutoffReachesTheMixer) {
    godot::AudioStreamPlayer player("speaker_music");
    game::MusicRouter router("data", "");
    router.Bind(&player);
    double cutoff = 156.0;
    router.BindFilter(&cutoff);
    player.set_stream("music main_room.ogg");
    player.play();

    const game::MusicRouter::Change play = router.Poll();
    ASSERT_EQ(play.kind, game::MusicRouter::Kind::Play);
    EXPECT_NEAR(play.cutoffHz, 156.0f, 1e-3f) << "a new track starts at its own cutoff";

    cutoff = 8000.0;   // BeginPan towards lowPassMaxValue
    const game::MusicRouter::Change opened = router.Poll();
    EXPECT_EQ(opened.kind, game::MusicRouter::Kind::Cutoff);
    EXPECT_NEAR(opened.cutoffHz, 8000.0f, 1e-3f);
    EXPECT_EQ(router.Poll().kind, game::MusicRouter::Kind::None);
}

// vita-pack-vpk builds its command line without quoting, so a file name with
// a space arrives as two arguments (docs/TOOLCHAIN.md). The batch tracks are
// all "music main_techno techno.ogg" and friends, so the vpk stages them with
// underscores and the device asks for that name.
TEST(MusicRouter, TheDeviceAsksForTheSpaceFreeName) {
    godot::AudioStreamPlayer player("speaker_music");
    game::MusicRouter router("app0:data", "");
    router.SetSpaceFreeNames(true);
    router.Bind(&player);
    player.set_stream("music main second loop_techno techno.ogg");
    player.play();
    EXPECT_EQ(router.Poll().path, "app0:data/music_main_second_loop_techno_techno.ogg");
}
