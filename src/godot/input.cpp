#include "godot/input.h"

#include "godot/input_map.h"

#include <chrono>
#include <cstdio>
#include <memory>

namespace godot {

Input& Input::get_singleton() {
    static Input input;
    return input;
}

void Input::reset() {
    dispatch_ = nullptr;
    buffered_.clear();
    action_state_.clear();
    process_frames_ = 0;
    mouse_button_mask_ = 0;
    mouse_from_touch_index_ = -1;
    mouse_pos_ = {};
    emulate_mouse_from_touch_ = true;
    joypads_.clear();
    mouse_mode_ = MouseMode::Visible;
    cursor_image_.clear();
    cursor_hotspot_ = {};
    cursor_shape_ = 0;
    ticks_usec_ = nullptr;
    mouse_velocity_track_.reset(ticks_usec());
}

uint64_t Input::ticks_usec() const {
    if (ticks_usec_) return ticks_usec_();
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

void Input::VelocityTrack::update(Vector2 delta_p, uint64_t tick) {
    const uint32_t tdiff = static_cast<uint32_t>(tick - last_tick);
    const float delta_t = static_cast<float>(tdiff / 1000000.0);
    last_tick = tick;
    if (delta_t > max_ref_frame) {
        // First movement in a long time, reset and start again.
        velocity = Vector2();
        accum = delta_p;
        accum_t = 0;
        return;
    }
    accum = {accum.x + delta_p.x, accum.y + delta_p.y};
    accum_t += delta_t;
    if (accum_t < min_ref_frame) return;   // not enough time for a precise speed
    velocity = {accum.x / accum_t, accum.y / accum_t};
    accum = Vector2();
    accum_t = 0;
}

void Input::VelocityTrack::reset(uint64_t tick) {
    last_tick = tick;
    velocity = Vector2();
    accum = Vector2();
    accum_t = 0;
}

Vector2 Input::get_last_mouse_velocity() {
    mouse_velocity_track_.update(Vector2(), ticks_usec());
    return mouse_velocity_track_.velocity;
}

void Input::set_custom_mouse_cursor(const std::string& image, int shape, Vector2 hotspot) {
    cursor_image_ = image;
    cursor_shape_ = shape;
    cursor_hotspot_ = hotspot;
}

// input.cpp Input::parse_input_event: with use_accumulated_input an event first
// tries to merge into the last buffered one.
void Input::parse_input_event(const InputEventRef& event) {
    if (!event) return;
    if (buffered_.empty() || !buffered_.back()->accumulate(*event)) buffered_.push_back(event);
}

// input.cpp Input::flush_buffered_events
void Input::flush_buffered_events() {
    while (!buffered_.empty()) {
        InputEventRef e = std::move(buffered_.front());
        buffered_.pop_front();
        parse_input_event_impl(e, false);
    }
}

// input.cpp Input::_parse_input_event_impl, in source order.
// PORT: keys_pressed, joy_buttons_pressed, joypad axes and gestures are not
// kept: no translated script reads Input.is_key_pressed, is_joy_button_pressed
// or gestures. The mouse velocity is kept (VelocityTrack). emulate_touch_from_mouse
// is false in this project (main.cpp:2418 default, not overridden), so its two
// branches are not transcribed.
void Input::parse_input_event_impl(const InputEventRef& event, bool /*is_emulated*/) {
    if (const auto* mb = event_cast<InputEventMouseButton>(event.get())) {
        const int mask = mouse::MASK_LEFT << (mb->button_index - mouse::LEFT);
        if (mb->is_pressed()) {
            mouse_button_mask_ |= mask;
        } else {
            mouse_button_mask_ &= ~mask;
        }
        mouse_pos_ = mb->global_position;
    }

    if (const auto* mm = event_cast<InputEventMouseMotion>(event.get())) {
        mouse_pos_ = mm->global_position;
        mouse_velocity_track_.update(mm->relative, ticks_usec());
    }

    if (const auto* st = event_cast<InputEventScreenTouch>(event.get())) {
        if (emulate_mouse_from_touch_) {
            bool translate = false;
            if (st->is_pressed()) {
                if (mouse_from_touch_index_ == -1) {
                    translate = true;
                    mouse_from_touch_index_ = st->index;
                }
            } else if (st->index == mouse_from_touch_index_) {
                translate = true;
                mouse_from_touch_index_ = -1;
            }
            if (translate) {
                auto button = std::make_shared<InputEventMouseButton>();
                button->device = DEVICE_ID_EMULATION;
                button->position = st->position;
                button->global_position = st->position;
                button->pressed = st->is_pressed();
                button->canceled = st->is_canceled();
                button->button_index = mouse::LEFT;
                button->double_click = st->double_tap;
                button->button_mask = st->is_pressed() ? (mouse_button_mask_ | mouse::MASK_LEFT)
                                                       : (mouse_button_mask_ & ~mouse::MASK_LEFT);
                parse_input_event_impl(button, true);
            }
        }
    }

    if (const auto* sd = event_cast<InputEventScreenDrag>(event.get())) {
        if (emulate_mouse_from_touch_ && sd->index == mouse_from_touch_index_) {
            auto motion = std::make_shared<InputEventMouseMotion>();
            motion->device = DEVICE_ID_EMULATION;
            motion->position = sd->position;
            motion->global_position = sd->position;
            motion->relative = sd->relative;
            motion->velocity = sd->velocity;
            motion->button_mask = mouse_button_mask_;
            parse_input_event_impl(motion, true);
        }
    }

    // input.cpp:695-712: action state follows every action the event is part of.
    const InputMap& map = InputMap::get_singleton();
    for (const std::string& name : map.get_actions()) {
        if (!map.event_get_action_status(*event, name, false, nullptr)) continue;
        ActionState& action = action_state_[name];
        const bool now = event->is_action_pressed(name);
        if (!event->is_echo() && is_action_pressed(name, false) != now) {
            if (now) {
                action.pressed = true;
                action.pressed_process_frame = process_frames_;
            } else {
                action.pressed = false;
                action.released_process_frame = process_frames_;
            }
            action.exact = map.event_get_action_status(*event, name, true, nullptr);
        }
    }

    if (dispatch_) dispatch_(event);
}

const Input::ActionState* Input::find_state(std::string_view action) const {
    if (!InputMap::get_singleton().has_action(action)) {
        std::fprintf(stderr, "Input: the action \"%.*s\" does not exist\n", static_cast<int>(action.size()),
                     action.data());
        return nullptr;
    }
    const auto it = action_state_.find(std::string(action));
    return it == action_state_.end() ? nullptr : &it->second;
}

// input.cpp Input::is_action_pressed
bool Input::is_action_pressed(std::string_view action, bool exact) const {
    const ActionState* s = find_state(action);
    return s && s->pressed && (!exact || s->exact);
}

// input.cpp:292 Input::is_action_just_pressed. PORT: the physics-frame branch is
// not transcribed; no script calls it from _physics_process.
bool Input::is_action_just_pressed(std::string_view action, bool exact) const {
    const ActionState* s = find_state(action);
    if (!s) return false;
    if (exact && !s->exact) return false;
    return s->pressed_process_frame == process_frames_;
}

// input.cpp Input::is_action_just_released
bool Input::is_action_just_released(std::string_view action, bool exact) const {
    const ActionState* s = find_state(action);
    if (!s) return false;
    if (exact && !s->exact) return false;
    return s->released_process_frame == process_frames_;
}

}  // namespace godot
