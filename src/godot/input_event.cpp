#include "godot/input_event.h"

#include "godot/input_map.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace godot {

// input_event.cpp InputEvent::is_action -> InputMap::event_is_action, which is
// event_get_action_status with no outputs (input_map.cpp).
bool InputEvent::is_action(std::string_view action, bool exact) const {
    return InputMap::get_singleton().event_get_action_status(*this, action, exact, nullptr);
}

// input_event.cpp InputEvent::is_action_pressed
bool InputEvent::is_action_pressed(std::string_view action, bool allow_echo, bool exact) const {
    bool pressed_state = false;
    const bool valid = InputMap::get_singleton().event_get_action_status(*this, action, exact, &pressed_state);
    return valid && pressed_state && (allow_echo || !is_echo());
}

// input_event.cpp InputEvent::is_action_released
bool InputEvent::is_action_released(std::string_view action, bool exact) const {
    bool pressed_state = false;
    const bool valid = InputMap::get_singleton().event_get_action_status(*this, action, exact, &pressed_state);
    return valid && !pressed_state;
}

bool InputEvent::action_match(const InputEvent&, bool, bool*) const {
    return false;
}

// input_event.cpp InputEventKey::action_match. PORT: no event carries modifier
// keys or a key_label (no bound action uses either), so the modifier masks are
// equal and key_label never decides.
bool InputEventKey::action_match(const InputEvent& event, bool /*exact*/, bool* r_pressed) const {
    const InputEventKey* key = event_cast<InputEventKey>(&event);
    if (!key) return false;
    bool match;
    if (keycode != 0) {
        match = keycode == key->keycode;
    } else if (physical_keycode != 0) {
        match = physical_keycode == key->physical_keycode;
    } else {
        match = false;
    }
    if (match && r_pressed) *r_pressed = key->is_pressed();
    return match;
}

// input_event.cpp InputEventJoypadButton::action_match
bool InputEventJoypadButton::action_match(const InputEvent& event, bool /*exact*/, bool* r_pressed) const {
    const InputEventJoypadButton* jb = event_cast<InputEventJoypadButton>(&event);
    if (!jb) return false;
    const bool match = button_index == jb->button_index;
    if (match && r_pressed) *r_pressed = jb->is_pressed();
    return match;
}

// input_event.cpp InputEventMouseButton::action_match. PORT: modifiers as above.
bool InputEventMouseButton::action_match(const InputEvent& event, bool /*exact*/, bool* r_pressed) const {
    const InputEventMouseButton* mb = event_cast<InputEventMouseButton>(&event);
    if (!mb) return false;
    const bool match = button_index == mb->button_index;
    if (match && r_pressed) *r_pressed = mb->is_pressed();
    return match;
}

// input_event.cpp InputEventMouseMotion::accumulate. PORT: one window and no
// modifier keys, so the window-id and modifier comparisons always pass.
bool InputEventMouseMotion::accumulate(const InputEvent& event) {
    const InputEventMouseMotion* motion = event_cast<InputEventMouseMotion>(&event);
    if (!motion) return false;
    if (is_canceled() != motion->is_canceled()) return false;
    if (is_pressed() != motion->is_pressed()) return false;
    if (button_mask != motion->button_mask) return false;

    position = motion->position;
    global_position = motion->global_position;
    velocity = motion->velocity;
    relative = {relative.x + motion->relative.x, relative.y + motion->relative.y};
    return true;
}

namespace {

struct KeyName {
    int64_t code;
    const char* text;
};

const KeyName kKeyNames[] = {
#include "godot/keycode_names.inc"
};

// keyboard.cpp keycode_get_string, without modifiers (the project binds none).
std::string KeycodeString(int64_t code) {
    for (const KeyName& k : kKeyNames) {
        if (k.code == code) return k.text;
    }
    if (code <= 0) return std::string();
    // String::chr(code), as UTF-8.
    std::string out;
    const uint32_t c = static_cast<uint32_t>(code);
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
    return out;
}

const char* const kJoyDescriptions[] = {
    "Bottom Action, Sony Cross, Xbox A, Nintendo B",
    "Right Action, Sony Circle, Xbox B, Nintendo A",
    "Left Action, Sony Square, Xbox X, Nintendo Y",
    "Top Action, Sony Triangle, Xbox Y, Nintendo X",
    "Back, Sony Select, Xbox Back, Nintendo -",
    "Guide, Sony PS, Xbox Home",
    "Start, Xbox Menu, Nintendo +",
    "Left Stick, Sony L3, Xbox L/LS",
    "Right Stick, Sony R3, Xbox R/RS",
    "Left Shoulder, Sony L1, Xbox LB",
    "Right Shoulder, Sony R1, Xbox RB",
    "D-pad Up",
    "D-pad Down",
    "D-pad Left",
    "D-pad Right",
    "Xbox Share, PS5 Microphone, Nintendo Capture",
    "Xbox Paddle 1",
    "Xbox Paddle 2",
    "Xbox Paddle 3",
    "Xbox Paddle 4",
    "PS4/5 Touchpad",
};

const char* const kMouseDescriptions[] = {
    "Left Mouse Button", "Right Mouse Button", "Middle Mouse Button",
    "Mouse Wheel Up",    "Mouse Wheel Down",   "Mouse Wheel Left",
    "Mouse Wheel Right", "Mouse Thumb Button 1", "Mouse Thumb Button 2",
};

}  // namespace

// input_event.cpp:439: keycode, else physical keycode + " (Physical)".
std::string InputEventKey::as_text() const {
    if (keycode != 0) return KeycodeString(keycode);
    if (physical_keycode != 0) return KeycodeString(physical_keycode) + " (Physical)";
    return "(Unset)";
}

// input_event.cpp:1212
std::string InputEventJoypadButton::as_text() const {
    std::string text = "Joypad Button " + std::to_string(button_index);
    if (button_index >= 0 &&
        button_index < static_cast<int>(sizeof(kJoyDescriptions) / sizeof(kJoyDescriptions[0]))) {
        text += " (";
        text += kJoyDescriptions[button_index];
        text += ")";
    }
    return text;
}

// input_event.cpp:773
std::string InputEventMouseButton::as_text() const {
    std::string text;
    if (button_index >= 1 && button_index <= 9) {
        text = kMouseDescriptions[button_index - 1];
    } else {
        text = "Button #" + std::to_string(button_index);
    }
    if (double_click) text += " (Double Click)";
    return text;
}

std::string event_to_str(const InputEvent& event) {
    if (const auto* k = event_cast<InputEventKey>(&event)) {
        return "key:" + std::to_string(k->keycode) + ":" + std::to_string(k->physical_keycode) + ":" +
               std::to_string(k->device);
    }
    if (const auto* j = event_cast<InputEventJoypadButton>(&event)) {
        return "joy:" + std::to_string(j->button_index) + ":" + std::to_string(j->device);
    }
    if (const auto* m = event_cast<InputEventMouseButton>(&event)) {
        return "mouse:" + std::to_string(m->button_index) + ":" + std::to_string(m->device);
    }
    return std::string();
}

InputEventRef str_to_event(const std::string& text) {
    std::vector<long long> n;
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos) return nullptr;
    const std::string kind = text.substr(0, colon);
    std::size_t at = colon + 1;
    while (at <= text.size()) {
        const std::size_t next = text.find(':', at);
        const std::string part = text.substr(at, next == std::string::npos ? std::string::npos : next - at);
        char* end = nullptr;
        const long long v = std::strtoll(part.c_str(), &end, 10);
        if (part.empty() || *end != '\0') return nullptr;
        n.push_back(v);
        if (next == std::string::npos) break;
        at = next + 1;
    }
    if (kind == "key" && n.size() == 3) {
        auto e = std::make_shared<InputEventKey>();
        e->keycode = n[0];
        e->physical_keycode = n[1];
        e->device = static_cast<int>(n[2]);
        return e;
    }
    if (kind == "joy" && n.size() == 2) {
        auto e = std::make_shared<InputEventJoypadButton>();
        e->button_index = static_cast<int>(n[0]);
        e->device = static_cast<int>(n[1]);
        return e;
    }
    if (kind == "mouse" && n.size() == 2) {
        auto e = std::make_shared<InputEventMouseButton>();
        e->button_index = static_cast<int>(n[0]);
        e->device = static_cast<int>(n[1]);
        return e;
    }
    return nullptr;
}

}  // namespace godot
