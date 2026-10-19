#include "godot/input_map.h"

#include "core/json_lite.h"

#include <cstdio>
#include <string>

namespace godot {
namespace {

bool ReadWholeFile(const char* path, std::string& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

InputEventRef ReadEventBody(const char* b, const char* e) {
    using namespace core::json;
    const std::string type = ReadStringTopLevelDecoded(b, e, "type");
    if (type == "key") {
        auto k = std::make_shared<InputEventKey>();
        k->keycode = static_cast<int64_t>(ReadNumberTopLevel(b, e, "keycode", 0.0));
        k->physical_keycode = static_cast<int64_t>(ReadNumberTopLevel(b, e, "physical_keycode", 0.0));
        return k;
    }
    if (type == "joypad_button") {
        auto j = std::make_shared<InputEventJoypadButton>();
        j->button_index = ReadIntTopLevel(b, e, "button_index", 0);
        return j;
    }
    if (type == "mouse_button") {
        auto m = std::make_shared<InputEventMouseButton>();
        m->button_index = ReadIntTopLevel(b, e, "button_index", 0);
        return m;
    }
    return nullptr;
}

InputEventRef ReadEvent(const char* b, const char* e) {
    InputEventRef ev = ReadEventBody(b, e);
    if (ev) ev->device = core::json::ReadIntTopLevel(b, e, "device", InputMap::ALL_DEVICES);
    return ev;
}

}  // namespace

InputMap& InputMap::get_singleton() {
    static InputMap map;
    return map;
}

const InputMap::Action* InputMap::find(std::string_view name) const {
    for (const Action& a : actions_) {
        if (a.name == name) return &a;
    }
    return nullptr;
}

InputMap::Action* InputMap::find(std::string_view name) {
    return const_cast<Action*>(static_cast<const InputMap*>(this)->find(name));
}

void InputMap::add_action(const std::string& action) {
    if (!find(action)) actions_.push_back({action, {}});
}

void InputMap::action_add_event(const std::string& action, InputEventRef event) {
    if (Action* a = find(action)) a->events.push_back(std::move(event));
}

const std::vector<InputEventRef>& InputMap::action_get_events(std::string_view action) const {
    static const std::vector<InputEventRef> kNone;
    const Action* a = find(action);
    return a ? a->events : kNone;
}

void InputMap::action_erase_events(std::string_view action) {
    if (Action* a = find(action)) a->events.clear();
}

std::vector<std::string> InputMap::get_actions() const {
    std::vector<std::string> out;
    out.reserve(actions_.size());
    for (const Action& a : actions_) out.push_back(a.name);
    return out;
}

// input_map.cpp event_get_action_status + _find_event:130: the first bound
// event on ALL_DEVICES or on the event's own device whose action_match accepts
// `event` decides the pressed state.
bool InputMap::event_get_action_status(const InputEvent& event, std::string_view action, bool exact,
                                       bool* r_pressed) const {
    const Action* a = find(action);
    if (!a) {
        std::fprintf(stderr, "InputMap: the action \"%.*s\" does not exist\n",
                     static_cast<int>(action.size()), action.data());
        return false;
    }
    for (const InputEventRef& bound : a->events) {
        if (bound->device != ALL_DEVICES && bound->device != event.device) continue;
        bool pressed = false;
        if (bound->action_match(event, exact, &pressed)) {
            if (r_pressed) *r_pressed = pressed;
            return true;
        }
    }
    return false;
}

bool InputMap::load(const char* json_path) {
    using namespace core::json;
    actions_.clear();
    std::string text;
    if (!ReadWholeFile(json_path, text)) return false;
    const char* b = text.data();
    const char* e = b + text.size();
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (!FindArrayTopLevel(b, e, "actions", &ab, &ae)) return false;
    std::vector<Span> actions;
    SplitObjects(ab, ae, actions);
    for (const Span& s : actions) {
        const std::string name = ReadStringTopLevelDecoded(s.first, s.second, "name");
        if (name.empty()) return false;
        add_action(name);
        const char* eb = nullptr;
        const char* ee = nullptr;
        if (!FindArrayTopLevel(s.first, s.second, "events", &eb, &ee)) continue;
        std::vector<Span> events;
        SplitObjects(eb, ee, events);
        for (const Span& ev : events) {
            if (InputEventRef parsed = ReadEvent(ev.first, ev.second)) action_add_event(name, std::move(parsed));
        }
    }
    return !actions_.empty();
}

}  // namespace godot
