// The Viewport's GUI half, transcribed from Godot 4.1.4-stable:
//   viewport.cpp  _gui_control_grab_focus, gui_release_focus, gui_find_control,
//                 _gui_find_control_at_pos, _gui_call_input:1543,
//                 _gui_call_notification, _gui_input_event:1726 (mouse button,
//                 mouse motion, key/joypad focus branch ~2196)
//   control.cpp   _get_focus_neighbor:2233, _window_find_focus_neighbor:2310
//   base_button.cpp gui_input:56, on_action_event:147, _pressed:135
//   geometry_2d.h get_closest_points_between_segments
//
// PORT, as main.tscn uses the GUI:
//   - drag and drop and tooltips are not transcribed: tooltip_text is set 0
//     times and no script overrides _get_drag_data / _can_drop_data;
//   - clip_contents is true 0 times and top_level 0 times, so every child is
//     searched and no control breaks the parent chain;
//   - there is one Viewport, no subwindows and no canvas layers between GUI
//     roots, so roots compare in tree order (Control::CComparator);
//   - the `gui_input` signal and `_gui_input` virtual are not modelled: no
//     translated script connects or overrides them. Only Button has gui_input.
#include "godot/control.h"
#include "godot/input_map.h"
#include "godot/scene_tree.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace godot {
namespace {

constexpr int kMaxNeighborSearchCount = 512;   // control.cpp:2231
constexpr float kCmpEpsilon = 0.00001f;         // math_defs.h CMP_EPSILON

Vector2 Sub(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
Vector2 Add(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
Vector2 Mul(Vector2 a, float s) { return {a.x * s, a.y * s}; }
float Dot(Vector2 a, Vector2 b) { return a.x * b.x + a.y * b.y; }
float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// geometry_2d.h Geometry2D::get_closest_points_between_segments
float ClosestDistanceBetweenSegments(Vector2 p1, Vector2 q1, Vector2 p2, Vector2 q2) {
    const Vector2 d1 = Sub(q1, p1);
    const Vector2 d2 = Sub(q2, p2);
    const Vector2 r = Sub(p1, p2);
    const float a = Dot(d1, d1);
    const float e = Dot(d2, d2);
    const float f = Dot(d2, r);
    float s = 0.0f;
    float t = 0.0f;
    if (a <= kCmpEpsilon && e <= kCmpEpsilon) {
        const Vector2 d = Sub(p1, p2);
        return std::sqrt(Dot(d, d));
    }
    if (a <= kCmpEpsilon) {
        s = 0.0f;
        t = Clamp01(f / e);
    } else {
        const float c = Dot(d1, r);
        if (e <= kCmpEpsilon) {
            t = 0.0f;
            s = Clamp01(-c / a);
        } else {
            const float b = Dot(d1, d2);
            const float denom = a * e - b * b;
            s = denom != 0.0f ? Clamp01((b * f - c * e) / denom) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = Clamp01(-c / a);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = Clamp01((b - c) / a);
            }
        }
    }
    const Vector2 c1 = Add(p1, Mul(d1, s));
    const Vector2 c2 = Add(p2, Mul(d2, t));
    const Vector2 d = Sub(c1, c2);
    return std::sqrt(Dot(d, d));
}

void Corners(const Control& c, Vector2 out[4]) {
    const Transform2D& x = c.get_global_transform();
    const Vector2 size = c.get_size();
    out[0] = x.xform({0.0f, 0.0f});
    out[1] = x.xform({size.x, 0.0f});
    out[2] = x.xform(size);
    out[3] = x.xform({0.0f, size.y});
}

Control* ParentControl(const Node* n) {
    return n ? cast_to<Control>(n->get_parent()) : nullptr;
}

// A GUI root: a Control whose parent is not a CanvasItem (data.RI set in
// Control NOTIFICATION_ENTER_CANVAS, control.cpp:3168).
bool IsRootControl(const Control* c) {
    return c && !ParentControl(c);
}

// viewport.cpp _gui_find_control_at_pos
Control* FindControlAtPos(Control* c, Vector2 global) {
    if (!c->is_visible()) return nullptr;
    if (c->get_global_transform().determinant() == 0.0f) return nullptr;
    for (int i = c->get_child_count() - 1; i >= 0; --i) {
        Control* child = cast_to<Control>(c->get_child(i));
        if (!child) continue;
        if (Control* hit = FindControlAtPos(child, global)) return hit;
    }
    if (c->get_mouse_filter() == Control::MOUSE_FILTER_IGNORE) return nullptr;
    const Vector2 local = c->get_global_transform().affine_inverse().xform(global);
    return c->has_point(local) ? c : nullptr;
}

// A copy of a pointer event with its position in `c`'s local space
// (mb->set_position(get_global_transform_with_canvas().affine_inverse().xform(mpos))).
InputEventRef Localized(const InputEvent& event, const Control& c, Vector2 global) {
    const Vector2 local = c.get_global_transform().affine_inverse().xform(global);
    if (const auto* mb = event_cast<InputEventMouseButton>(&event)) {
        auto copy = std::make_shared<InputEventMouseButton>(*mb);
        copy->position = local;
        return copy;
    }
    if (const auto* mm = event_cast<InputEventMouseMotion>(&event)) {
        auto copy = std::make_shared<InputEventMouseMotion>(*mm);
        copy->position = local;
        copy->relative = c.get_global_transform().affine_inverse().basis_xform(mm->relative);
        return copy;
    }
    return nullptr;
}

void CallGuiInput(Control& c, const InputEvent& event) {
    if (auto* button = cast_to<Button>(&c)) button->gui_input(event);
}

}  // namespace

// --- focus --------------------------------------------------------------------

void SceneTree::gui_grab_focus(Control* control) {
    if (!control || (focus_owner_ && focus_owner_ == control)) return;
    gui_release_focus();
    if (control->get_tree() != this) return;
    focus_owner_ = control;
    const std::vector<std::function<void(Control*)>> callbacks = focus_changed_;
    for (const auto& cb : callbacks) cb(control);
    control->emit_signal("focus_entered");
}

void SceneTree::gui_release_focus() {
    if (!focus_owner_) return;
    Control* f = focus_owner_;
    focus_owner_ = nullptr;
    f->emit_signal("focus_exited");
}

// viewport.cpp:2381. PORT: _drop_mouse_focus also sends the held buttons a
// release event; nothing in main.tscn is hidden while a mouse button is held
// on it, so the port only forgets the mouse focus.
void SceneTree::gui_hide_control(Control* control) {
    if (mouse_focus_ == control) {
        mouse_focus_ = nullptr;
        mouse_focus_mask_ = 0;
    }
    if (focus_owner_ == control) gui_release_focus();
    if (mouse_over_ == control) mouse_over_ = nullptr;
}

namespace {

// canvas_item.cpp _propagate_visibility_changed (:63): a visible CanvasItem
// child is notified in turn, a hidden one stops the walk. A node that is not a
// CanvasItem (not a Control, here) is not visited, nor are its children.
void NotifyHidden(SceneTree& tree, Control& control) {
    tree.gui_hide_control(&control);   // control.cpp:3251, now !is_visible_in_tree()
    for (int i = 0; i < control.get_child_count(); ++i) {
        Control* child = cast_to<Control>(control.get_child(i));
        if (child && child->is_visible()) NotifyHidden(tree, *child);
    }
}

}  // namespace

// canvas_item.cpp:72 set_visible. Showing notifies too, but control.cpp only
// resizes then (:3257), so only a hide reaches the Viewport.
void Control::set_visible(bool v) {
    if (visible_ == v) return;
    visible_ = v;
    if (v) return;
    SceneTree* tree = get_tree();
    if (!tree) return;
    const Control* parent = cast_to<Control>(get_parent());
    if (parent && !parent->is_visible_in_tree()) {
        // canvas_item.cpp:80: already off screen, so only this item is notified.
        tree->gui_hide_control(this);
        return;
    }
    NotifyHidden(*tree, *this);
}

Node* SceneTree::get_node_relative(const Node* from, std::string_view relative) const {
    Node* at = const_cast<Node*>(from);
    std::size_t start = 0;
    while (at && start <= relative.size()) {
        const std::size_t slash = relative.find('/', start);
        const std::string_view part =
            relative.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        if (part == "..") {
            at = at->get_parent();
        } else if (!part.empty() && part != ".") {
            Node* next = nullptr;
            for (int i = 0; i < at->get_child_count(); ++i) {
                if (at->get_child(i)->get_name() == part) {
                    next = at->get_child(i);
                    break;
                }
            }
            at = next;
        }
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return at;
}

// control.cpp _get_focus_neighbor:2233
Control* Control::_get_focus_neighbor(int side, int count) {
    if (side < 0 || side > 3 || count >= kMaxNeighborSearchCount) return nullptr;
    SceneTree* tree = get_tree();
    if (!tree) return nullptr;

    if (!focus_neighbor_[side].empty()) {
        Node* n = tree->get_node_relative(this, focus_neighbor_[side]);
        if (!n) {
            std::fprintf(stderr, "Neighbor focus node path is invalid: '%s'.\n", focus_neighbor_[side].c_str());
            return nullptr;
        }
        Control* c = cast_to<Control>(n);
        if (!c) {
            std::fprintf(stderr, "Neighbor focus node is not a control: '%s'.\n", n->get_name().c_str());
            return nullptr;
        }
        const bool valid = c->is_visible() && c->get_focus_mode() != FOCUS_NONE;
        if (valid) return c;
        return c->_get_focus_neighbor(side, count + 1);
    }

    Vector2 points[4];
    Corners(*this, points);
    const Vector2 dir[4] = {{-1.0f, 0.0f}, {0.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
    const Vector2 vdir = dir[side];
    float maxd = -1e7f;
    for (const Vector2& p : points) maxd = std::max(maxd, Dot(vdir, p));

    Node* base = this;
    while (base) {
        if (IsRootControl(cast_to<Control>(base))) break;
        base = base->get_parent();
    }
    if (!base) return nullptr;

    float dist = 1e7f;
    Control* result = nullptr;
    // control.cpp _window_find_focus_neighbor:2310
    struct Finder {
        Control* self;
        Vector2 vdir;
        const Vector2* points;
        float min;
        float* dist;
        Control** result;
        void operator()(Node* at) const {
            Control* c = cast_to<Control>(at);
            if (c && c != self && c->get_focus_mode() == FOCUS_ALL && c->is_visible_in_tree()) {
                Vector2 cp[4];
                Corners(*c, cp);
                float mn = 1e7f;
                for (const Vector2& p : cp) mn = std::min(mn, Dot(vdir, p));
                if (mn > (min - kCmpEpsilon)) {
                    for (int i = 0; i < 4; ++i) {
                        for (int j = 0; j < 4; ++j) {
                            const float d =
                                ClosestDistanceBetweenSegments(points[i], points[(i + 1) % 4], cp[j], cp[(j + 1) % 4]);
                            if (d < *dist) {
                                *dist = d;
                                *result = c;
                            }
                        }
                    }
                }
            }
            for (int i = 0; i < at->get_child_count(); ++i) {
                Node* child = at->get_child(i);
                // A Control child of a non-Control is another GUI root: skipped.
                if (cast_to<Control>(child) && !c) continue;
                (*this)(child);
            }
        }
    };
    Finder{this, vdir, points, maxd, &dist, &result}(base);
    return result;
}

// PORT (control.h header note): TextShaker, and the port's credit
// (PortBranding::MakeRoomForTheCredit), are the only writers of a position.
void Control::set_position(Vector2 p) {
    const Vector2 delta = Sub(p, position_);
    position_ = p;
    const Control* parent = ParentControl(this);
    const Vector2 d = parent ? parent->get_global_transform().basis_xform(delta) : delta;
    struct Shift {
        Vector2 d;
        void operator()(Control& c) const {
            Transform2D x = c.get_global_transform();
            x.ox += d.x;
            x.oy += d.y;
            c.set_global_transform(x);
            c.add_offset_since_load(d);   // the 2D draw's rects are the load-time ones
            for (int i = 0; i < c.get_child_count(); ++i) {
                if (auto* child = cast_to<Control>(c.get_child(i))) (*this)(*child);
            }
        }
    };
    Shift{d}(*this);
}

// --- the pointer --------------------------------------------------------------

// viewport.cpp gui_find_control: roots searched last to first.
Control* SceneTree::gui_find_control(Vector2 global) const {
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
        Control* root = cast_to<Control>(*it);
        if (!IsRootControl(root) || !root->is_visible_in_tree()) continue;
        if (Control* hit = FindControlAtPos(root, global)) return hit;
    }
    return nullptr;
}

// --- BaseButton -----------------------------------------------------------------

// base_button.cpp gui_input:56
void Button::gui_input(const InputEvent& event) {
    const auto* mb = event_cast<InputEventMouseButton>(&event);
    const bool ui_accept = event.is_action("ui_accept", true) && !event.is_echo();
    const bool button_masked = mb && (button_mask & (mouse::MASK_LEFT << (mb->button_index - mouse::LEFT))) != 0;
    if (button_masked || ui_accept) {
        // on_action_event:147, ACTION_MODE_BUTTON_RELEASE (base_button.h:58), not toggle mode.
        if (event.is_pressed()) {
            press_attempt = true;
            pressing_inside = true;
            emit_signal("button_down");
        }
        if (press_attempt && pressing_inside && !event.is_pressed()) {
            emit_signal("pressed");   // _pressed:135
        }
        if (!event.is_pressed()) {
            press_attempt = false;
            pressing_inside = false;
            emit_signal("button_up");
        }
        return;
    }
    if (const auto* mm = event_cast<InputEventMouseMotion>(&event)) {
        if (press_attempt) pressing_inside = has_point(mm->position);
    }
}

// --- _gui_input_event ----------------------------------------------------------

namespace {

// viewport.cpp _gui_call_notification: up the chain, skipping IGNORE controls,
// stopping at STOP.
void CallNotificationChain(Control* c, const char* signal) {
    for (; c; c = ParentControl(c)) {
        if (c->get_mouse_filter() != Control::MOUSE_FILTER_IGNORE) c->emit_signal(signal);
        if (c->get_mouse_filter() == Control::MOUSE_FILTER_STOP) break;
    }
}

}  // namespace

void SceneTree::gui_input_event(const InputEventRef& event) {
    // viewport.cpp _gui_call_input:1543 for pointer events.
    const auto call_input = [this](Control* control, const InputEvent& ev, Vector2 global) {
        bool stopped = false;
        for (Control* c = control; c; c = ParentControl(c)) {
            if (c->get_mouse_filter() != Control::MOUSE_FILTER_IGNORE) {
                const InputEventRef local = Localized(ev, *c, global);
                CallGuiInput(*c, local ? *local : ev);
            }
            if (key_event_accepted_) {
                stopped = true;
                break;
            }
            if (c->get_mouse_filter() == Control::MOUSE_FILTER_STOP) {
                stopped = true;
                break;
            }
            if (input_handled_) break;
        }
        return stopped;
    };

    if (const auto* mb = event_cast<InputEventMouseButton>(event.get())) {
        key_event_accepted_ = false;
        const Vector2 mpos = mb->position;
        const int bit = mouse::MASK_LEFT << (mb->button_index - mouse::LEFT);
        if (mb->is_pressed()) {
            if (mouse_focus_mask_ != 0) {
                mouse_focus_mask_ |= bit;
            } else {
                mouse_focus_ = gui_find_control(mpos);
                if (!mouse_focus_) {
                    mouse_focus_mask_ = 0;
                    return;
                }
                mouse_focus_mask_ = bit;
            }
            if (!mouse_focus_) return;
            if (mb->button_index == mouse::LEFT) {
                for (Control* c = mouse_focus_; c; c = ParentControl(c)) {
                    if (c->get_focus_mode() != Control::FOCUS_NONE) {
                        if (c != focus_owner_) c->grab_focus();
                        break;
                    }
                    if (c->get_mouse_filter() == Control::MOUSE_FILTER_STOP) break;
                }
            }
            if (call_input(mouse_focus_, *mb, mpos)) set_input_as_handled();
        } else {
            mouse_focus_mask_ &= ~bit;
            if (!mouse_focus_) return;
            Control* focus = mouse_focus_;
            if (mouse_focus_mask_ == 0) mouse_focus_ = nullptr;
            if (call_input(focus, *mb, mpos)) set_input_as_handled();
        }
        return;
    }

    if (const auto* mm = event_cast<InputEventMouseMotion>(event.get())) {
        key_event_accepted_ = false;
        const Vector2 mpos = mm->position;
        Control* over = gui_find_control(mpos);
        if (over != mouse_over_) {
            if (mouse_over_) CallNotificationChain(mouse_over_, "mouse_exited");   // _drop_mouse_over
            mouse_over_ = nullptr;
            if (over) {
                CallNotificationChain(over, "mouse_entered");
                mouse_over_ = over;
            }
        }
        if (mouse_focus_) over = mouse_focus_;
        if (over && call_input(over, *mm, mpos)) set_input_as_handled();
        return;
    }

    // The key / joypad branch: is_action_type() events that are not mouse.
    if (event->type != InputEventType::Key && event->type != InputEventType::JoypadButton) return;

    if (focus_owner_ && !focus_owner_->is_visible_in_tree()) focus_owner_->release_focus();

    if (focus_owner_) {
        key_event_accepted_ = false;
        CallGuiInput(*focus_owner_, *event);
        if (key_event_accepted_) {
            set_input_as_handled();
            return;
        }
    }

    Control* from = focus_owner_;
    if (!from || !event->is_pressed()) return;

    const InputMap& map = InputMap::get_singleton();
    const auto pressed = [&](const char* action) {
        return map.has_action(action) && event->is_action_pressed(action, true, true);
    };
    Control* next = nullptr;
    // PORT: ui_focus_next / ui_focus_prev have no bound events in this project
    // (input_map.json), so find_next/prev_valid_focus are never reached; a press
    // that did match is reported rather than silently ignored.
    if (pressed("ui_focus_next") || pressed("ui_focus_prev")) {
        std::fprintf(stderr, "gui: ui_focus_next/prev matched but is not transcribed\n");
    }
    if (pressed("ui_up")) next = from->_get_focus_neighbor(SIDE_TOP);
    if (pressed("ui_left")) next = from->_get_focus_neighbor(SIDE_LEFT);
    if (pressed("ui_right")) next = from->_get_focus_neighbor(SIDE_RIGHT);
    if (pressed("ui_down")) next = from->_get_focus_neighbor(SIDE_BOTTOM);
    if (next) {
        next->grab_focus();
        set_input_as_handled();
    }
}

}  // namespace godot
