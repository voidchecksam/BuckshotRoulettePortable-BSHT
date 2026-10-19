// source: scripts/DealerIntelligence.gd (431 lines)
// PORT deviations:
//   - call() dispatches METHOD track keys by name (`dealer hands on table`
//     calls Speaker_DealerArrive).
#pragma once

#include "godot/animation_player.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <string>
#include <vector>

namespace scripts {

class DealerIntelligence : public godot::Script {
public:
    static constexpr const char* kClassName = "DealerIntelligence";
    const char* get_class_name() const override { return kClassName; }

    Medicine* medicine = nullptr;
    HandManager* hands = nullptr;
    ItemManager* itemManager = nullptr;
    ShellSpawner* shellSpawner = nullptr;
    ShellLoader* shellLoader = nullptr;
    ShotgunShooting* shotgunShooting = nullptr;
    RoundManager* roundManager = nullptr;
    godot::AnimationPlayer* animator_shotgun = nullptr;
    godot::AnimationPlayer* animator_dealerHands = nullptr;
    CameraManager* camera = nullptr;
    ShellEjectManager* ejectManager = nullptr;
    HealthCounter* healthCounter = nullptr;
    DeathManager* death = nullptr;
    CameraShaker* cameraShaker = nullptr;
    SmokeController* smoke = nullptr;
    ShellEjectManager* shellEject_dealer = nullptr;
    godot::AudioStreamPlayer* speaker_dealerarrive = nullptr;
    godot::AudioStreamPlayer* speaker_handCrack = nullptr;
    std::vector<std::string> soundArray_cracks;
    godot::AudioStreamPlayer* speaker_checkHandcuffs = nullptr;
    godot::AudioStreamPlayer* speaker_breakHandcuffs = nullptr;
    godot::AudioStreamPlayer* speaker_giveHandcuffs = nullptr;
    std::string sound_dealerarrive;
    std::string sound_dealerarriveCuffed;
    godot::VisualInstance3D* dealermesh_normal = nullptr;
    godot::VisualInstance3D* dealermesh_crushed = nullptr;
    std::vector<bool> sequenceArray_knownShell;
    Amounts* amounts = nullptr;

    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;

    seq::Task DealerCheckHandCuffs();                // gd: L39-L57
    void Animator_CheckHandcuffs();                  // gd: L59-L60
    void Animator_GiveHandcuffs();                   // gd: L62-L63
    void BeginDealerTurn();                          // gd: L65-L69
    seq::Task DealerChoice();                        // gd: L85-L280
    bool FigureOutShell();                           // gd: L282-L303 (endless only)
    void EndDealerTurn(bool canDealerGoAgain);       // gd: L305-L324
    void ChooseWhoToShootRandomly();                 // gd: L326-L329
    seq::Task GrabShotgun();                         // gd: L331-L339
    seq::Task EndTurnMain();                         // gd: L341-L348
    seq::Task Shoot(std::string who);                // gd: L350-L394
    void Speaker_DealerArrive();                      // gd: L396-L400
    void Speaker_DealerArrive_Cuffed();               // gd: L402-L406
    void Speaker_HandCrack();                         // gd: L408-L411
    void SwapDealerMesh();                           // gd: L414-L419
    bool swapped = false;
    bool dealerHoldingShotgun = false;   // gd: L37
    bool dealerAboutToBreakFree = false; // gd: L35
    bool dealerCanGoAgain = false;       // gd: L36
    std::string dealerTarget;            // gd: L71
    std::string knownShell;              // gd: L72
    bool dealerKnowsShell = false;       // gd: L73
    bool mainLoopFinished = false;       // gd: L74
    bool usingHandsaw = false;           // gd: L75
    bool dealerUsedItem = false;         // gd: L76
    bool usingMedicine = false;          // gd: L77
    bool adrenalineSetup = false;        // gd: L79
    bool stealing = false;               // gd: L80
    std::string adrenaline_itemSlot;     // gd: L81
    std::vector<std::string> inv_playerside;   // gd: L82
    std::vector<std::string> inv_dealerside;   // gd: L83
    int CoinFlip();                                  // gd: L421-L431
};

}  // namespace scripts
