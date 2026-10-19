// The 2D Controls the scripts touch: visibility, text, the typewriter's
// visible_characters, the dialogue backdrop's scale, focus, and the GUI state
// Godot's Viewport needs to navigate focus and hit-test the pointer. Rects and
// fonts for drawing stay in ui_layout.json / ui_fonts.json.
//
// Rules from Godot 4.1.4-stable:
//   - CanvasItem::is_visible_in_tree ANDs own `visible` up the CanvasItem
//     parents and stops at a parent that is not one (a Node3D, a plain Node).
//     The port models every CanvasItem the scripts reach as a Control.
//   - Label::set_text (label.cpp:764) returns when the text is unchanged, else
//     rescales visible_characters by visible_ratio while that ratio is < 1.
//   - Label::set_visible_characters (label.cpp:899): ratio = amount / total as
//     float, 1.0 when the text is empty. Total = xl_text.length() (label.cpp:984),
//     newlines included.
//   - Label::set_text stores xl_text = atr(text) (label.cpp:769) and draws
//     xl_text; a translation change re-translates it (label.cpp:380). PORT:
//     get_xl_text refreshes when translation_generation() has moved on.
//   - Control defaults: focus_mode FOCUS_NONE (control.h:184), mouse_filter
//     MOUSE_FILTER_STOP (control.h:212); BaseButton's constructor sets
//     FOCUS_ALL (base_button.cpp:504).
//   - Control::grab_focus does nothing for FOCUS_NONE (control.cpp grab_focus).
//   - Control::has_point: Rect2(Point2(), size).has_point in local space
//     (control.cpp:1822).
//
// PORT: the global Transform2D is the one Godot computed at export
// (get_global_transform). The only Control transform a translated script
// writes is TextShaker's `position`, so set_position moves the global origin by
// the change mapped through the parent's basis; rotation, scale, pivot and size
// are never written at runtime.
#pragma once

#include "godot/input_event.h"
#include "godot/node.h"
#include "godot/translation.h"
#include "godot/scene_tree.h"

#include <string>

namespace godot {

// Side (core/math/math_defs.h)
inline constexpr int SIDE_LEFT = 0;
inline constexpr int SIDE_TOP = 1;
inline constexpr int SIDE_RIGHT = 2;
inline constexpr int SIDE_BOTTOM = 3;

// A 2D affine transform, column form: x = (xx, xy), y = (yx, yy), origin (ox, oy).
struct Transform2D {
    float xx = 1.0f, xy = 0.0f;
    float yx = 0.0f, yy = 1.0f;
    float ox = 0.0f, oy = 0.0f;

    Vector2 xform(Vector2 p) const { return {xx * p.x + yx * p.y + ox, xy * p.x + yy * p.y + oy}; }
    Vector2 basis_xform(Vector2 v) const { return {xx * v.x + yx * v.y, xy * v.x + yy * v.y}; }
    float determinant() const { return xx * yy - xy * yx; }
    // Transform2D::affine_inverse (transform_2d.cpp).
    Transform2D affine_inverse() const {
        const float det = determinant();
        const float idet = det != 0.0f ? 1.0f / det : 0.0f;
        Transform2D r;
        r.xx = yy * idet;
        r.xy = -xy * idet;
        r.yx = -yx * idet;
        r.yy = xx * idet;
        const Vector2 o = r.basis_xform({-ox, -oy});
        r.ox = o.x;
        r.oy = o.y;
        return r;
    }
};

class Control : public Node {
public:
    static constexpr uint32_t kClassBit = kClassControl;

    enum FocusMode { FOCUS_NONE = 0, FOCUS_CLICK = 1, FOCUS_ALL = 2 };
    enum MouseFilter { MOUSE_FILTER_STOP = 0, MOUSE_FILTER_PASS = 1, MOUSE_FILTER_IGNORE = 2 };

    explicit Control(std::string name) : Node(std::move(name), kClassControl) { set_class("Control"); }

    bool is_visible() const { return visible_; }
    // canvas_item.cpp:72. Hiding releases the focus of every control it takes
    // off screen (control.cpp:3251 -> viewport.cpp:2381; gui.cpp).
    void set_visible(bool v);
    bool is_visible_in_tree() const {
        for (const Node* n = this; n; n = n->get_parent()) {
            const Control* c = cast_to<Control>(n);
            if (!c) break;
            if (!c->visible_) return false;
        }
        return true;
    }

    Color   get_modulate() const { return modulate_; }
    void    set_modulate(const Color& c) { modulate_ = c; }
    Vector2 get_scale() const { return scale_; }
    void    set_scale(const Vector2& s) { scale_ = s; }
    // CanvasItem.self_modulate: this item's own tint, not inherited.
    Color   get_self_modulate() const { return self_modulate_; }
    void    set_self_modulate(const Color& c) { self_modulate_ = c; }
    // Control.rotation (radians). PORT: recorded, not drawn (the UI draw uses
    // the exported global transform), like scale.
    float   get_rotation() const { return rotation_; }
    void    set_rotation(float r) { rotation_ = r; }

    // --- focus --------------------------------------------------------------
    int  get_focus_mode() const { return focus_mode_; }
    void set_focus_mode(int mode) { focus_mode_ = mode; }
    const std::string& get_focus_neighbor(int side) const { return focus_neighbor_[side]; }
    void set_focus_neighbor(int side, std::string path) { focus_neighbor_[side] = std::move(path); }

    void grab_focus() {
        if (focus_mode_ == FOCUS_NONE) return;
        if (SceneTree* t = get_tree()) t->gui_grab_focus(this);
    }
    void release_focus() {
        SceneTree* t = get_tree();
        if (t && t->gui_get_focus_owner() == this) t->gui_release_focus();
    }
    bool has_focus() const {
        const SceneTree* t = get_tree();
        return t && t->gui_get_focus_owner() == this;
    }

    // control.cpp _get_focus_neighbor:2233 (gui.cpp).
    Control* _get_focus_neighbor(int side, int count = 0);

    // --- geometry and the pointer -------------------------------------------
    int  get_mouse_filter() const { return mouse_filter_; }
    void set_mouse_filter(int filter) { mouse_filter_ = filter; }

    const Transform2D& get_global_transform() const { return global_xform_; }
    void set_global_transform(const Transform2D& t) { global_xform_ = t; }
    Vector2 get_size() const { return size_; }
    void    set_size(Vector2 s) { size_ = s; }
    // An axis-aligned global rect: translation-only transform plus size.
    void set_global_rect(Vector2 position, Vector2 size) {
        global_xform_ = Transform2D{};
        global_xform_.ox = position.x;
        global_xform_.oy = position.y;
        size_ = size;
    }

    Vector2 get_position() const { return position_; }
    // PORT: see the header note; moves the exported global origin with it.
    void set_position(Vector2 p);
    void set_position_at_load(Vector2 p) { position_ = p; }

    // How far this control has moved since the scene loaded, in global space.
    // The 2D draw reads rects from ui_layout.json, which holds the load-time
    // ones, so TextShaker's jitter reaches the screen through this.
    Vector2 get_offset_since_load() const { return offset_since_load_; }
    void    add_offset_since_load(Vector2 d) {
        offset_since_load_.x += d.x;
        offset_since_load_.y += d.y;
    }

    bool has_point(Vector2 local) const {
        return local.x >= 0.0f && local.y >= 0.0f && local.x < size_.x && local.y < size_.y;
    }

protected:
    Control(std::string name, uint32_t bits) : Node(std::move(name), kClassControl | bits) {}

private:
    bool visible_ = true;
    Color modulate_;
    Color self_modulate_;
    Vector2 scale_{1.0f, 1.0f};
    float rotation_ = 0.0f;
    int focus_mode_ = FOCUS_NONE;
    int mouse_filter_ = MOUSE_FILTER_STOP;
    std::string focus_neighbor_[4];
    Transform2D global_xform_;
    Vector2 size_;
    Vector2 position_;
    Vector2 offset_since_load_;
};

class Label : public Control {
public:
    static constexpr uint32_t kClassBit = kClassLabel;

    explicit Label(std::string name) : Control(std::move(name), kClassLabel) { set_class("Label"); }

    // theme_override_colors/font_color, when a script or animation sets it
    // (the menu's volume text flickers through it).
    bool  has_font_color_override() const { return has_font_color_; }
    Color get_font_color_override() const { return font_color_; }
    void  set_font_color_override(const Color& c) { font_color_ = c; has_font_color_ = true; }

    const std::string& get_text() const { return text_; }
    void set_text(const std::string& t) {
        if (text_ == t) return;
        text_ = t;
        xl_generation_ = ~0u;   // re-translate on the next get_xl_text
        if (visible_ratio_ < 1.0f) {
            visible_chars_ = static_cast<int>(static_cast<float>(get_total_character_count()) * visible_ratio_);
        }
    }

    int  get_visible_characters() const { return visible_chars_; }
    void set_visible_characters(int amount) {
        if (visible_chars_ == amount) return;
        visible_chars_ = amount;
        const int total = get_total_character_count();
        visible_ratio_ = total > 0 ? static_cast<float>(amount) / static_cast<float>(total) : 1.0f;
    }

    // What the Label draws: its text through the translation table.
    const std::string& get_xl_text() const {
        const unsigned generation = translation_generation();
        if (xl_generation_ != generation) {
            xl_text_ = tr(text_);
            xl_generation_ = generation;
        }
        return xl_text_;
    }

    // String.length() counts UTF-32 characters; the text is UTF-8 here, so the
    // count is of code points (every language).
    int   get_total_character_count() const { return utf8_length(get_xl_text()); }
    float get_visible_ratio() const { return visible_ratio_; }

private:
    Color font_color_{1.0f, 1.0f, 1.0f, 1.0f};
    bool  has_font_color_ = false;
    std::string text_;
    mutable std::string xl_text_;
    mutable unsigned xl_generation_ = ~0u;
    int   visible_chars_ = -1;
    float visible_ratio_ = 1.0f;
};

class ColorRect : public Control {
public:
    static constexpr uint32_t kClassBit = kClassColorRect;
    explicit ColorRect(std::string name) : Control(std::move(name), kClassColorRect) { set_class("ColorRect"); }
    // What it fills with, live: the menu's splash fades it (color_rect.cpp).
    // `has_color` is false until the export or a script sets it, and the 2D
    // draw then uses the layout's load-time colour.
    Color get_color() const { return color_; }
    void  set_color(const Color& c) { color_ = c; has_color_ = true; }
    bool  has_color() const { return has_color_; }

private:
    Color color_{1.0f, 1.0f, 1.0f, 1.0f};
    bool  has_color_ = false;
};

class TextureRect : public Control {
public:
    static constexpr uint32_t kClassBit = kClassTextureRect;
    explicit TextureRect(std::string name) : Control(std::move(name), kClassTextureRect) { set_class("TextureRect"); }

    // The res:// path a script assigned to `texture`; "" until one does (the
    // layout's load-time texture draws until then).
    void set_texture(std::string res) { texture_ = std::move(res); }
    const std::string& get_texture() const { return texture_; }

private:
    std::string texture_;
};

// Button with BaseButton's press semantics (base_button.cpp). PORT: toggle
// mode, button groups, shortcuts and disabled are not modelled: no Button in
// main.tscn uses them.
class Button : public Control {
public:
    static constexpr uint32_t kClassBit = kClassButton;
    explicit Button(std::string name) : Control(std::move(name), kClassButton) {
        set_class("Button");
        set_focus_mode(FOCUS_ALL);   // base_button.cpp:504
    }

    const std::string& get_text() const { return text_; }
    void set_text(std::string t) { text_ = std::move(t); }

    // base_button.cpp gui_input:56 + on_action_event:147 (gui.cpp). `event` is
    // already in the button's local space for mouse events.
    void gui_input(const InputEvent& event);

    bool press_attempt = false;
    bool pressing_inside = false;
    int  button_mask = mouse::MASK_LEFT;   // base_button.h:49

private:
    std::string text_;
};

}  // namespace godot
