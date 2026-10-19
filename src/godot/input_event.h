// Godot 4.1.4's InputEvent hierarchy, reduced to the fields the scripts, the
// GUI and the touch -> mouse emulation read. Matching is
// core/input/input_event.cpp; pressed state is InputMap::event_get_action_status
// (core/input/input_map.cpp).
//
// PORT: no modifier keys, windows or pen/pressure fields. No bound action in
// project.godot uses a modifier, and the port has one window.
#pragma once

#include "godot/value_types.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace godot {

namespace key {                                              // keyboard.h:45-64
inline constexpr int64_t SPECIAL   = int64_t{1} << 22;
inline constexpr int64_t ESCAPE    = SPECIAL | 0x01;
inline constexpr int64_t BACKSPACE = SPECIAL | 0x04;
inline constexpr int64_t ENTER     = SPECIAL | 0x05;
inline constexpr int64_t KP_ENTER  = SPECIAL | 0x06;
inline constexpr int64_t LEFT      = SPECIAL | 0x0F;
inline constexpr int64_t UP        = SPECIAL | 0x10;
inline constexpr int64_t RIGHT     = SPECIAL | 0x11;
inline constexpr int64_t DOWN      = SPECIAL | 0x12;
}  // namespace key

namespace joy {                                              // input_enums.h:64-78
inline constexpr int A = 0, B = 1, X = 2, Y = 3, BACK = 4, GUIDE = 5, START = 6;
inline constexpr int DPAD_UP = 11, DPAD_DOWN = 12, DPAD_LEFT = 13, DPAD_RIGHT = 14;
}  // namespace joy

namespace mouse {                                            // input_enums.h
inline constexpr int LEFT = 1, RIGHT = 2, MIDDLE = 3;
inline constexpr int MASK_LEFT = 1 << (LEFT - 1);
}  // namespace mouse

inline constexpr int DEVICE_ID_EMULATION = -1;               // input_event.h

enum class InputEventType { Key, JoypadButton, MouseButton, MouseMotion, ScreenTouch, ScreenDrag };

struct InputEvent {
    explicit InputEvent(InputEventType t) : type(t) {}
    virtual ~InputEvent() = default;
    InputEvent(const InputEvent&) = default;
    InputEvent& operator=(const InputEvent&) = default;

    InputEventType type;
    int  device = 0;
    bool pressed = false;
    bool echo = false;
    bool canceled = false;

    // input_event.cpp InputEvent::is_pressed
    bool is_pressed() const { return pressed && !canceled; }
    bool is_echo() const { return echo; }
    bool is_canceled() const { return canceled; }

    bool is_action(std::string_view action, bool exact = false) const;
    bool is_action_pressed(std::string_view action, bool allow_echo = false, bool exact = false) const;
    bool is_action_released(std::string_view action, bool exact = false) const;

    // True when `event` is the input this map entry describes; *r_pressed gets
    // the event's pressed state.
    virtual bool action_match(const InputEvent& event, bool exact, bool* r_pressed) const;
    // InputEventMouseMotion::accumulate; every other type refuses.
    virtual bool accumulate(const InputEvent&) { return false; }
    // InputEvent::as_text (input_event.cpp): what the rebind screen shows.
    virtual std::string as_text() const { return std::string(); }
};

using InputEventRef = std::shared_ptr<InputEvent>;

struct InputEventKey : InputEvent {
    InputEventKey() : InputEvent(InputEventType::Key) {}
    int64_t keycode = 0;
    int64_t physical_keycode = 0;
    bool action_match(const InputEvent& event, bool exact, bool* r_pressed) const override;
    std::string as_text() const override;   // input_event.cpp:439
};

struct InputEventJoypadButton : InputEvent {
    InputEventJoypadButton() : InputEvent(InputEventType::JoypadButton) {}
    int button_index = 0;
    bool action_match(const InputEvent& event, bool exact, bool* r_pressed) const override;
    std::string as_text() const override;   // input_event.cpp:1212
};

struct InputEventMouse : InputEvent {
    using InputEvent::InputEvent;
    Vector2 position;
    Vector2 global_position;
    int button_mask = 0;
};

struct InputEventMouseButton : InputEventMouse {
    InputEventMouseButton() : InputEventMouse(InputEventType::MouseButton) {}
    int  button_index = 0;
    bool double_click = false;
    bool action_match(const InputEvent& event, bool exact, bool* r_pressed) const override;
    std::string as_text() const override;   // input_event.cpp:773
};

struct InputEventMouseMotion : InputEventMouse {
    InputEventMouseMotion() : InputEventMouse(InputEventType::MouseMotion) {}
    Vector2 relative;
    Vector2 velocity;
    bool accumulate(const InputEvent& event) override;
};

struct InputEventScreenTouch : InputEvent {
    InputEventScreenTouch() : InputEvent(InputEventType::ScreenTouch) {}
    int index = 0;
    Vector2 position;
    bool double_tap = false;
};

struct InputEventScreenDrag : InputEvent {
    InputEventScreenDrag() : InputEvent(InputEventType::ScreenDrag) {}
    int index = 0;
    Vector2 position;
    Vector2 relative;
    Vector2 velocity;
};

// `event is T`. InputEventMouse matches both mouse kinds.
template <class T> const T* event_cast(const InputEvent* e);
template <class T> T* event_cast(InputEvent* e) {
    return const_cast<T*>(event_cast<T>(static_cast<const InputEvent*>(e)));
}

template <> inline const InputEventKey* event_cast(const InputEvent* e) {
    return e && e->type == InputEventType::Key ? static_cast<const InputEventKey*>(e) : nullptr;
}
template <> inline const InputEventJoypadButton* event_cast(const InputEvent* e) {
    return e && e->type == InputEventType::JoypadButton ? static_cast<const InputEventJoypadButton*>(e) : nullptr;
}
template <> inline const InputEventMouse* event_cast(const InputEvent* e) {
    return e && (e->type == InputEventType::MouseButton || e->type == InputEventType::MouseMotion)
               ? static_cast<const InputEventMouse*>(e)
               : nullptr;
}
template <> inline const InputEventMouseButton* event_cast(const InputEvent* e) {
    return e && e->type == InputEventType::MouseButton ? static_cast<const InputEventMouseButton*>(e) : nullptr;
}
template <> inline const InputEventMouseMotion* event_cast(const InputEvent* e) {
    return e && e->type == InputEventType::MouseMotion ? static_cast<const InputEventMouseMotion*>(e) : nullptr;
}
template <> inline const InputEventScreenTouch* event_cast(const InputEvent* e) {
    return e && e->type == InputEventType::ScreenTouch ? static_cast<const InputEventScreenTouch*>(e) : nullptr;
}
template <> inline const InputEventScreenDrag* event_cast(const InputEvent* e) {
    return e && e->type == InputEventType::ScreenDrag ? static_cast<const InputEventScreenDrag*>(e) : nullptr;
}

// OptionsManager saves the input map with var_to_str / str_to_var. The port's
// save is its own format (game::SaveStore), so an event is written as a short
// text the port reads back: "key:<keycode>:<physical>:<device>",
// "joy:<index>:<device>", "mouse:<index>:<device>". An unreadable text is null.
std::string event_to_str(const InputEvent& event);
InputEventRef str_to_event(const std::string& text);

}  // namespace godot
