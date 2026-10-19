#include "scripts/MenuManager.h"

#include "godot/control.h"
#include "godot/print.h"
#include "godot/scene_tree.h"
#include "scripts/ButtonClass.h"
#include "scripts/ControllerManager.h"
#include "scripts/CursorManager.h"
#include "scripts/GlobalVariables.h"
#include "scripts/OptionsManager.h"
#include "scripts/SaveFileManager.h"

namespace scripts {

void MenuManager::OnPressed(std::vector<ButtonClass*>& list, std::size_t index, std::function<void()> fn) {
    if (index >= list.size() || !list[index] || !list[index]->get_node_owner()) return;
    list[index]->get_node_owner()->connect("is_pressed", std::move(fn));
}

bool MenuManager::call(const std::string& method, const std::vector<godot::Variant>& /*args*/) {
    if (method == "ReturnToLastScreen") { ReturnToLastScreen(); return true; }
    return false;
}

// gd: L28-L60
void MenuManager::_ready() {
    Show("main");
    OnPressed(buttons, 0, [this] { get_tree()->spawn(Start()); });
    OnPressed(buttons, 1, [this] { SubOptions(); });
    OnPressed(buttons, 2, [this] { Credits(); });
    OnPressed(buttons, 3, [this] { get_tree()->spawn(Exit()); });
    OnPressed(buttons, 4, [this] { ReturnToLastScreen(); });
    OnPressed(buttons, 5, [this] { ReturnToLastScreen(); });
    OnPressed(buttons, 6, [this] { ReturnToLastScreen(); });
    OnPressed(buttons, 7, [this] { Options_AudioVideo(); });
    OnPressed(buttons, 8, [this] { Options_Language(); });
    OnPressed(buttons, 9, [this] { Options_Controller(); });
    OnPressed(buttons, 10, [this] { ReturnToLastScreen(); });
    OnPressed(buttons, 11, [this] { ReturnToLastScreen(); });
    OnPressed(buttons, 16, [this] { RebindControls(); });
    OnPressed(buttons, 17, [this] { ReturnToLastScreen(); });
    OnPressed(buttons, 18, [this] { ResetControls(); });
    OnPressed(buttons, 19, [this] { DiscordLink(); });
    // PORT: buttons[22] is StartMultiplayer in v2.2 (the Steam lobby).
    OnPressed(buttons, 22, [this] { get_tree()->spawn(ComingSoon(), get_node_owner()); });

    OnPressed(buttons_options, 0, [this] { IncreaseVol(); });
    OnPressed(buttons_options, 1, [this] { DecreaseVol(); });
    OnPressed(buttons_options, 2, [this] { SetFull(); });
    OnPressed(buttons_options, 3, [this] { SetWindowed(); });
    OnPressed(buttons_options, 4, [this] { ControllerEnable(); });
    OnPressed(buttons_options, 5, [this] { ControllerDisable(); });
    OnPressed(buttons_options, 6, [this] { ToggleColorblind(); });
    OnPressed(buttons_options, 7, [this] { ToggleMusic(); });
    OnPressed(buttons_options, 8, [this] { ToggleGreyscaleDeath(); });

    version->set_text(GlobalVariables::currentVersion);

    Intro();
}

// gd: L62-L63
void MenuManager::_process(double /*delta*/) { T(); }

// gd: L78-L85
void MenuManager::_input(const godot::InputEventRef& event) {
    if (!event) return;
    if (event->is_action_pressed("ui_cancel") && failsafed) {
        if (currentScreen != "main") ReturnToLastScreen();
    }
    if (event->is_action_pressed("exit game") && failsafed) {
        if (currentScreen != "main") ReturnToLastScreen();
    }
    if (event->is_action_pressed("exit game") && !fs2 && viewing_intro) {
        SkipIntro();
        fs2 = true;
    }
}

// gd: L87-L95
void MenuManager::Intro() {
    cursor->SetCursor(false, false);
    if (GlobalVariables::lobby_id_found_in_command_line != 0 && !GlobalVariables::command_line_checked) {
        godot::print("lobby id found in command line. dont run the menu intro");
        return;
    }
    viewing_intro = true;
    speaker_music->play();
    animator_intro->play("splash screen");
    c = true;
}

// gd: L97-L101
void MenuManager::SkipIntro() {
    c = false;
    animator_intro->stop();
    animator_intro->play("skip intro");
    FinishIntro();
}

// gd: L103-L111
void MenuManager::FinishIntro() {
    viewing_intro = false;
    mouseblocker->set_visible(false);
    cursor->SetCursor(true, false);
    controller->settingFilter = true;
    controller->SetMainControllerState(controller->controller_currently_enabled);
    if (cursor->controller_active) firstFocus_main->grab_focus();
    controller->previousFocus = firstFocus_main;
    assigningFocus = true;
}

// gd: L116-L120
void MenuManager::T() {
    if (c) t += get_process_delta_time();
    if (t > 9 && !fs) {
        FinishIntro();
        fs = true;
    }
}

// gd: L122-L130
void MenuManager::Buttons(bool state) {
    if (!state) {
        for (ButtonClass* i : buttons) {
            i->isActive = false;
            i->SetFilter("ignore");
        }
    } else {
        for (ButtonClass* i : buttons) {
            i->isActive = true;
            i->SetFilter("stop");
        }
    }
}

// gd: L143-L176
void MenuManager::Show(std::string what) {
    lastScreen = currentScreen;
    currentScreen = what;
    godot::Control* focus = nullptr;
    title->set_visible(false);
    for (godot::Control* screen : screens) screen->set_visible(false);
    if (what == "main" || what == "sub options") title->set_visible(true);
    if (what == "main") {
        parent_main->set_visible(true);
        focus = firstFocus_main;
    } else if (what == "sub options") {
        parent_suboptions->set_visible(true);
        focus = firstFocus_subOptions;
    } else if (what == "credits") {
        parent_creds->set_visible(true);
        focus = firstFocus_credits;
        anim_creds->play("RESET");
        anim_creds->play("show credits");
    } else if (what == "audio video") {
        parent_audiovideo->set_visible(true);
        focus = firstFocus_audioVideo;
    } else if (what == "language") {
        parent_language->set_visible(true);
        focus = firstFocus_language;
    } else if (what == "controller") {
        parent_controller->set_visible(true);
        focus = firstFocus_controller;
    } else if (what == "rebind controls") {
        parent_rebinding->set_visible(true);
        focus = firstFocus_rebinding;
    }
    if (assigningFocus) {
        // PORT: `&& focus`. gd calls grab_focus on null for a screen with no
        // first focus (a script error there, a crash here).
        if (cursor->controller_active && focus) focus->grab_focus();
        controller->previousFocus = focus;
    }
}

// gd: L178-L185
void MenuManager::ReturnToLastScreen() {
    godot::print("return to last screen");
    if (currentScreen == "credits") anim_creds->play("RESET");
    if (currentScreen == "sub options") lastScreen = "main";
    if (currentScreen == "rebind controls") lastScreen = "sub options";
    if (currentScreen == "audio video" || currentScreen == "language" || currentScreen == "controller" ||
        currentScreen == "rebind controls") {
        optionmanager->SaveSettings();
    }
    Show(lastScreen);
    ResetButtons();
}

// gd: L187-L190
void MenuManager::ResetButtons() {
    cursor->SetCursorImage("point");
}

// PORT: the MULTIPLAYER caption reads COMING SOON for a moment, then goes back.
seq::Task MenuManager::ComingSoon() {
    auto* caption = buttons.size() > 22 && buttons[22] ? godot::cast_to<godot::Label>(buttons[22]->ui) : nullptr;
    if (!caption) co_return;
    if (caption->get_text() != kComingSoon) comingSoonCaption = caption->get_text();
    const int press = ++comingSoonPress;
    caption->set_text(kComingSoon);
    godot::print("menu: multiplayer pressed -> coming soon");
    co_await get_tree()->create_timer(kComingSoonSeconds);
    if (press == comingSoonPress) caption->set_text(comingSoonCaption);
}

// gd: L192-L206
seq::Task MenuManager::Start() {
    Buttons(false);
    ResetButtons();
    for (godot::Control* screen : screens) screen->set_visible(false);
    title->set_visible(false);
    controller->previousFocus = nullptr;
    speaker_music->stop();
    animator_intro->play("snap");
    for (godot::AnimationPlayer* w : waterfalls) w->pause();
    speaker_start->play();
    cursor->SetCursor(false, false);
    savefile->ClearSave();
    co_await get_tree()->create_timer(4);
    godot::print("changing scene to: main");
    get_tree()->change_scene_to_file("res://scenes/main.tscn");
}

// gd: L229-L231
void MenuManager::Credits() {
    Show("credits");
    ResetButtons();
}

// gd: L233-L240
seq::Task MenuManager::Exit() {
    Buttons(false);
    ResetButtons();
    speaker_music->stop();
    animator_intro->play("snap2");
    cursor->SetCursor(false, false);
    co_await get_tree()->create_timer(.5);
    get_tree()->quit();
}

// gd: L242-L247
void MenuManager::DisableMenu() {
    Buttons(false);
    ResetButtons();
    speaker_music->stop();
    animator_intro->play("snap2");
    cursor->SetCursor(false, false);
}

// gd: L249-L251
void MenuManager::ResetControls() {
    optionmanager->ResetControls();
    ResetButtons();
}

// gd: L253-L254
void MenuManager::ToggleMusic() { optionmanager->AdjustSettings_music(); }
// gd: L255-L256
void MenuManager::ToggleColorblind() { optionmanager->ToggleColorblind(); }
// gd: L257-L258
void MenuManager::ToggleGreyscaleDeath() { optionmanager->ToggleGreyscaleDeath(); }

// gd: L259-L260
void MenuManager::DiscordLink() {
    // PORT: OS.shell_open(discord_link): no browser on the Vita, and the
    // port's menu hides the Discord corner (PortBranding). Logged only.
    godot::print("discord link: ", GlobalVariables::discord_link);
}

// gd: L261-L263
void MenuManager::RebindControls() {
    Show("rebind controls");
    ResetButtons();
}

// gd: L264-L266
void MenuManager::SubOptions() {
    Show("sub options");
    ResetButtons();
}

// gd: L267-L269
void MenuManager::Options_AudioVideo() {
    Show("audio video");
    ResetButtons();
}

// gd: L270-L272
void MenuManager::Options_Language() {
    Show("language");
    ResetButtons();
}

// gd: L273-L276
void MenuManager::Options_Controller() {
    Show("controller");
    ResetButtons();
}

// gd: L277-L278
void MenuManager::IncreaseVol() { optionmanager->Adjust("increase"); }
// gd: L279-L280
void MenuManager::DecreaseVol() { optionmanager->Adjust("decrease"); }
// gd: L281-L282
void MenuManager::SetWindowed() { optionmanager->Adjust("windowed"); }
// gd: L283-L284
void MenuManager::SetFull() { optionmanager->Adjust("fullscreen"); }
// gd: L285-L286
void MenuManager::ControllerEnable() { optionmanager->Adjust("controller enable"); }
// gd: L287-L288
void MenuManager::ControllerDisable() { optionmanager->Adjust("controller disable"); }

}  // namespace scripts
