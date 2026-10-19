// source: scripts/MenuManager.gd
// PORT deviations:
//   - connect("is_pressed", Start) on a ButtonClass: the port connects on the
//     node the ButtonClass script is attached to, which is where OnPress emits.
//     A button index the scene lacks is skipped rather than crashing.
//   - DiscordLink: OS.shell_open has nothing to open on a Vita; a no-op
//     everywhere.
//   - get_tree().quit() is recorded on the SceneTree and performed by the host
//     after the frame, as change_scene_to_file is.
#pragma once

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <functional>
#include <string>
#include <vector>

namespace scripts {

class MenuManager : public godot::Script {
public:
    static constexpr const char* kClassName = "MenuManager";
    const char* get_class_name() const override { return kClassName; }

    // v2.2
    godot::Label* version = nullptr;

    SaveFileManager* savefile = nullptr;
    CursorManager* cursor = nullptr;
    godot::AnimationPlayer* animator_intro = nullptr;
    godot::AudioStreamPlayer* speaker_music = nullptr;
    godot::AudioStreamPlayer* speaker_start = nullptr;
    std::vector<ButtonClass*> buttons;
    std::vector<ButtonClass*> buttons_options;
    std::vector<godot::Control*> screens;
    godot::Control* parent_main = nullptr;
    godot::Control* parent_creds = nullptr;
    godot::Control* parent_suboptions = nullptr;
    godot::Control* parent_audiovideo = nullptr;
    godot::Control* parent_language = nullptr;
    godot::Control* parent_controller = nullptr;
    godot::Control* parent_rebinding = nullptr;
    godot::Node3D* title = nullptr;
    std::vector<godot::AnimationPlayer*> waterfalls;
    OptionsManager* optionmanager = nullptr;
    ControllerManager* controller = nullptr;
    godot::Control* mouseblocker = nullptr;
    godot::AnimationPlayer* anim_creds = nullptr;
    godot::Control* firstFocus_main = nullptr;
    godot::Control* firstFocus_subOptions = nullptr;
    godot::Control* firstFocus_credits = nullptr;
    godot::Control* firstFocus_audioVideo = nullptr;
    godot::Control* firstFocus_language = nullptr;
    godot::Control* firstFocus_controller = nullptr;
    godot::Control* firstFocus_rebinding = nullptr;

    bool failsafed = true;
    bool fs2 = false;
    bool viewing_intro = false;
    double t = 0;
    bool c = false;
    bool fs = false;
    bool assigningFocus = false;
    std::string lastScreen = "main";
    std::string currentScreen = "main";

    void _ready() override;
    bool has_process() const override { return true; }
    void _process(double delta) override;
    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;
    // PORT: omitted CheckCommandLine (Steam lobby invites; multiplayer is out)
    bool has_input() const override { return true; }
    void _input(const godot::InputEventRef& event) override;
    void Intro();
    void SkipIntro();
    void FinishIntro();
    void T();
    void Buttons(bool state);
    // By value: callers pass lastScreen, which the first line overwrites.
    void Show(std::string what);
    void ReturnToLastScreen();
    void ResetButtons();
    seq::Task Start();
    // PORT: omitted StartMultiplayer (it opens the Steam lobby; the port's
    // multiplayer comes later, so the entry says COMING SOON instead)
    seq::Task ComingSoon();
    static constexpr const char* kComingSoon = "COMING SOON";
    static constexpr double kComingSoonSeconds = 1.5;
    int comingSoonPress = 0;   // the latest press; an older timer leaves the caption alone
    std::string comingSoonCaption;   // the caption before COMING SOON replaced it
    void Credits();
    seq::Task Exit();
    void DisableMenu();
    void ResetControls();
    void ToggleMusic();
    void ToggleColorblind();
    void ToggleGreyscaleDeath();
    void DiscordLink();
    void RebindControls();
    void SubOptions();
    void Options_AudioVideo();
    void Options_Language();
    void Options_Controller();
    void IncreaseVol();
    void DecreaseVol();
    void SetWindowed();
    void SetFull();
    void ControllerEnable();
    void ControllerDisable();

private:
    void OnPressed(std::vector<ButtonClass*>& list, std::size_t index, std::function<void()> fn);
};

}  // namespace scripts
