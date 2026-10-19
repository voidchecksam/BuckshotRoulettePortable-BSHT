#include "app/scene_host.h"

#include "core/json_lite.h"

#include <cstdio>
#include <memory>

#include "app/achievements_screen.h"
#include "game/save_store.h"
#include "godot/control.h"
#include "godot/scene_tree.h"
#include "scripts/ButtonClass.h"
#include "scripts/FilterController.h"
#include "scripts/MenuManager.h"
#include "scripts/MusicManager.h"
#include "scripts/OptionsManager.h"

namespace app {
namespace {

std::string Join(const std::string& a, const std::string& b) {
    return a.empty() ? b : a + "/" + b;
}

}  // namespace

SceneFiles FilesForScene(const std::string& resPath, const std::string& dataRoot,
                         const std::string& roomRoot, const std::string& portRoot) {
    SceneFiles f;
    if (resPath == "res://scenes/main.tscn" || resPath == "res://scenes/death.tscn") {
        f.name = "main";
        f.room = Join(roomRoot, "full_world.room");
        f.runtime = Join(dataRoot, "runtime_main.json");
        f.animations = Join(dataRoot, "animations.json");
        f.ui = Join(dataRoot, "ui_layout.json");
        f.uiPixels = Join(dataRoot, "ui_textures.bin");
        f.labels = Join(dataRoot, "labels3d.json");
    } else if (resPath == "res://scenes/menu.tscn") {
        f.name = "menu";
        f.room = Join(roomRoot, "menu.room");
        f.runtime = Join(dataRoot, "runtime_menu.json");
        f.animations = Join(dataRoot, "animations_menu.json");
        f.ui = Join(dataRoot, "ui_layout_menu.json");
        f.uiPixels = Join(dataRoot, "ui_textures_menu.bin");
        f.logo = Join(portRoot, "menu_logo.rgba");   // tools/bake_menu_logo.py
    } else if (resPath == "res://scenes/heaven.tscn") {
        // A true death (DeathManager.gd:154). No Label3D.
        f.name = "heaven";
        f.room = Join(roomRoot, "heaven.room");
        f.runtime = Join(dataRoot, "runtime_heaven.json");
        f.animations = Join(dataRoot, "animations_heaven.json");
        f.ui = Join(dataRoot, "ui_layout_heaven.json");
        f.uiPixels = Join(dataRoot, "ui_textures_heaven.bin");
    }
    return f;
}

bool HideRebindButton(godot::SceneTree& tree) {
    auto* menu = godot::script_cast<scripts::MenuManager>(tree.get_node("standalone managers/menu manager"));
    if (!menu || menu->buttons.size() <= 16 || !menu->buttons[16]) return false;
    godot::Node* owner = menu->buttons[16]->get_node_owner();
    auto* button = owner ? godot::cast_to<godot::Control>(owner->get_parent()) : nullptr;
    if (!button) return false;
    button->set_visible(false);
    // The Button is transparent; the text the player reads is its ButtonClass
    // `ui`, a sibling Label ("REBINDING"). Hide it too.
    if (menu->buttons[16]->ui) menu->buttons[16]->ui->set_visible(false);
    return true;
}

bool HideDesktopOnlyOptions(godot::SceneTree& tree) {
    const std::string ui = "Camera/dialogue UI/menu ui/";
    static const char* const kHidden[] = {
        "sub options select/button_controller",          "sub options select/true button_controller",
        "options_audio video/true button_windowed",       "options_audio video/true button_fullscreen",
        "options_audio video/option monitor_windowed",    "options_audio video/option monitor_fullscreen",
        "options_audio video/bracket selection_windowed", "options_audio video/bracket selection_fullscreen",
    };
    bool all = true;
    for (const char* path : kHidden) {
        auto* c = tree.get_node_as<godot::Control>(ui + path);
        if (c) c->set_visible(false);
        all = all && c != nullptr;
    }
    // RETURN up into the CONTROLLER slot: the list keeps its spacing.
    auto* controllerLabel = tree.get_node_as<godot::Control>(ui + "sub options select/button_controller");
    auto* returnLabel = tree.get_node_as<godot::Control>(ui + "sub options select/button_return");
    auto* returnButton = tree.get_node_as<godot::Control>(ui + "sub options select/true button_exit sub options");
    if (controllerLabel && returnLabel && returnButton) {
        const float up = returnLabel->get_position().y - controllerLabel->get_position().y;
        for (godot::Control* c : {returnLabel, returnButton}) {
            const godot::Vector2 p = c->get_position();
            c->set_position({p.x, p.y - up});
        }
    } else {
        all = false;
    }
    return HideRebindButton(tree) && all;
}

ScreenRect ScreenRectOf(const godot::Control& c) {
    const godot::Transform2D& t = c.get_global_transform();
    const godot::Vector2 size = t.basis_xform(c.get_size());
    return {t.ox, t.oy, size.x, size.y};
}

ScreenRect AchievementsEntryTextBox(const godot::SceneTree& tree) {
    const auto* exit = tree.get_node_as<godot::Control>("Camera/dialogue UI/menu ui/main screen/button_exit");
    if (!exit) return {};
    const auto* credits = tree.get_node_as<godot::Control>("Camera/dialogue UI/menu ui/main screen/button_credits");
    ScreenRect r = ScreenRectOf(*exit);
    r.y += credits ? r.y - ScreenRectOf(*credits).y : r.h;
    return r;
}

godot::Button* AddAchievementsEntry(godot::SceneTree& tree) {
    auto* exitButton = tree.get_node_as<godot::Control>("Camera/dialogue UI/menu ui/main screen/true button_exit");
    if (!exitButton || !exitButton->get_parent()) return nullptr;
    const std::string startLink = exitButton->get_focus_neighbor(godot::SIDE_BOTTOM);
    const std::string dir = startLink.substr(0, startLink.rfind('/') + 1);
    auto* entry = godot::cast_to<godot::Button>(
        tree.add_node(std::make_unique<godot::Button>("true button_achievements"), exitButton->get_parent()));
    if (!entry) return nullptr;
    // One row below EXIT: the CREDITS -> EXIT spacing (EXIT's height without CREDITS).
    const ScreenRect exitRect = ScreenRectOf(*exitButton);
    const auto* credits = tree.get_node_as<godot::Control>("Camera/dialogue UI/menu ui/main screen/true button_credits");
    const float row = credits ? exitRect.y - ScreenRectOf(*credits).y : exitRect.h;
    const float x = exitRect.x + exitRect.w * 0.5f - AchievementsScreen::kEntryW * 0.5f;
    entry->set_global_rect({x, exitRect.y + row}, {AchievementsScreen::kEntryW, AchievementsScreen::kEntryH});
    entry->set_focus_neighbor(godot::SIDE_TOP, dir + "true button_exit");
    entry->set_focus_neighbor(godot::SIDE_BOTTOM, startLink);
    exitButton->set_focus_neighbor(godot::SIDE_BOTTOM, dir + "true button_achievements");
    // START's up link pointed at EXIT; it goes to the new last row instead.
    if (auto* start = tree.get_node_as<godot::Control>("Camera/dialogue UI/menu ui/main screen/true button_start")) {
        start->set_focus_neighbor(godot::SIDE_TOP, dir + "true button_achievements");
    }
    return entry;
}

bool SceneFilesPresent(const SceneFiles& f) {
    for (const std::string* path : {&f.room, &f.runtime, &f.animations, &f.ui, &f.uiPixels, &f.labels}) {
        if (path == &f.labels && path->empty()) continue;
        std::FILE* file = std::fopen(path->c_str(), "rb");
        if (!file) return false;
        std::fclose(file);
    }
    return true;
}

bool PlaysIntro(bool skipMenu, bool scenario) {
    return !skipMenu && !scenario;
}

bool UsesSaveStore(bool skipMenu, bool scenario) {
    return !skipMenu && !scenario;
}

bool MenuMusicAtBoot(const std::string& saveDir, bool skipMenu, bool scenario) {
    if (!UsesSaveStore(skipMenu, scenario)) return true;
    const game::SaveStore store(saveDir);
    return scripts::OptionsManager::SavedMusicEnabled(&store);
}

const char* BootScene(bool skipMenu, const std::string& scenarioJson) {
    if (!scenarioJson.empty() &&
        core::json::ReadStringTopLevel(scenarioJson.data(), scenarioJson.data() + scenarioJson.size(), "scene") ==
            "heaven") {
        return "res://scenes/heaven.tscn";
    }
    return skipMenu ? "res://scenes/main.tscn" : "res://scenes/menu.tscn";
}

SceneMusic FindSceneMusic(godot::SceneTree& tree) {
    SceneMusic m;
    if (auto* music = godot::script_cast<scripts::MusicManager>(tree.get_node("standalone managers/music manager"))) {
        m.player = music->speaker_music;
        // FilterController owns the bus's cutoff; MusicManager points at it.
        if (music->filter) m.cutoffHz = &music->filter->effect_lowPass_cutoff_hz;
        m.owner = "MusicManager";
    } else if (auto* menu = godot::script_cast<scripts::MenuManager>(
                   tree.get_node("standalone managers/menu manager"))) {
        m.player = menu->speaker_music;
        m.owner = "MenuManager";
    }
    return m;
}

}  // namespace app
