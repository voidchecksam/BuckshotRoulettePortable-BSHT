#include "scripts/EndingManager.h"

#include "scripts/LeaderboardManager.h"

#include "game/save_store.h"
#include "godot/node.h"
#include "godot/print.h"
#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"
#include "scripts/AchievementManager.h"
#include "scripts/CameraManager.h"
#include "scripts/DialogueManager_Ending.h"
#include "scripts/DonUnlockManager.h"
#include "scripts/GlobalVariables.h"
#include "scripts/RoundManager.h"
#include "scripts/SaveFileManager.h"
#include "scripts/SpeakerController.h"
#include "scripts/TextFire.h"
#include "scripts/UserExit.h"
#include "scripts/resources.h"

#include <cstdio>
#include <variant>

namespace scripts {

namespace {

// str(float): Variant's rtos, 14 significant digits ("65570", "69998.5").
std::string Rtos(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.14g", v);
    return buf;
}

std::int64_t Int(const game::SaveMap& m, const char* key) {
    const auto it = m.find(key);
    if (it == m.end()) return 0;
    if (const auto* i = std::get_if<std::int64_t>(&it->second)) return *i;
    if (const auto* d = std::get_if<double>(&it->second)) return static_cast<std::int64_t>(*d);
    return 0;
}

double Real(const game::SaveMap& m, const char* key) {
    const auto it = m.find(key);
    if (it == m.end()) return 0;
    if (const auto* d = std::get_if<double>(&it->second)) return *d;
    if (const auto* i = std::get_if<std::int64_t>(&it->second)) return static_cast<double>(*i);
    return 0;
}

}  // namespace

// gd: L39-L43
void EndingManager::_unhandled_input(const godot::InputEventRef& event) {
    if (event->is_pressed()) AnyKey();
}

void EndingManager::AnyKey() {
    if (waitingForInput) {
        get_tree()->spawn(ExitGame(), get_node_owner());
        waitingForInput = false;
    }
}

// gd: L45-L87, called without await (BriefcaseMachine.OpenLid).
void EndingManager::BeginEnding() {
    get_tree()->spawn(BeginEndingRoutine(), get_node_owner());
}

seq::Task EndingManager::BeginEndingRoutine() {
    exitm->exitAllowed = false;
    cam->moving = false;
    animator_pan->play("pan to brief");
    music_ending->play();
    get_tree()->spawn(AmbienceFade(), get_node_owner());
    co_await get_tree()->create_timer(10.67);
    viewblocker->set_visible(true);
    animator_viewblocker->play("snap");
    animator_pan->stop();
    get_tree()->spawn(SetupEnding(), get_node_owner());
    FinalScore();
    for (godot::Label* l : label_array) l->set_visible(false);
    label_congrats->set_visible(false);
    co_await get_tree()->create_timer(1.2);
    animator_viewblocker->play("fade out");
    co_await get_tree()->create_timer(3);
    ach->UnlockAchievement("ach1");
    if (roundManager->endless) {
        if (!roundManager->doubled) ach->UnlockAchievement("ach5");
        if (roundManager->doubled) ach->UnlockAchievement("ach6");
        if (roundManager->endscore > 1000000) ach->UnlockAchievement("ach7");
    }
    co_await get_tree()->create_timer(3);
    // SHOW UI
    label_congrats->set_visible(true);
    animator_congrats->play("wobble it");
    animator_cash->play("wobble it");
    dia->ShowText_Forever(glob_text_congratulations);
    co_await get_tree()->create_timer(2);
    for (std::size_t i = 0; i < fireArray.size(); ++i) {
        speaker_load->set_pitch_scale(static_cast<float>(godot::randf_range(.95, 1)));
        speaker_load->play();
        co_await get_tree()->create_timer(.15);
    }
    co_await get_tree()->create_timer(.5);
    for (std::size_t i = 0; i < fireArray.size(); ++i) {
        if (i == 5) {
            co_await get_tree()->create_timer(.4);
            fireArray[i]->changed = true;
            fireArray[i]->speaker->set_pitch_scale(.15f);
        }
        fireArray[i]->Fire();
        co_await get_tree()->create_timer(.33);
    }
    co_await get_tree()->create_timer(3);
    get_tree()->spawn(GetInput(), get_node_owner());
}

// gd: L89-L92
seq::Task EndingManager::GetInput() {
    animator_anykey->play("flicker");
    co_await get_tree()->create_timer(.8);
    waitingForInput = true;
}

// gd: L94-L107
seq::Task EndingManager::ExitGame() {
    animator_viewblocker->play("fade in");
    cntrl_endingmusic->FadeOut();
    cntrl_ambience->fadeDuration = 3;
    cntrl_ambience->FadeOut();
    isActive = false;
    co_await get_tree()->create_timer(4);
    const bool unlocked = unlocker->Unlocked();   // gd: FileAccess.file_exists(unlocker.savepath)
    if (unlocked) {
        godot::print("changing scene to: menu");
        get_tree()->change_scene_to_file("res://scenes/menu.tscn");
        co_return;
    }
    get_tree()->spawn(unlocker->UnlockRoutine(), unlocker->get_node_owner());
}

// gd: L110-L115
seq::Task EndingManager::AmbienceFade() {
    co_await get_tree()->create_timer(55);
    if (isActive) {
        cntrl_ambience->SnapVolume(false);
        speaker_ambience->play();
        cntrl_ambience->FadeIn();
    }
}

// gd: L117-L128
seq::Task EndingManager::SetupEnding() {
    co_await get_tree()->create_timer(.1);
    uiparent->set_visible(true);
    for (godot::Node3D* b : blockout) b->set_visible(true);
    pp->set_adjustment_brightness(5.68f);
    pp->set_adjustment_contrast(1.3f);
    pp->set_adjustment_saturation(.26f);
    cam->moving = false;
    cam->cam->set_fov(60);
    animator_streetlights->play("looping");
    animator_backdrop->play("loop exterior");
    animator_cam->play("camera loop car");
}

// gd: L156-L161
// gd: label_array[i].get_child(2).text = ..., the line's `stat` Label.
void EndingManager::SetStat(int line, const std::string& text) {
    auto* stat = godot::cast_to<godot::Label>(label_array[static_cast<std::size_t>(line)]->get_child(2));
    if (stat) stat->set_text(text);
}

// gd: L135-L175
void EndingManager::FinalScore() {
    const std::string playername = roundManager->playerData->playername;
    const int shots_fired = roundManager->playerData->stat_shotsFired;
    const int shells_ejected = roundManager->playerData->stat_shellsEjected;
    const int doors_kicked = roundManager->playerData->stat_doorsKicked;
    const int cigarettes_smoked = roundManager->playerData->stat_cigSmoked;
    const int ml_of_beer_drank = roundManager->playerData->stat_beerDrank;
    const int max_cash = 70000;

    const int cancer_bills = cigarettes_smoked * 220;
    const int new_leg_bones_price = doors_kicked * 1000;
    const double DUI_fine = ml_of_beer_drank * 1.5;
    double total_cash = max_cash - cancer_bills - DUI_fine;
    if (doors_kicked > 2) total_cash -= new_leg_bones_price;
    if (total_cash < 0) total_cash = 0;

    if (endless_overwriting) total_cash = endless_score;
    cash_gl = total_cash;

    const std::string dots(64, '.');
    const std::string text_congratulations = godot::format(godot::tr("CONGRATULATIONS"), {playername});
    std::string text_shotsFired = godot::tr("SHOTS FIRED") + " " + dots;
    SetStat(0, std::to_string(shots_fired));
    if (endless_overwriting) {
        text_shotsFired = godot::tr("ROUNDS BEAT") + " " + dots;
        SetStat(0, std::to_string(endless_roundsbeat));
    }
    const std::string text_shellsEjected = godot::tr("SHELLS EJECTED") + " " + dots;
    SetStat(1, std::to_string(shells_ejected));
    const std::string text_doorsKicked = godot::tr("DOORS KICKED") + " " + dots;
    SetStat(2, std::to_string(doors_kicked));
    const std::string text_cigSmoked = godot::tr("CIGS SMOKED") + " " + dots;
    SetStat(3, std::to_string(cigarettes_smoked));
    const std::string text_beerDrank = godot::tr("ML DRANK") + " " + dots;
    SetStat(4, std::to_string(ml_of_beer_drank));
    const std::string text_totalcash = godot::tr("TOTAL CASH") + " " + Rtos(total_cash) + " $";

    glob_text_congratulations = text_congratulations;

    label_array[0]->set_text(text_shotsFired);
    label_array[1]->set_text(text_shellsEjected);
    label_array[2]->set_text(text_doorsKicked);
    label_array[3]->set_text(text_cigSmoked);
    label_array[4]->set_text(text_beerDrank);
    label_array[5]->set_text(text_totalcash);
    IncrementGlobalStats();
    if (endless_overwriting && GlobalVariables::using_steam) {
        board->UploadScore(roundManager->double_or_nothing_rounds_beat, roundManager->double_or_nothing_score,
                           roundManager->double_or_nothing_initial_score);
    }
}

// gd: L194-L203
void EndingManager::IncrementGlobalStats() {
    LoadPlayerData();
    statSAVE_shots_fired = statPREV_shots_fired + roundManager->playerData->stat_shotsFired;
    statSAVE_shells_ejected = statPREV_shells_ejected + roundManager->playerData->stat_shellsEjected;
    statSAVE_doors_kicked = statPREV_doors_kicked + roundManager->playerData->stat_doorsKicked;
    // gd: L199 adds the previous DOORS, not cigarettes: the original's own bug, kept.
    statSAVE_cigs_smoked = statPREV_doors_kicked + roundManager->playerData->stat_cigSmoked;
    statSAVE_rounds_beat = statPREV_rounds_beat + endless_roundsbeat;
    statSAVE_ml_drank = statPREV_ml_drank + roundManager->playerData->stat_beerDrank;
    statSAVE_total_cash = statPREV_total_cash + cash_gl;
    SavePlayerData();
}

// gd: L205-L217
void EndingManager::SavePlayerData() {
    if (!store) return;   // PORT: no store on a diagnostic boot
    const game::SaveMap playerdata{
        {"shots_fired", statSAVE_shots_fired},
        {"shells_ejected", statSAVE_shells_ejected},
        {"doors_kicked", statSAVE_doors_kicked},
        {"cigs_smoked", statSAVE_cigs_smoked},
        {"rounds_beat", statSAVE_rounds_beat},
        {"ml_drank", statSAVE_ml_drank},
        {"total_cash", statSAVE_total_cash},
    };
    store->Save(SaveFileManager::savePath_stats, playerdata);
}

// gd: L220-L232
void EndingManager::LoadPlayerData() {
    game::SaveMap temp_data;
    if (store && store->Load(SaveFileManager::savePath_stats, temp_data)) {
        statPREV_shots_fired = Int(temp_data, "shots_fired");
        statPREV_shells_ejected = Int(temp_data, "shells_ejected");
        statPREV_doors_kicked = Int(temp_data, "doors_kicked");
        statPREV_cigs_smoked = Int(temp_data, "cigs_smoked");
        statPREV_rounds_beat = Int(temp_data, "rounds_beat");
        statPREV_total_cash = Real(temp_data, "total_cash");
        statPREV_ml_drank = Int(temp_data, "ml_drank");
    }
}

}  // namespace scripts
