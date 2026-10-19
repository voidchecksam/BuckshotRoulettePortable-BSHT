// source: scripts/HealthCounter.gd (212 lines)
// PORT deviations:
//   - SwapSkullSymbols: `sym.get_parent()` is cast to VisualInstance3D.
//   - coroutines called without await (UpdateDisplayRoutineCigarette_Main,
//     ShowText_ForDuration) are spawned.
#pragma once

#include "godot/animation_player.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <string>
#include <vector>

namespace scripts {

class HealthCounter : public godot::Script {
public:
    static constexpr const char* kClassName = "HealthCounter";
    const char* get_class_name() const override { return kClassName; }

    godot::Label3D* ui_playerwin = nullptr;
    DefibCutter* defibCutter = nullptr;
    RoundManager* roundManager = nullptr;
    std::vector<godot::MeshInstance3D*> symbolArray_player;
    std::vector<godot::MeshInstance3D*> symbolArray_enemy;
    godot::Node3D* uiparent = nullptr;
    godot::Label3D* ui_playername = nullptr;
    std::vector<godot::Node3D*> uiArray;
    godot::AudioStreamPlayer3D* speaker_bootup = nullptr;
    godot::AudioStreamPlayer3D* speaker_beep = nullptr;
    godot::AudioStreamPlayer3D* speaker_noise = nullptr;
    CameraManager* camera = nullptr;
    godot::AnimationPlayer* animator_flickerEnemy = nullptr;
    godot::AnimationPlayer* animator_flickerPlayer = nullptr;
    godot::AnimationPlayer* animator_playerHands = nullptr;
    godot::AnimationPlayer* animator_dealerHands = nullptr;
    Dialogue* dialogue = nullptr;
    godot::AudioStreamPlayer* speaker_truedeath = nullptr;
    std::vector<godot::VisualInstance3D*> skullSymbols;

    bool dialogueEntered_player = false;
    bool dialogueEntered_dealer = false;

    void _ready() override;                                                   // gd: L26-L28

    bool setting = true;
    void SetupHealth();                                                       // gd: L31-L42
    void UpdateDisplay();                                                     // gd: L44-L54
    void FlickerLastDigit();                                                  // gd: L56-L60

    bool playerShotSelf = false;
    bool trueDeathSetup = false;
    bool checkingPlayer = false;
    bool skipping_careful = false;
    seq::Task UpdateDisplayRoutine(bool isRaisingHealth, bool goingToPreviousSocket,
                                   bool showPlayerWin);                       // gd: L66-L121

    bool overriding_medicine_adding = false;
    bool overriding_medicine = false;
    // Animation METHOD tracks: "player use cigarettes" and "dealer use
    // cigarettes" call the cigarette routines (trap 4).
    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;
    void UpdateDisplayRoutineCigarette_Enemy();                               // gd: L126-L139
    void UpdateDisplayRoutineCigarette_Player();                              // gd: L141-L153
    seq::Task UpdateDisplayRoutineCigarette_Main(bool isChanging, bool isAddingEnemy);  // gd: L155-L179
    void SwapSkullSymbols();                                                  // gd: L181-L184

    bool settingUpBroken = false;
    seq::Task Bootup();                                                       // gd: L188-L201
    void ClearDisplay();                                                      // gd: L203-L207
    void DisableCounter();                                                    // gd: L209-L211
};

}  // namespace scripts
