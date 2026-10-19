// source: scripts/EndingManager.gd (254 lines)
// PORT deviations:
//   - `save.savePath_stats` is a const the original reads through a null
//     `save` (GDScript resolves it by the static type): the port reads
//     SaveFileManager::savePath_stats. FileAccess.store_var/get_var of the
//     stats Dictionary is game::SaveStore entry "playerstats", written only
//     when the host sets `store` (a player's boot, app::UsesSaveStore).
//   - total_cash is a double throughout (GDScript mixes int and float): its
//     text is rtos's %.14g, identical to str(int) for every whole value the
//     game reaches (< 1e14).
//   - board.UploadScore (endless only) goes to the local board (game::LocalBoard).
//   - change_scene_to_file / the unlock's are performed by the host.
#pragma once

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/node.h"
#include "godot/physics.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <string>

namespace game {
class SaveStore;
}
#include <cstdint>
#include <vector>

namespace scripts {

class EndingManager : public godot::Script {
public:
    static constexpr const char* kClassName = "EndingManager";
    const char* get_class_name() const override { return kClassName; }

    SaveFileManager* save = nullptr;
    RoundManager* roundManager = nullptr;
    godot::WorldEnvironment* pp = nullptr;
    godot::AnimationPlayer* animator_cam = nullptr;
    godot::AnimationPlayer* animator_backdrop = nullptr;
    CameraManager* cam = nullptr;
    std::vector<godot::Node3D*> blockout;
    godot::ColorRect* viewblocker = nullptr;
    godot::AnimationPlayer* animator_viewblocker = nullptr;
    godot::AnimationPlayer* animator_streetlights = nullptr;
    godot::AudioStreamPlayer* music_ending = nullptr;
    DialogueEnding* dia = nullptr;
    godot::Label* label_congrats = nullptr;
    std::vector<godot::Label*> label_array;
    godot::Control* uiparent = nullptr;
    godot::AnimationPlayer* animator_congrats = nullptr;
    godot::AnimationPlayer* animator_cash = nullptr;
    std::vector<TextFire*> fireArray;
    godot::AudioStreamPlayer* speaker_load = nullptr;
    godot::AudioStreamPlayer* speaker_ambience = nullptr;
    SpeakerController* cntrl_ambience = nullptr;
    SpeakerController* cntrl_endingmusic = nullptr;
    godot::AnimationPlayer* animator_anykey = nullptr;
    godot::AnimationPlayer* animator_pan = nullptr;
    ExitManager* exitm = nullptr;
    Achievement* ach = nullptr;
    Unlocker* unlocker = nullptr;
    Board* board = nullptr;

    game::SaveStore* store = nullptr;   // PORT: see the header
    bool waitingForInput = false;       // gd: L31
    bool isActive = true;               // gd: L109

    // gd: L130-L134
    bool endless_overwriting = false;
    int endless_roundsbeat = 0;
    int64_t endless_score = 0;
    std::string glob_text_congratulations;
    double cash_gl = 0;

    // gd: L179-L193
    std::int64_t statPREV_shots_fired = 0;
    std::int64_t statPREV_shells_ejected = 0;
    std::int64_t statPREV_doors_kicked = 0;
    std::int64_t statPREV_cigs_smoked = 0;
    std::int64_t statPREV_rounds_beat = 0;
    std::int64_t statPREV_ml_drank = 0;
    double statPREV_total_cash = 0;
    std::int64_t statSAVE_shots_fired = 0;
    std::int64_t statSAVE_shells_ejected = 0;
    std::int64_t statSAVE_doors_kicked = 0;
    std::int64_t statSAVE_cigs_smoked = 0;
    std::int64_t statSAVE_rounds_beat = 0;
    std::int64_t statSAVE_ml_drank = 0;
    double statSAVE_total_cash = 0;

    bool has_unhandled_input() const override { return true; }
    void _unhandled_input(const godot::InputEventRef& event) override;   // gd: L39-L43
    void BeginEnding();                 // gd: L45-L87 (spawns BeginEndingRoutine)
    seq::Task GetInput();               // gd: L89-L92
    seq::Task ExitGame();               // gd: L94-L107
    seq::Task AmbienceFade();           // gd: L110-L115
    seq::Task SetupEnding();            // gd: L117-L128
    void FinalScore();                  // gd: L135-L175
    void SetStat(int line, const std::string& text);  // FinalScore: get_child(2).text
    void IncrementGlobalStats();        // gd: L194-L203
    void SavePlayerData();              // gd: L205-L217
    void LoadPlayerData();              // gd: L220-L232

private:
    seq::Task BeginEndingRoutine();
    void AnyKey();
};

}  // namespace scripts
