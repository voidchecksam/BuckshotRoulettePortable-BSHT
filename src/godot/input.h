// Godot 4.1.4's Input singleton (core/input/input.cpp), reduced to what the
// scripts, the GUI and the platforms use:
//   - buffered events with motion accumulation (parse_input_event /
//     flush_buffered_events; use_accumulated_input defaults true, input.h:117);
//   - action state and just-pressed / just-released frames;
//   - touch -> mouse emulation (emulate_mouse_from_touch defaults true,
//     main.cpp:2426; emulate_touch_from_mouse defaults false, main.cpp:2418);
//   - mouse mode and custom cursor, recorded for the desktop platform to draw.
//
// Frame count: main.cpp:3467 increments Engine's process frames after the
// frame's process step, so a press flushed before a frame reads as just
// pressed for that whole frame. SceneTree::process_frame calls
// end_process_frame() as its last step.
#pragma once

#include "godot/input_event.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace godot {

class Input {
public:
    enum class MouseMode { Visible = 0, Hidden = 1 };

    static Input& get_singleton();

    // Clears every piece of state, the dispatch function included (tests).
    void reset();

    // Where parsed events go: SceneTree::push_input in the game.
    void set_dispatch(std::function<void(const InputEventRef&)> dispatch) { dispatch_ = std::move(dispatch); }

    void parse_input_event(const InputEventRef& event);
    void flush_buffered_events();

    void     end_process_frame() { ++process_frames_; }
    uint64_t get_process_frames() const { return process_frames_; }

    bool is_action_pressed(std::string_view action, bool exact = false) const;
    bool is_action_just_pressed(std::string_view action, bool exact = false) const;
    bool is_action_just_released(std::string_view action, bool exact = false) const;

    std::vector<int> get_connected_joypads() const { return joypads_; }
    void set_connected_joypads(std::vector<int> ids) { joypads_ = std::move(ids); }

    void      set_mouse_mode(MouseMode mode) { mouse_mode_ = mode; }
    MouseMode get_mouse_mode() const { return mouse_mode_; }
    // PORT: the image is the resource's file name ("cursor xp_hover.png"); the
    // desktop platform loads it from the extracted assets. `shape` is recorded
    // for completeness; every call in the scripts passes 0 (CURSOR_ARROW).
    void set_custom_mouse_cursor(const std::string& image, int shape, Vector2 hotspot);
    const std::string& custom_cursor_image() const { return cursor_image_; }
    Vector2            custom_cursor_hotspot() const { return cursor_hotspot_; }

    void set_emulate_mouse_from_touch(bool on) { emulate_mouse_from_touch_ = on; }

    // input.cpp get_last_mouse_velocity: the platform stamps each mouse motion
    // with it before parsing (display_server_windows.cpp); it ticks the track.
    Vector2 get_last_mouse_velocity();
    // The clock the track reads (OS::get_ticks_usec). The host sets the
    // tree's time so runs are repeatable; the default is the steady clock.
    void set_ticks_usec(std::function<uint64_t()> ticks) { ticks_usec_ = std::move(ticks); }
    Vector2 get_mouse_position() const { return mouse_pos_; }

private:
    struct ActionState {
        uint64_t pressed_process_frame = UINT64_MAX;
        uint64_t released_process_frame = UINT64_MAX;
        bool pressed = false;
        bool exact = true;
    };

    // input.cpp Input::VelocityTrack.
    struct VelocityTrack {
        uint64_t last_tick = 0;
        Vector2 velocity;
        Vector2 accum;
        float accum_t = 0.0f;
        float min_ref_frame = 0.1f;
        float max_ref_frame = 3.0f;
        void update(Vector2 delta_p, uint64_t tick);
        void reset(uint64_t tick);
    };

    uint64_t ticks_usec() const;
    void parse_input_event_impl(const InputEventRef& event, bool is_emulated);
    const ActionState* find_state(std::string_view action) const;

    std::function<void(const InputEventRef&)> dispatch_;
    std::deque<InputEventRef> buffered_;
    std::unordered_map<std::string, ActionState> action_state_;
    uint64_t process_frames_ = 0;
    int mouse_button_mask_ = 0;
    int mouse_from_touch_index_ = -1;
    Vector2 mouse_pos_;
    bool emulate_mouse_from_touch_ = true;
    std::vector<int> joypads_;
    MouseMode mouse_mode_ = MouseMode::Visible;
    std::string cursor_image_;
    Vector2 cursor_hotspot_;
    int cursor_shape_ = 0;
    std::function<uint64_t()> ticks_usec_;
    VelocityTrack mouse_velocity_track_;
};

}  // namespace godot
