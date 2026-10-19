// AnimationPlayer semantics from animation_player.cpp 4.1.4, one rule per test.
// parity_anim_play_semantics is the authority; these stop regressions.
#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/scene_tree.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

// Player root ".": the absolute form animations.json uses.
const char* kLib = R"({"format":2,"players":[{"path":"AP","root":".","autoplay":"","animations":[
  {"name":"a","length":0.5,"loop":0,"step":0,"tracks":[
    {"type":0,"path":"T:position","interp":1,"update":0,"loop_wrap":true,
     "keys":[{"t":0,"tr":1,"k":"v3","v":[0,0,0]},{"t":0.5,"tr":1,"k":"v3","v":[1,0,0]}]},
    {"type":5,"path":"M","interp":1,"loop_wrap":true,
     "keys":[{"t":0,"tr":1,"m":"mark","a":[]},{"t":0.5,"tr":1,"m":"mark","a":[]}]}]}]}]})";

struct Marker : godot::Node {
    explicit Marker(int* count) : godot::Node("M"), count_(count) {}
    bool call(const std::string& m, const std::vector<assets::Value>&) override {
        if (m != "mark") return false;
        ++*count_;
        return true;
    }
    int* count_;
};

struct Rig {
    godot::SceneTree tree;
    assets::AnimationLibrary lib;
    godot::AnimationPlayer* ap = nullptr;
    godot::Node3D* t = nullptr;
    int marks = 0;
    Rig() {
        EXPECT_TRUE(lib.LoadFromString(kLib));
        godot::Node* root = tree.add_node(std::make_unique<godot::Node>("root"), nullptr);
        ap = godot::cast_to<godot::AnimationPlayer>(
            tree.add_node(std::make_unique<godot::AnimationPlayer>("AP"), root));
        t = godot::cast_to<godot::Node3D>(tree.add_node(std::make_unique<godot::Node3D>("T"), root));
        tree.add_node(std::make_unique<Marker>(&marks), root);
        ap->set_animations(&lib.Players()[0]);
    }
    void frames(int n) {
        for (int i = 0; i < n; ++i) tree.process_frame(1.0 / 60.0);
    }
};

}  // namespace

TEST(GodotAnimationPlayer, PlayProcessesOnTheNextFrameAndDefersMethods) {
    Rig r;
    r.ap->play("a");
    EXPECT_EQ(r.marks, 0) << "play() itself processes nothing";
    r.frames(1);
    EXPECT_EQ(r.marks, 1) << "the key at 0 fires on the first processed frame";
    EXPECT_NEAR(r.t->get_position().x, (1.0 / 60.0) / 0.5, 1e-5);
}

TEST(GodotAnimationPlayer, ReplayingTheSameAnimationContinues) {
    Rig r;
    r.ap->play("a");
    r.frames(10);
    const double pos = r.ap->get_current_animation_position();
    r.ap->play("a");
    EXPECT_DOUBLE_EQ(r.ap->get_current_animation_position(), pos);
}

TEST(GodotAnimationPlayer, ANonLoopingAnimationHoldsItsEndAndStops) {
    Rig r;
    r.ap->play("a");
    r.frames(40);
    EXPECT_FALSE(r.ap->is_playing());
    EXPECT_DOUBLE_EQ(r.ap->get_current_animation_position(), 0.5);
    EXPECT_FLOAT_EQ(r.t->get_position().x, 1.0f);
    EXPECT_EQ(r.marks, 2) << "keys at 0 and at the end, each once";
    r.ap->play("a");   // replay after the end restarts from 0
    EXPECT_DOUBLE_EQ(r.ap->get_current_animation_position(), 0.0);
}

TEST(GodotAnimationPlayer, StopSeeksToZeroWithoutCallingMethods) {
    // _stop_internal(reset=true): is_stopping, seek(0, true), then no animation.
    Rig r;
    r.ap->play("a");
    r.frames(10);
    ASSERT_GT(r.t->get_position().x, 0.1f);
    r.ap->stop();
    EXPECT_FALSE(r.ap->is_playing());
    EXPECT_FLOAT_EQ(r.t->get_position().x, 0.0f);
    r.frames(1);
    EXPECT_EQ(r.marks, 1) << "stop's seek must not re-fire the key at 0";
}

// animation_player.cpp _node_removed: a bound node that leaves the tree clears
// the caches, so the next play re-binds by path and finds nothing; it never
// writes through the freed node (the signed waiver).
TEST(GodotAnimationPlayer, AFreedTargetIsReboundAsMissingNotWrittenThrough) {
    Rig r;
    r.ap->play("a");
    r.frames(40);
    ASSERT_EQ(r.ap->unresolved_tracks(), 0);
    r.t->queue_free();
    r.frames(1);
    r.ap->play("a");
    r.frames(2);
    EXPECT_EQ(r.ap->unresolved_tracks(), 1) << "T:position re-bound to a node that is gone";
}

TEST(GodotAnimationPlayer, AnUnknownAnimationIsCountedNotSilent) {
    Rig r;
    r.ap->play("nope");
    EXPECT_FALSE(r.ap->is_playing());
    EXPECT_EQ(r.tree.animation_stats().missing, 1);
}

// ItemInteraction waits `get_animation("player use magnifier").get_length() + .2`
// before handing control back; the length comes from the animation itself.
TEST(GodotAnimationPlayer, GetAnimationReturnsTheNamedAnimationOrNull) {
    assets::AnimationPlayerData data;
    data.animations.resize(2);
    data.animations[0].name = "a";
    data.animations[0].length = 1.5f;
    data.animations[1].name = "player use magnifier";
    data.animations[1].length = 4.65f;
    godot::AnimationPlayer player("p");
    player.set_animations(&data);
    ASSERT_NE(player.get_animation("player use magnifier"), nullptr);
    EXPECT_FLOAT_EQ(player.get_animation("player use magnifier")->length, 4.65f);
    EXPECT_EQ(player.get_animation("missing"), nullptr);
}

// menu.tscn animates Controls: the splash fades a ColorRect's `color` and
// hides a TextureRect (`visible`), the credits scroll a Control's `position`,
// and the volume text flickers through its font_color override.
namespace {

const char* kControlLib = R"({"format":2,"players":[{"path":"AP","root":".","autoplay":"","animations":[
  {"name":"c","length":1.0,"loop":0,"step":0,"tracks":[
    {"type":0,"path":"VB:color","interp":1,"update":0,"loop_wrap":true,
     "keys":[{"t":0,"tr":1,"k":"c","v":[0,0,0,1]},{"t":1,"tr":1,"k":"c","v":[0,0,0,0]}]},
    {"type":0,"path":"R:visible","interp":1,"update":1,"loop_wrap":true,
     "keys":[{"t":0,"tr":1,"k":"b","v":true},{"t":0.5,"tr":1,"k":"b","v":false}]},
    {"type":0,"path":"P:position","interp":1,"update":0,"loop_wrap":true,
     "keys":[{"t":0,"tr":1,"k":"v2","v":[0,580]},{"t":1,"tr":1,"k":"v2","v":[0,-1378]}]},
    {"type":0,"path":"L:theme_override_colors/font_color","interp":1,"update":0,"loop_wrap":true,
     "keys":[{"t":0,"tr":1,"k":"c","v":[1,1,1,1]},{"t":1,"tr":1,"k":"c","v":[1,1,1,0.25]}]}]}]}]})";

}  // namespace

TEST(GodotAnimationPlayer, ValueTracksDriveControls) {
    godot::SceneTree tree;
    assets::AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromString(kControlLib));
    godot::Node* root = tree.add_node(std::make_unique<godot::Node>("root"), nullptr);
    auto* ap = godot::cast_to<godot::AnimationPlayer>(
        tree.add_node(std::make_unique<godot::AnimationPlayer>("AP"), root));
    auto* vb = godot::cast_to<godot::ColorRect>(tree.add_node(std::make_unique<godot::ColorRect>("VB"), root));
    auto* r = godot::cast_to<godot::Control>(tree.add_node(std::make_unique<godot::TextureRect>("R"), root));
    auto* p = godot::cast_to<godot::Control>(tree.add_node(std::make_unique<godot::Control>("P"), root));
    auto* l = godot::cast_to<godot::Label>(tree.add_node(std::make_unique<godot::Label>("L"), root));
    ap->set_animations(&lib.Players()[0]);
    EXPECT_TRUE(ap->unresolved_paths().empty());

    ap->play("c");
    for (int i = 0; i < 61; ++i) tree.process_frame(1.0 / 60.0);
    EXPECT_TRUE(ap->unresolved_paths().empty()) << ap->unresolved_paths().front();
    EXPECT_NEAR(vb->get_color().a, 0.0f, 1e-4f);
    EXPECT_FALSE(r->is_visible());
    EXPECT_NEAR(p->get_position().y, -1378.0f, 1e-3f);
    ASSERT_TRUE(l->has_font_color_override());
    EXPECT_NEAR(l->get_font_color_override().a, 0.25f, 1e-4f);
}

// The ending's labels fade and wobble by CanvasItem.modulate,
// CanvasItem.self_modulate and Control.rotation (radians) value tracks.
namespace {
const char* kCanvasLib = R"({"format":2,"players":[{"path":"AP","root":".","autoplay":"","animations":[
  {"name":"c","length":1.0,"loop":0,"step":0,"tracks":[
    {"type":0,"path":"A:modulate","interp":1,"update":0,"loop_wrap":true,
     "keys":[{"t":0,"tr":1,"k":"c","v":[1,1,1,1]},{"t":1,"tr":1,"k":"c","v":[1,1,1,0]}]},
    {"type":0,"path":"B:self_modulate","interp":1,"update":0,"loop_wrap":true,
     "keys":[{"t":0,"tr":1,"k":"c","v":[1,1,1,1]},{"t":1,"tr":1,"k":"c","v":[1,1,1,0.5]}]},
    {"type":0,"path":"B:rotation","interp":1,"update":0,"loop_wrap":true,
     "keys":[{"t":0,"tr":1,"k":"f","v":0},{"t":1,"tr":1,"k":"f","v":0.2}]}]}]}]})";
}  // namespace

TEST(GodotAnimationPlayer, ValueTracksDriveCanvasModulateAndControlRotation) {
    godot::SceneTree tree;
    assets::AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromString(kCanvasLib));
    godot::Node* root = tree.add_node(std::make_unique<godot::Node>("root"), nullptr);
    auto* ap = godot::cast_to<godot::AnimationPlayer>(
        tree.add_node(std::make_unique<godot::AnimationPlayer>("AP"), root));
    auto* a = godot::cast_to<godot::Control>(tree.add_node(std::make_unique<godot::Control>("A"), root));
    auto* b = godot::cast_to<godot::Label>(tree.add_node(std::make_unique<godot::Label>("B"), root));
    ap->set_animations(&lib.Players()[0]);
    ap->play("c");
    for (int i = 0; i < 61; ++i) tree.process_frame(1.0 / 60.0);
    EXPECT_TRUE(ap->unresolved_paths().empty()) << ap->unresolved_paths().front();
    EXPECT_NEAR(a->get_modulate().a, 0.0f, 1e-4f);
    EXPECT_NEAR(b->get_self_modulate().a, 0.5f, 1e-4f);
    EXPECT_NEAR(b->get_modulate().a, 1.0f, 1e-4f) << "self_modulate is its own property";
    EXPECT_NEAR(b->get_rotation(), 0.2f, 1e-5f);
}

// animation_player.cpp:1643 queue / :1290 end_reached (4.1.4): a queued
// animation plays when the current one ends; with nothing playing, at once.
// HeavenManager.BeginLoop: play("camera pan down"), queue("camera idle").
namespace {
const char* kQueueLib = R"({"format":2,"players":[{"path":"AP","root":".","autoplay":"","animations":[
  {"name":"a","length":0.5,"loop":0,"step":0,"tracks":[]},
  {"name":"b","length":1.0,"loop":0,"step":0,"tracks":[]}]}]})";
}  // namespace

TEST(GodotAnimationPlayer, AQueuedAnimationPlaysWhenTheCurrentOneEnds) {
    godot::SceneTree tree;
    assets::AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromString(kQueueLib));
    godot::Node* root = tree.add_node(std::make_unique<godot::Node>("root"), nullptr);
    auto* ap = godot::cast_to<godot::AnimationPlayer>(
        tree.add_node(std::make_unique<godot::AnimationPlayer>("AP"), root));
    ap->set_animations(&lib.Players()[0]);
    ap->play("a");
    ap->queue("b");
    EXPECT_EQ(ap->get_current_animation(), "a");
    for (int i = 0; i < 32; ++i) tree.process_frame(1.0 / 60.0);
    EXPECT_EQ(ap->get_current_animation(), "b");
    EXPECT_TRUE(ap->is_playing());
    for (int i = 0; i < 62; ++i) tree.process_frame(1.0 / 60.0);
    EXPECT_FALSE(ap->is_playing()) << "the queue is empty: b ends";

    ap->queue("a");   // nothing playing: plays now
    EXPECT_EQ(ap->get_current_animation(), "a");
}

// animation_player.cpp:2045-2064 (4.1.4): stop() and pause() both run
// _stop_internal, which clears the queue.
TEST(GodotAnimationPlayer, StopAndPauseClearTheQueue) {
    godot::SceneTree tree;
    assets::AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromString(kQueueLib));
    godot::Node* root = tree.add_node(std::make_unique<godot::Node>("root"), nullptr);
    auto* ap = godot::cast_to<godot::AnimationPlayer>(
        tree.add_node(std::make_unique<godot::AnimationPlayer>("AP"), root));
    ap->set_animations(&lib.Players()[0]);
    ap->play("a");
    ap->queue("b");
    ap->stop();
    EXPECT_TRUE(ap->get_queue().empty()) << "stop";
    ap->play("a");
    ap->queue("b");
    ap->pause();
    EXPECT_TRUE(ap->get_queue().empty()) << "pause";
    ap->play("a");
    for (int i = 0; i < 40; ++i) tree.process_frame(1.0 / 60.0);
    EXPECT_FALSE(ap->is_playing()) << "b must not play after the cleared queue";
}

// An AnimationPlayer stops every audio player it played into when its
// animation ends, when another animation is played, and on stop()
// (_clear_audio_streams, animation_player.cpp:1287-1289, 1716-1719, 2045-2047):
// the speaker's sounds fade out, as the round blink's does at RESET.
namespace {

const char* kAudioLib = R"({"format":2,"players":[{"path":"AP","root":".","autoplay":"","animations":[
 {"name":"blink","length":0.5,"loop":0,"step":0.1,"tracks":[
  {"type":7,"path":"SPK","interp":1,"loop_wrap":true,
   "keys":[{"t":0,"tr":1,"stream":"res://audio/round blinker wave.ogg","start":0.0645,"end":0}]}]},
 {"name":"other","length":1.0,"loop":0,"step":0.1,"tracks":[]}]}]})";

struct AudioAnim : ::testing::Test {
    godot::SceneTree tree;
    assets::AnimationLibrary lib;
    godot::AnimationPlayer* ap = nullptr;
    std::vector<int> stopped;
    std::vector<godot::AudioPlayRequest> requests;

    void SetUp() override {
        ASSERT_TRUE(lib.LoadFromString(kAudioLib));
        godot::Node* root = tree.add_node(std::make_unique<godot::Node>("root"), nullptr);
        ap = godot::cast_to<godot::AnimationPlayer>(
            tree.add_node(std::make_unique<godot::AnimationPlayer>("AP"), root));
        tree.add_node(std::make_unique<godot::AudioStreamPlayer>("SPK"), root);
        ap->set_animations(&lib.Players()[0]);
        tree.set_audio_sink([this](const godot::AudioPlayback&, const godot::AudioPlayRequest& r) {
            requests.push_back(r);
            return godot::AudioPlayResult{2.0f, 7};
        });
        tree.set_audio_stop_sink([this](int voice) { stopped.push_back(voice); });
    }
};

TEST_F(AudioAnim, TheKeysOffsetsReachThePlay) {
    ap->play("blink");
    tree.process_frame(1.0 / 60.0);
    ASSERT_EQ(requests.size(), 1u);
    // Started: Godot adds how far into the key the first frame lands; its
    // p_time is the position after that frame's delta (_animation_process_data).
    EXPECT_NEAR(requests[0].from, 0.0645f + 1.0f / 60.0f, 1e-5f);
    EXPECT_TRUE(requests[0].ramp_in);
}

TEST_F(AudioAnim, ItsSoundsStopWhenTheAnimationEnds) {
    ap->play("blink");
    for (int i = 0; i < 40; ++i) tree.process_frame(1.0 / 60.0);   // past 0.5 s
    ASSERT_EQ(stopped.size(), 1u) << "the 2 s sound is cut at the 0.5 s animation's end";
    EXPECT_EQ(stopped[0], 7);
}

TEST_F(AudioAnim, ItsSoundsStopWhenAnotherAnimationPlays) {
    ap->play("blink");
    tree.process_frame(1.0 / 60.0);
    ap->play("other");
    EXPECT_EQ(stopped.size(), 1u);
}

TEST_F(AudioAnim, ItsSoundsStopOnStop) {
    ap->play("blink");
    tree.process_frame(1.0 / 60.0);
    ap->stop();
    EXPECT_EQ(stopped.size(), 1u);
}

}  // namespace
