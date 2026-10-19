#include "scripts/CrtManager.h"

#include "godot/input_event.h"
#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "scripts/CrtButtonBranch.h"
#include "scripts/CrtIcon.h"
#include "scripts/GlobalVariables.h"
#include "scripts/InteractionBranch.h"
#include "scripts/IntroManager.h"
#include "scripts/LeaderboardManager.h"
#include "scripts/PartitionBranch.h"
#include "scripts/UserExit.h"

namespace scripts {

namespace {

// `node.material_override.albedo_color = c` on a node the .gd types Node3D:
// GDScript reaches the GeometryInstance3D property dynamically.
void OverrideAlbedo(godot::Node3D* node, const godot::Color& c) {
    if (auto* g = godot::cast_to<godot::GeometryInstance3D>(node)) g->set_override_albedo(c);
}

// `branch.get_parent().get_child(1).Press()`: the CrtButton beside the branch.
seq::Task PressButtonOf(InteractionBranch* branch) {
    auto* button = godot::script_cast<CrtButton>(branch->get_node_owner()->get_parent()->get_child(1));
    if (button) co_await button->Press();
}

}  // namespace

// gd: L38-L40
void CRT::_ready() {
    screenparent_stats->set_visible(true);
    screenparent_leaderboard->set_visible(false);
}

// gd: L42-L52. Interaction is called without await: spawned.
void CRT::_unhandled_input(const godot::InputEventRef& event) {
    godot::SceneTree* tree = get_tree();
    if (event->is_action_pressed("ui_accept") && viewing) tree->spawn(Interaction("window"), get_node_owner());
    if (event->is_action_pressed("ui_cancel") && viewing) tree->spawn(Interaction("exit"), get_node_owner());
    if (event->is_action_pressed("exit game") && viewing) tree->spawn(Interaction("exit"), get_node_owner());
    if (event->is_action_pressed("ui_left") && viewing) tree->spawn(Interaction("left"), get_node_owner());
    if (event->is_action_pressed("ui_right") && viewing) tree->spawn(Interaction("right"), get_node_owner());
}

// gd: L54-L55
void CRT::DisableCRT() {
    SetCRT(false);
}

// gd: L57-L69
void CRT::SetCRT(bool state) {
    if (state && GlobalVariables::using_steam) {
        bathroom_normal->set_layer_mask_value(1, false);
        bathroom_broken->set_visible(true);
        for (godot::Node3D* obj : objarray_normal) obj->set_visible(false);
        for (godot::Node3D* obj : objarray_broken) obj->set_visible(true);
        mask->set_visible(true);
    } else {
        bathroom_normal->set_layer_mask_value(1, true);
        bathroom_broken->set_visible(false);
        for (godot::Node3D* obj : objarray_normal) obj->set_visible(true);
        for (godot::Node3D* obj : objarray_broken) obj->set_visible(false);
        mask->set_visible(false);
    }
}

// gd: L77-L104. The buttons' Press() is called without await: spawned.
seq::Task CRT::Interaction(std::string alias) {
    godot::SceneTree* tree = get_tree();
    speaker_buttonpress->set_pitch_scale(static_cast<float>(godot::randf_range(.8, 1)));
    speaker_buttonpress->play();
    if (alias == "right") {
        tree->spawn(PressButtonOf(branch_right), get_node_owner());
        if (selection_range2 <= board->active_entry_count && window_index == 0) {
            selection_range1 += 12;
            selection_range2 += 12;
            if (window_index == 0) board->DownloadEntries(selection_range1, selection_range2, "top");
        }
    } else if (alias == "left") {
        tree->spawn(PressButtonOf(branch_left), get_node_owner());
        if (selection_range1 != 1 && window_index == 0) {
            selection_range1 -= 12;
            selection_range2 -= 12;
            if (window_index == 0) board->DownloadEntries(selection_range1, selection_range2, "top");
        }
    } else if (alias == "window") {
        tree->spawn(PressButtonOf(branch_window), get_node_owner());
        CycleWindow();
    } else if (alias == "exit") {
        has_exited = true;
        tree->spawn(PressButtonOf(branch_exit), get_node_owner());
        viewing = false;
        board->TurnOffDisplay();
        intro->DisableInteractionCrt();
        co_await tree->create_timer(.3);
        // gd: intro.RevertCRT(), not awaited: spawned, owned by the intro manager.
        tree->spawn(intro->RevertCRT(), intro->get_node_owner());
        exit->exitAllowed = true;
    }
}

// gd: L106-L126
void CRT::CycleWindow() {
    OverrideAlbedo(board->lock, godot::Color{1, 1, 1, 0});
    selection_range1 = 1;
    selection_range2 = 12;
    board->ClearDisplay();
    window_index += 1;
    if (window_index == 4) window_index = 0;
    for (CrtIcon* icon : iconbranches) icon->CheckState(window_index);
    if (window_index == 3) {
        board->nocon->set_visible(false);
        screenparent_leaderboard->set_visible(false);
        screenparent_stats->set_visible(true);
    } else {
        screenparent_leaderboard->set_visible(true);
        screenparent_stats->set_visible(false);
        board->nocon->set_visible(true);
    }
    if (window_index == 0) board->PassLeaderboard(selection_range1, selection_range2, "top");
    if (window_index == 1) {
        board->lock->set_visible(true);
        board->PassLeaderboard(selection_range1, selection_range2, "overview");
    }
    if (window_index == 2) board->PassLeaderboard(1, 49, "friends");   // gd: #range ignored
}

// gd: L128-L171
seq::Task CRT::Bootup() {
    has_exited = false;
    OverrideAlbedo(board->lock, godot::Color{1, 1, 1, 0});
    screenparent_stats->set_visible(true);
    board->UpdateStats();
    window_index = 3;
    for (CrtIcon* icon : iconbranches) icon->CheckState(window_index);
    for (godot::Node3D* line : array_bootup) {
        line->set_visible(true);
        co_await get_tree()->create_timer(.07);
        speaker_navbeep->set_pitch_scale(static_cast<float>(godot::randf_range(1, 1)));
        speaker_navbeep->play();
    }
    co_await get_tree()->create_timer(.1);
    for (PartitionBranch* part : array_partbranch) {
        part->Loop(true);
        co_await get_tree()->create_timer(.04);
        speaker_navbeep->set_pitch_scale(static_cast<float>(godot::randf_range(.1, .1)));
        speaker_navbeep->play();
    }
    co_await get_tree()->create_timer(1);
    for (PartitionBranch* part : array_partbranch) part->Loop(false);
    co_await get_tree()->create_timer(1);
    for (godot::Node3D* line : array_bootup) line->set_visible(false);
    co_await get_tree()->create_timer(.2);
    speaker_melody->set_pitch_scale(2);
    speaker_melody->play();
    for (godot::Node3D* line : array_bootuplogo) {
        line->set_visible(true);
        co_await get_tree()->create_timer(.07);
    }
    co_await get_tree()->create_timer(2);
    for (godot::Node3D* line : array_bootuplogo) line->set_visible(false);
    speaker_melody->stop();
    speaker_melodyhide->play();
    co_await get_tree()->create_timer(.5);
    anim_iconfade->play("fade in");
    co_await get_tree()->create_timer(.5);
    for (godot::Node3D* i : array_stats) {
        i->set_visible(true);
        co_await get_tree()->create_timer(.07);
        speaker_navbeep->set_pitch_scale(static_cast<float>(godot::randf_range(.5, .5)));
        speaker_navbeep->play();
    }
    co_await get_tree()->create_timer(.3);
    intro->EnabledInteractionCRT();
    exit->exitAllowed = false;
    viewing = true;
}

}  // namespace scripts
