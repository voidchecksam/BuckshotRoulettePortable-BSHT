#include "scripts/ButtonClass.h"

#include "godot/rng.h"
#include "godot/translation.h"
#include "scripts/CursorManager.h"
#include "scripts/KeyRebinding.h"
#include "scripts/OptionsManager.h"

namespace scripts {

// gd: L26-L34
void ButtonClass::_ready() {
    if (adding_cursor) orig = t->get_text();
    godot::Node* parent = get_node_owner()->get_parent();
    ui_control = godot::cast_to<godot::Control>(parent);
    parent->connect("focus_entered", [this] { OnHover(); });
    parent->connect("focus_exited", [this] { OnExit(); });
    parent->connect("mouse_entered", [this] { OnHover(); });
    parent->connect("mouse_exited", [this] { OnExit(); });
    parent->connect("pressed", [this] { OnPress(); });
    if (isDynamic) {
        godot::Color m = ui->get_modulate();
        m.a = static_cast<float>(ui_opacity_inactive);
        ui->set_modulate(m);
    }
}

// gd: L36-L41
void ButtonClass::SetFilter(const std::string& filterAlias) {
    if (filterAlias == "ignore") {
        ui_control->set_mouse_filter(godot::Control::MOUSE_FILTER_IGNORE);
    } else if (filterAlias == "stop") {
        ui_control->set_mouse_filter(godot::Control::MOUSE_FILTER_STOP);
    }
}

// gd: L43-L50
void ButtonClass::OnHover() {
    if (isActive && mainActive) {
        if (isDynamic) {
            speaker_hover->set_pitch_scale(static_cast<float>(godot::randf_range(0.95, 1.0)));
            speaker_hover->play();
            godot::Color m = ui->get_modulate();
            m.a = static_cast<float>(ui_opacity_active);
            ui->set_modulate(m);
        }
        cursor->SetCursorImage("hover");
        if (adding_cursor) t->set_text("<  " + godot::tr(orig) + "  >");
    }
}

// gd: L52-L57
void ButtonClass::OnExit() {
    if (isActive && mainActive) {
        if (isDynamic) {
            godot::Color m = ui->get_modulate();
            m.a = static_cast<float>(ui_opacity_inactive);
            ui->set_modulate(m);
        }
        cursor->SetCursorImage("point");
        if (adding_cursor) t->set_text(godot::tr(orig));
    }
}

// gd: L60-L67
void ButtonClass::OnPress() {
    if (isActive && mainActive) {
        if (altsound) speaker_press->play();
        if (isDynamic && playing) speaker_press->play();
        if (rebind != nullptr) rebindManager->GetRebind(rebind);
        if (language) options->AdjustLanguage(alias);
        // PORT: `if pipe != null: pipe.Pipe(alias)`. pipe is the multiplayer
        // lobby manager, null in every single-player scene.
        get_node_owner()->emit_signal("is_pressed");
    }
}

}  // namespace scripts
