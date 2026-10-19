// Godot 4.1's SceneTree, reduced to what the scripts use: ownership, path
// lookup, _ready, _process, deferred calls and create_timer, in Godot's frame
// order.
//
// Frame order (scene_tree.cpp:486-524 and 893-952 at 4.1.4-stable, pinned by
// parity_frame_order and parity_anim_play_semantics):
//   1. the frame's delta is raw * time_scale, sampled before any callback runs
//   2. deferred calls queued since the last frame run (MessageQueue::flush)
//   3. every node that was processing when the frame began, in tree order:
//      _internal_process then _process. One that stops processing during the
//      pass is skipped for the rest of the frame; one that starts waits.
//   4. deferred calls queued during step 3 run (METHOD track keys land here)
//   5. timers: time_left -= delta; fire at <= 0; a timer created in step 3 is
//      counted this frame, one created while timers fire waits for the next
#pragma once

#include "core/profiler.h"

#include "godot/rng.h"

#include "assets/animation.h"
#include "godot/node.h"
#include "godot/physics.h"
#include "seq/delay.h"
#include "seq/scheduler.h"
#include "seq/task.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace godot {

class Control;

// `await get_tree().create_timer(t, false).timeout`
inline seq::Delay Timer(double seconds) { return seq::Delay(seconds); }

// What animation and audio did, for the debug census and the tests. A missing
// animation or an unhandled METHOD key is counted and named, never silent.
struct AnimationStats {
    int plays = 0;
    int missing = 0;
    int methods_called = 0;
    int methods_unhandled = 0;
    int audio_plays = 0;
    std::vector<std::string> unhandled_methods;
};

// Plays `player`'s stream on the platform mixer.
using AudioSink = std::function<AudioPlayResult(const AudioPlayback& player, const AudioPlayRequest& request)>;
// Stops a voice the sink started, with Godot's one-buffer fade.
using AudioStopSink = std::function<void(int voice)>;
// A playing voice's player changed its mix gain or pitch.
using AudioUpdateSink = std::function<void(int voice, StereoGain gain, float pitch)>;

class SceneTree {
public:
    // scene_tree.cpp:1696: the constructor seeds the global RNG from the clock,
    // so two runs never deal the same game. A caller that wants determinism
    // (the parity harness, the tests) calls godot::seed afterwards. PORT: the
    // port builds a new tree on a scene change where Godot keeps one, so a
    // death reload reseeds too, which is invisible since the seed is random.
    SceneTree() { randomize(); }
    SceneTree(const SceneTree&) = delete;
    SceneTree& operator=(const SceneTree&) = delete;

    // Adds `node` under `parent` (nullptr: it becomes the root, path "."). Nodes
    // must arrive parent-first; tree order is insertion order, which is the
    // exporter's depth-first pre-order.
    Node* add_node(std::unique_ptr<Node> node, Node* parent);

    Node* get_root() const { return root_; }

    // SceneTree.change_scene_to_file. Godot defers the swap to the end of the
    // frame; the port records the request and the app, which owns the tree,
    // takes it between frames and rebuilds (app::GameApp::ChangeScene).
    void change_scene_to_file(std::string path) { pending_scene_ = std::move(path); }
    // The pending request, cleared by taking it; empty when there is none.
    std::string take_scene_change() {
        std::string p = std::move(pending_scene_);
        pending_scene_.clear();
        return p;
    }
    // SceneTree.quit, recorded like a scene change and performed by the host
    // after the frame (the menu's Exit, gd: MenuManager L168).
    void quit() { quit_requested_ = true; }
    bool take_quit() {
        const bool q = quit_requested_;
        quit_requested_ = false;
        return q;
    }
    Node* get_node(std::string_view path) const;
    template <class T>
    T* get_node_as(std::string_view path) const { return cast_to<T>(get_node(path)); }

    // The first Camera3D marked current, or nullptr.
    Camera3D* get_current_camera() const;
    // The frame profiler (core/profiler.h) the node pass, coroutines,
    // deferred calls and particle step time themselves into. Null: off.
    void set_profiler(core::Profiler* p) { profiler_ = p; }
    core::Profiler* profiler() const { return profiler_; }

    // Viewport GUI (gui.cpp, viewport.cpp at 4.1.4-stable).
    // _gui_control_grab_focus: the old focus owner gets focus_exited, then
    // gui_focus_changed fires, then focus_entered on the new one.
    Control* gui_get_focus_owner() const { return focus_owner_; }
    void     gui_grab_focus(Control* control);
    void     gui_release_focus();
    // viewport.cpp:2381 _gui_hide_control: a control that left the screen loses
    // key focus, mouse focus and hover. Control::set_visible calls it.
    void     gui_hide_control(Control* control);
    void     connect_gui_focus_changed(std::function<void(Control*)> callback) {
        focus_changed_.push_back(std::move(callback));
    }
    // Viewport::gui_find_control: the topmost Control under a viewport point.
    Control* gui_find_control(Vector2 global) const;
    // A Control's NodePath relative to it ("../m button_choice you"), or nullptr.
    Node* get_node_relative(const Node* from, std::string_view relative) const;

    // The physics server's view of the scene: every enabled CollisionShape3D
    // under a CollisionObject3D, with its global transform and its body's layer
    // as they are NOW. Godot syncs body state into the server on the physics
    // step (main.cpp:3386), which runs before process; process_frame calls this
    // first, and a query between syncs sees the last one.
    void physics_sync();
    const PhysicsDirectSpaceState3D& physics_space() const { return space_; }

    std::size_t get_node_count() const { return order_.size(); }
    const std::vector<Node*>& nodes_in_tree_order() const { return order_; }

    // Node::add_child's tree half. A name a sibling already has becomes
    // "@<Class>@<n>" (node.cpp:1253); the subtree joins tree order after the
    // parent's last descendant and readies children first. Called during the
    // process pass, the order insert waits until the pass ends.
    Node* adopt(Node* parent, std::unique_ptr<Node> child);

    // Node::remove_child's tree half: unlink the subtree and drop its paths,
    // keeping ownership. Leaving tree order waits for the end of the process
    // pass, as adopt's insert does, so the pass never sees shifted indices.
    void detach(Node* child);
    void remove_from_order(const std::vector<Node*>& subtree);
    // add_child(Node*)'s tree half: link an owned, detached node under a new
    // parent and give it and its subtree their new paths.
    Node* reattach(Node* parent, Node* child);

    // Node::queue_free's tree half; see process_frame for when it lands.
    void queue_delete(Node* n) { delete_queue_.push_back(n); }

    // Every MeshInstance3D in the tree cloned from a PackedScene, for the
    // renderer's template props. Maintained on adopt and delete.
    const std::vector<MeshInstance3D*>& template_instances() const { return template_instances_; }

    // _ready on every node, children before their parent; each node's script
    // readies right after its native _ready.
    void ready();

    // Script _ready only, children first, for every script not yet readied.
    // The runtime export is captured after Godot ran every _ready, so native
    // nodes must not ready twice; scripts attached after loading still must.
    void ready_scripts();

    // One Godot frame. `raw_delta` is unscaled. Ends with
    // Input::end_process_frame(), as main.cpp:3467 counts the frame after process.
    // The physics step of a frame (Godot runs it before process): the physics
    // space, then the deferred calls and the delete queue. process_frame runs
    // it unless the caller already has; the parity harness does, so its
    // samples land where Godot's MainLoop::process sees the tree.
    void physics_frame();
    void process_frame(double raw_delta);

    // Viewport::push_input (viewport.cpp:2957): scripts' _input in reverse tree
    // order until handled, then the GUI, then _unhandled_input in reverse tree
    // order until handled.
    // Then _unhandled_key_input for an unhandled InputEventKey (same order).
    // PORT: the _shortcut_input group is not modelled: no translated script
    // defines it (viewport.cpp _push_unhandled_input_internal runs it first).
    void push_input(const InputEventRef& event);
    void set_input_as_handled() { input_handled_ = true; }
    bool is_input_handled() const { return input_handled_; }

    // get_process_delta_time(): the scaled delta of the frame being processed.
    double get_process_delta_time() const { return process_delta_; }
    // The frame's delta before time_scale: the audio clock (playbacks end in
    // real time whatever the game's speed).
    double get_raw_delta_time() const { return raw_delta_; }
    // Every raw delta so far: the clock Input's velocity track reads in the game.
    double get_raw_time() const { return raw_time_; }

    // `await get_tree().create_timer(t, false).timeout`: scaled, paused.
    seq::Delay create_timer(double seconds) const { return Timer(seconds); }

    // The renderer's one observable side effect: particle systems drawing the
    // global RNG (godot/particles.h). Off = headless Godot's dummy renderer,
    // which the headless references were made with.
    void set_particle_rng(bool on) { particle_rng_ = on; }
    bool get_particle_rng() const { return particle_rng_; }

    double   get_time_scale() const { return time_scale_; }
    void     set_time_scale(double s) { time_scale_ = s < 0.0 ? 0.0 : s; }
    uint64_t get_frame() const { return frame_; }

    // A GDScript call to a coroutine WITHOUT await: runs to its first suspension now.
    // `owner` is the node whose script method this is: freeing the node drops
    // the sequence, as Godot drops a function state with its instance.
    void spawn(seq::Task&& task, const Node* owner = nullptr);

    // `node.call_deferred(method, args)`: runs at the next flush (see frame order).
    void call_deferred(Node* node, const std::string& method, const std::vector<assets::Value>& args);

    void set_audio_sink(AudioSink sink) { audio_sink_ = std::move(sink); }
    const AudioSink& audio_sink() const { return audio_sink_; }
    void set_audio_stop_sink(AudioStopSink sink) { audio_stop_sink_ = std::move(sink); }
    void set_audio_update_sink(AudioUpdateSink sink) { audio_update_sink_ = std::move(sink); }
    const AudioUpdateSink& audio_update_sink() const { return audio_update_sink_; }
    const AudioStopSink& audio_stop_sink() const { return audio_stop_sink_; }

    AnimationStats& animation_stats() { return stats_; }
    const AnimationStats& animation_stats() const { return stats_; }

    seq::Scheduler& scheduler() { return scheduler_; }

    // Moves each time the delete queue frees nodes. A cache of node pointers
    // (the renderers, AnimationPlayer's bound tracks) re-resolves by path when
    // it changes, where Godot's caches hear tree_exiting instead.
    uint64_t free_generation() const { return free_generation_; }

private:
    bool quit_requested_ = false;
    std::string pending_scene_;
    friend class Node;

    struct DeferredCall {
        Node* node = nullptr;
        std::string method;
        std::vector<assets::Value> args;
    };

    void ready_recursive(Node* n);
    void ready_scripts_recursive(Node* n);
    void insert_in_order(Node* parent, const std::vector<Node*>& subtree);
    // _flush_delete_queue (scene_tree.cpp:516).
    void flush_delete_queue();
    // NOTIFICATION_READY's script half: process first, then _ready, once.
    static void ready_script(Node* n);
    void flush_deferred();
    // Node processing stopped: during a pass, skip it for the rest of this frame.
    void note_process_removed(Node& n) {
        if (processing_) n.removed_in_frame_ = frame_;
    }

    std::vector<std::unique_ptr<Node>> owned_;
    std::vector<Node*> order_;
    std::unordered_map<std::string, Node*> by_path_;
    Node* root_ = nullptr;
    Control* focus_owner_ = nullptr;
    bool input_handled_ = false;
    PhysicsDirectSpaceState3D space_;
    // Viewport gui state: the control a mouse press went to, and the buttons held.
    Control* mouse_focus_ = nullptr;
    Control* mouse_over_ = nullptr;
    int mouse_focus_mask_ = 0;
    bool key_event_accepted_ = false;
    std::vector<std::function<void(Control*)>> focus_changed_;
    void gui_input_event(const InputEventRef& event);
    seq::Scheduler scheduler_;
    double time_scale_ = 1.0;
    bool particle_rng_ = true;
    // RenderingServerDefault::_draw's particle half, run after the frame's
    // scripts and timers (main.cpp:3442 draws with the scaled step).
    void render_step(double delta);
    double process_delta_ = 0.0;
    double raw_delta_ = 0.0;
    double raw_time_ = 0.0;
    mutable Camera3D* camera_cache_ = nullptr;   // get_current_camera, for one frame
    mutable uint64_t camera_frame_ = ~0ull;
    mutable uint64_t camera_generation_ = ~0ull;
    uint64_t frame_ = 0;
    core::Profiler* profiler_ = nullptr;
    bool processing_ = false;
    std::vector<uint8_t> in_list_;           // reused: who was processing at frame start
    std::vector<DeferredCall> deferred_;
    std::vector<Node*> delete_queue_;
    uint64_t free_generation_ = 0;
    std::vector<MeshInstance3D*> template_instances_;
    // Subtrees adopted during the process pass: (parent, pre-order nodes).
    std::vector<std::pair<Node*, std::vector<Node*>>> pending_order_;
    std::vector<Node*> pending_detach_;
    bool physics_ran_ = false;   // physics_frame ran for the coming process_frame   // left order_ during a process pass
    uint32_t generated_names_ = 1;   // node_hrcr_count
    AudioSink audio_sink_;
    AudioStopSink audio_stop_sink_;
    AudioUpdateSink audio_update_sink_;
    AnimationStats stats_;
};

}  // namespace godot
