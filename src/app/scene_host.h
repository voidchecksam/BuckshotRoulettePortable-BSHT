// One scene resident at a time. Godot's change_scene_to_file frees the current
// scene and instantiates the next; the port does the same with its data: every
// scene is a baked room plus its runtime tree, animation, interface and Label3D
// exports, named here. Headless.
#pragma once

#include <string>

namespace godot {
class AudioPlayback;
class Button;
class Control;
class SceneTree;
}

namespace app {

struct SceneFiles {
    std::string name;        // "main", "menu"; empty = not ported
    std::string room;        // the baked room
    std::string runtime;     // runtime_<scene>.json
    std::string animations;
    std::string ui;          // ui_layout json
    std::string uiPixels;    // its textures
    std::string labels;      // labels3d json; empty when the scene has none
    std::string logo;        // the port's title logo (raw RGBA); empty when the scene has none
};

// The files for a res:// scene path. `dataRoot` holds the exports, `roomRoot`
// the baked rooms, `portRoot` the port's own files (the menu logo). See
// app/data_layout.h for where each points.
// death.tscn is main: TempDeathScreen changes straight back to it. A scene
// the port does not have yet returns an empty set; the caller refuses the
// change and logs it.
SceneFiles FilesForScene(const std::string& resPath, const std::string& dataRoot,
                         const std::string& roomRoot, const std::string& portRoot);

// The scene a boot starts in: the menu, or main.tscn when a diagnostic wants
// the table (AppConfig::skipMenu). A scenario whose "scene" is "heaven" boots
// heaven.tscn.
const char* BootScene(bool skipMenu, const std::string& scenarioJson = std::string());

// Every file `f` names (labels only when it has any) can be opened. Checked
// before a scene change tears the old scene down.
bool SceneFilesPresent(const SceneFiles& f);

// Whether the scripts get the save directory (SaveFileManager, OptionsManager).
bool UsesSaveStore(bool skipMenu, bool scenario);
// Whether the loading screen may start the menu's track: not on a player's
// boot whose saved options have the music off.
bool MenuMusicAtBoot(const std::string& saveDir, bool skipMenu, bool scenario);

// Whether the restroom intro plays: only on a player's boot.
bool PlaysIntro(bool skipMenu, bool scenario);

// The Vita hides the controller-rebinding button: its pad layout is fixed.
// MenuManager.buttons[16]'s Button is hidden; Godot's _get_focus_neighbor then
// skips it, so no focus link leads to it. False when the tree has no menu
// (every scene but the menu).
bool HideRebindButton(godot::SceneTree& tree);
// The Vita hides every option that means nothing on it: the CONTROLLER
// sub-screen's entry (mouse or pad, rebinding) and WINDOWED / FULLSCREEN with
// their icons and brackets. RETURN moves up into the CONTROLLER slot, and the
// rebind button goes as above. Pad focus skips hidden controls
// (Control::_get_focus_neighbor). False if a node is missing.
bool HideDesktopOnlyOptions(godot::SceneTree& tree);

// A Control's rect on the 960x540 design screen (its global transform applied
// to its size; the menu's buttons are scaled).
struct ScreenRect {
    float x = 0, y = 0, w = 0, h = 0;
};
ScreenRect ScreenRectOf(const godot::Control& c);

// PORT: the one addition to the original's menu. A real Button named
// "true button_achievements" under the main screen, one menu row below EXIT
// (the CREDITS -> EXIT spacing), centred on it; EXIT -> it -> START in Godot's
// focus links. Null when the tree has no menu EXIT button.
godot::Button* AddAchievementsEntry(godot::SceneTree& tree);
// Where the ACHIEVEMENTS entry's text goes: EXIT's label box one row (the
// CREDITS -> EXIT step) further down. Empty when the menu has no EXIT label.
ScreenRect AchievementsEntryTextBox(const godot::SceneTree& tree);

// The scene's music player, which streams instead of going to the SFX bank
// (game/music_router.h): MusicManager's at the table, MenuManager's in the
// menu. `cutoffHz` is FilterController's low-pass when the scene has one;
// `owner` names the script for the log, empty when the scene has neither.
// GameApp binds it before _ready, because MenuManager plays in _ready.
struct SceneMusic {
    godot::AudioPlayback* player = nullptr;
    const double* cutoffHz = nullptr;
    std::string owner;
};
SceneMusic FindSceneMusic(godot::SceneTree& tree);

}  // namespace app
