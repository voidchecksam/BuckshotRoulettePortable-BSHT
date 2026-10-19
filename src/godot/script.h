// A GDScript attached to a node. Each translated script (src/scripts/) derives
// from Script and keeps the GDScript's own class, method and field names.
//
// Lifecycle, as Godot 4.1.4 runs it (scene/main/node.cpp):
//   - _ready runs children first (_propagate_ready, node.cpp:217), once.
//   - A script that defines _process has set_process(true) called for it in
//     NOTIFICATION_READY before its _ready runs (node.cpp:179), so a _ready
//     that calls set_process(false) wins.
//   - _process runs in tree order with the frame's scaled delta.
//
// Scripts are created by the GDScript file name the export records
// ("DialogueManager"), because that is what the scene knows; the C++ class is
// the GDScript class_name ("Dialogue"). script_cast compares class names, so no
// RTTI is needed on either platform.
#pragma once

#include "godot/input_event.h"
#include "godot/variant.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace godot {

class Node;
class SceneTree;

class Script {
public:
    Script() = default;
    virtual ~Script() = default;
    Script(const Script&) = delete;
    Script& operator=(const Script&) = delete;

    // The GDScript class_name, e.g. "Dialogue".
    virtual const char* get_class_name() const = 0;

    virtual void _ready() {}
    // True when the GDScript defines _process: the node starts processing.
    virtual bool has_process() const { return false; }
    virtual void _process(double /*delta*/) {}

    // True when the GDScript defines _input / _unhandled_input: the node joins
    // Godot's input groups (node.cpp NOTIFICATION_READY sets them the same way).
    virtual bool has_input() const { return false; }
    virtual void _input(const InputEventRef& /*event*/) {}
    virtual bool has_unhandled_input() const { return false; }
    virtual void _unhandled_input(const InputEventRef& /*event*/) {}
    // _unhandled_key_input: InputEventKey only, after _unhandled_input
    // (viewport.cpp _push_unhandled_input_internal).
    virtual bool has_unhandled_key_input() const { return false; }
    virtual void _unhandled_key_input(const InputEventRef& /*event*/) {}

    // A METHOD track key or a call by name. False: no such method (the caller
    // counts it, never silently ignores it).
    virtual bool call(const std::string& /*method*/, const std::vector<Variant>& /*args*/) {
        return false;
    }

    Node* get_node_owner() const { return owner_; }
    SceneTree* get_tree() const;
    double get_process_delta_time() const;

private:
    friend class Node;
    friend class SceneTree;

    Node* owner_ = nullptr;
    bool readied_ = false;
};

// `x as T` for script classes: nullptr unless the node carries a T.
template <class T>
T* script_cast(Script* s) {
    if (!s) return nullptr;
    return std::string_view(s->get_class_name()) == T::kClassName ? static_cast<T*>(s) : nullptr;
}

using ScriptFactory = std::unique_ptr<Script> (*)();

// Filled by the generated scripts::RegisterScripts(). Re-registering a file
// replaces its factory.
void register_script_class(std::string_view file, ScriptFactory factory);
std::unique_ptr<Script> create_script(std::string_view file);

}  // namespace godot
