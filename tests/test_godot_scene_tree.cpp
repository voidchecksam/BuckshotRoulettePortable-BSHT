// SceneTree order, pinned to Godot 4.1.4 scene_tree.cpp:486-524. parity_frame_order
// checks the same things against the real engine; these keep them from
// regressing without a Godot binary.
#include "godot/scene_tree.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Recorder : godot::Node {
    // A GDScript node that defines _process has processing on from _ready.
    Recorder(const char* n, std::vector<std::string>* log) : godot::Node(n), log_(log) {
        set_process(true);
    }
    void _ready() override { log_->push_back("ready:" + get_name()); }
    void _process(double) override { log_->push_back("process:" + get_name()); }
    std::vector<std::string>* log_;
};

seq::Task WaitThenLog(double seconds, std::vector<std::string>* log, std::string tag) {
    co_await godot::Timer(seconds);
    log->push_back("timer:" + tag);
}

}  // namespace

TEST(GodotSceneTree, PathsAreTreeRelativeAndResolve) {
    godot::SceneTree t;
    godot::Node* root = t.add_node(std::make_unique<godot::Node>("main"), nullptr);
    godot::Node* a = t.add_node(std::make_unique<godot::Node3D>("tabletop parent"), root);
    t.add_node(std::make_unique<godot::Node3D>("shotgun main parent"), a);
    EXPECT_EQ(root->get_path(), ".");
    EXPECT_EQ(t.get_node("tabletop parent/shotgun main parent")->get_path(),
              "tabletop parent/shotgun main parent");
    EXPECT_EQ(t.get_node("."), root);
    EXPECT_EQ(t.get_node("missing"), nullptr);
    EXPECT_NE(t.get_node_as<godot::Node3D>("tabletop parent"), nullptr);
    EXPECT_EQ(a->get_child(0)->get_parent(), a);
}

TEST(GodotSceneTree, ReadyIsChildrenFirstAndProcessIsTreeOrder) {
    std::vector<std::string> log;
    godot::SceneTree t;
    godot::Node* a = t.add_node(std::make_unique<Recorder>("A", &log), nullptr);
    godot::Node* b = t.add_node(std::make_unique<Recorder>("B", &log), a);
    t.add_node(std::make_unique<Recorder>("D", &log), b);
    t.add_node(std::make_unique<Recorder>("C", &log), a);
    t.ready();
    t.process_frame(1.0 / 60.0);
    const std::vector<std::string> expected = {
        "ready:D", "ready:B", "ready:C", "ready:A",
        "process:A", "process:B", "process:D", "process:C"};
    EXPECT_EQ(log, expected);
}

TEST(GodotSceneTree, TimersRunAfterProcessAndCountTheirFirstFrame) {
    std::vector<std::string> log;
    godot::SceneTree t;
    t.add_node(std::make_unique<Recorder>("A", &log), nullptr);
    t.spawn(WaitThenLog(0.04, &log, "t40"));
    for (int f = 1; f <= 3; ++f) {
        log.push_back("frame:" + std::to_string(f));
        t.process_frame(1.0 / 60.0);
    }
    // 0.04 - 3 * (1/60) < 0: fires in frame 3, after that frame's _process.
    ASSERT_GE(log.size(), 2u);
    EXPECT_EQ(log.back(), "timer:t40");
    EXPECT_EQ(log[log.size() - 2], "process:A");
    EXPECT_EQ(std::count(log.begin(), log.end(), "timer:t40"), 1);
}

TEST(GodotSceneTree, TimeScaleChangedMidFrameAppliesNextFrame) {
    std::vector<std::string> log;
    godot::SceneTree t;
    t.spawn(WaitThenLog(0.02, &log, "t20"));
    t.set_time_scale(0.5);
    t.process_frame(1.0 / 60.0);   // delta 1/120 -> 0.01167 left
    EXPECT_TRUE(log.empty());
    t.process_frame(1.0 / 60.0);   // 0.00333 left
    EXPECT_TRUE(log.empty());
    t.process_frame(1.0 / 60.0);   // fires
    EXPECT_EQ(log.size(), 1u);
}

// ---------------------------------------------------------------------------
// push_input (viewport.cpp push_input:2957 and _push_unhandled_input_internal).
#include "godot/input_event.h"
#include "godot/script.h"

namespace {

struct InputRecorder : godot::Script {
    InputRecorder(std::string tag, std::vector<std::string>* log, bool handles)
        : tag_(std::move(tag)), log_(log), handles_(handles) {}
    const char* get_class_name() const override { return "InputRecorder"; }
    bool has_input() const override { return true; }
    void _input(const godot::InputEventRef&) override {
        log_->push_back("input:" + tag_);
        if (handles_) get_tree()->set_input_as_handled();
    }
    bool has_unhandled_input() const override { return true; }
    void _unhandled_input(const godot::InputEventRef&) override { log_->push_back("unhandled:" + tag_); }
    std::string tag_;
    std::vector<std::string>* log_;
    bool handles_;
};

}  // namespace

// scene_tree.cpp _call_input_pause: _input runs from the LAST node in tree order
// to the first and stops once handled; _unhandled_input follows only if nobody
// handled the event, in the same reverse order.
TEST(GodotSceneTree, InputRunsReverseTreeOrderThenUnhandled) {
    godot::SceneTree t;
    std::vector<std::string> log;
    godot::Node* root = t.add_node(std::make_unique<godot::Node>("main"), nullptr);
    godot::Node* a = t.add_node(std::make_unique<godot::Node>("a"), root);
    godot::Node* b = t.add_node(std::make_unique<godot::Node>("b"), root);
    a->set_script(std::make_unique<InputRecorder>("a", &log, false));
    b->set_script(std::make_unique<InputRecorder>("b", &log, false));
    t.ready_scripts();
    t.push_input(std::make_shared<godot::InputEventKey>());
    EXPECT_EQ(log, (std::vector<std::string>{"input:b", "input:a", "unhandled:b", "unhandled:a"}));

    log.clear();
    b->set_script(std::make_unique<InputRecorder>("b", &log, true));
    t.ready_scripts();
    t.push_input(std::make_shared<godot::InputEventKey>());
    EXPECT_EQ(log, (std::vector<std::string>{"input:b"}));
}

// scene_tree.cpp SceneTree::physics_process (4.1.4-stable) ends with
// _flush_delete_queue(), and Main::iteration runs physics before process. So a
// node freed by a timer at the end of frame N is gone before frame N+1's
// process, and a sample taken there (MainLoop::process) no longer sees it
// (use_magnifier_b2r1).
TEST(GodotSceneTree, ThePhysicsStepFreesWhatWasQueuedLastFrame) {
    godot::SceneTree t;
    godot::Node* parent = t.add_node(std::make_unique<godot::Node>("parent"), t.get_root());
    godot::Node* doomed = t.add_node(std::make_unique<godot::Node>("doomed"), parent);
    t.process_frame(1.0 / 60.0);
    doomed->queue_free();
    t.physics_frame();
    EXPECT_EQ(parent->get_child_count(), 0);
}

// process_frame still runs the physics step itself when the caller did not.
TEST(GodotSceneTree, ProcessFrameRunsThePhysicsStepWhenNotRunAlready) {
    godot::SceneTree t;
    godot::Node* parent = t.add_node(std::make_unique<godot::Node>("parent"), t.get_root());
    godot::Node* doomed = t.add_node(std::make_unique<godot::Node>("doomed"), parent);
    doomed->queue_free();
    t.process_frame(1.0 / 60.0);
    EXPECT_EQ(parent->get_child_count(), 0);
}

// Anything that cached a node pointer (the renderers, an AnimationPlayer's
// bound tracks) must learn that nodes were freed: Godot tells them through
// tree_exiting (animation_player.cpp _node_removed -> clear_caches). The port
// counts frees instead, and a cache re-resolves by path when the count moves.
TEST(GodotSceneTree, FreeingNodesMovesTheFreeGenerationAndDropsTheirPaths) {
    godot::SceneTree t;
    godot::Node* parent = t.add_node(std::make_unique<godot::Node>("parent"), t.get_root());
    godot::Node* doomed = t.add_node(std::make_unique<godot::Node>("doomed"), parent);
    const std::string path = doomed->get_path();
    const auto before = t.free_generation();
    t.process_frame(1.0 / 60.0);
    EXPECT_EQ(t.free_generation(), before) << "a frame that frees nothing leaves it";
    doomed->queue_free();
    t.process_frame(1.0 / 60.0);
    EXPECT_NE(t.free_generation(), before);
    EXPECT_EQ(t.get_node(path), nullptr);
}

// get_tree().change_scene_to_file is deferred in Godot: the current frame
// finishes, then the scene is swapped. The port records the request; the app
// services it between frames and rebuilds the tree.
TEST(GodotSceneTree, ChangeSceneIsARequestTakenOnce) {
    godot::SceneTree t;
    EXPECT_TRUE(t.take_scene_change().empty());
    t.change_scene_to_file("res://scenes/death.tscn");
    EXPECT_EQ(t.take_scene_change(), "res://scenes/death.tscn");
    EXPECT_TRUE(t.take_scene_change().empty());
}

// viewport.cpp _push_unhandled_input_internal (4.1.4): after _unhandled_input,
// and only if nobody handled it, an InputEventKey goes to the
// _unhandled_key_input group in the same reverse tree order. Any other event
// never reaches it (EndingManager's "press any key" listens there).
namespace {
struct KeyRecorder : godot::Script {
    explicit KeyRecorder(std::vector<std::string>* log) : log_(log) {}
    const char* get_class_name() const override { return "KeyRecorder"; }
    bool has_unhandled_key_input() const override { return true; }
    void _unhandled_key_input(const godot::InputEventRef&) override { log_->push_back("key"); }
    std::vector<std::string>* log_;
};
}  // namespace

TEST(GodotSceneTree, UnhandledKeyInputSeesKeysOnly) {
    godot::SceneTree t;
    std::vector<std::string> log;
    godot::Node* root = t.add_node(std::make_unique<godot::Node>("main"), nullptr);
    godot::Node* a = t.add_node(std::make_unique<godot::Node>("a"), root);
    godot::Node* b = t.add_node(std::make_unique<godot::Node>("b"), root);
    a->set_script(std::make_unique<KeyRecorder>(&log));
    b->set_script(std::make_unique<InputRecorder>("b", &log, false));
    t.ready_scripts();
    t.push_input(std::make_shared<godot::InputEventKey>());
    EXPECT_EQ(log, (std::vector<std::string>{"input:b", "unhandled:b", "key"}));

    log.clear();
    t.push_input(std::make_shared<godot::InputEventJoypadButton>());
    EXPECT_EQ(log, (std::vector<std::string>{"input:b", "unhandled:b"}));

    log.clear();
    b->set_script(std::make_unique<InputRecorder>("b", &log, true));
    t.ready_scripts();
    t.push_input(std::make_shared<godot::InputEventKey>());
    EXPECT_EQ(log, (std::vector<std::string>{"input:b"})) << "handled in _input: no key pass";
}
