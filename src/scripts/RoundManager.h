// source: scripts/RoundManager.gd (528 lines)
// PORT deviations:
//   - untyped GDScript vars get the narrowest type their uses need (ints for
//     the scores, double for timers, std::string for wireToCut).
//   - batchArray holds Nodes; SetupRoundArray reads each one's RoundBatch
//     script (GDScript reads the fields off the Node dynamically).
//   - port_playerTurnsBegun counts the SetupDeskUI calls; tests wait on it.
#pragma once

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <cstdint>
#include <string>
#include <vector>

namespace scripts {

class RoundManager : public godot::Script {
public:
    static constexpr const char* kClassName = "RoundManager";
    const char* get_class_name() const override { return kClassName; }

    // v2.2
    Statue* statue = nullptr;

    Signature* sign = nullptr;
    BriefcaseMachine* brief = nullptr;
    DefibCutter* defibCutter = nullptr;
    SegmentManager* segmentManager = nullptr;
    HandcuffManager* handcuffs = nullptr;
    ItemManager* itemManager = nullptr;
    godot::Node* death = nullptr;  // PORT: DeathManager not translated yet
    PlayerData* playerData = nullptr;
    CursorManager* cursor = nullptr;
    ControllerManager* controller = nullptr;
    PermissionManager* perm = nullptr;
    int health_player = 0;
    int health_opponent = 0;
    std::vector<godot::Node*> batchArray;
    std::vector<RoundClass*> roundArray;
    ShellSpawner* shellSpawner = nullptr;
    ShellLoader* shellLoader = nullptr;
    int currentRound = 0;
    int mainBatchIndex = 0;
    HealthCounter* healthCounter = nullptr;
    DealerIntelligence* dealerAI = nullptr;
    godot::Node* typingManager = nullptr;  // PORT: nothing calls TypingManager's methods
    CameraManager* camera = nullptr;
    std::vector<godot::Vector3> roundIndicatorPositions;
    godot::Node3D* roundIndicatorParent = nullptr;
    godot::Node3D* roundIndicator = nullptr;
    godot::AnimationPlayer* animator_roundIndicator = nullptr;
    godot::AudioStreamPlayer3D* speaker_roundHum = nullptr;
    godot::AudioStreamPlayer3D* speaker_roundShutDown = nullptr;
    godot::AudioStreamPlayer3D* speaker_winner = nullptr;
    godot::Node3D* ui_winner = nullptr;
    godot::AnimationPlayer* animator_dealer = nullptr;
    std::vector<godot::Node*> ejectManagers;  // PORT: ShellEjectManager not translated yet
    godot::AnimationPlayer* animator_dealerHands = nullptr;
    godot::Node* gameover = nullptr;  // PORT: GameOverManager not translated yet
    MusicManager* musicManager = nullptr;
    DefibCutter* deficutter = nullptr;   // gd: sic, a second export of the same node
    godot::AnimationPlayer* anim_doubleor = nullptr;
    godot::AnimationPlayer* anim_yes = nullptr;
    godot::AnimationPlayer* anim_no = nullptr;
    InteractionBranch* intbranch_yes = nullptr;
    InteractionBranch* intbranch_no = nullptr;
    godot::AudioStreamPlayer* speaker_slot = nullptr;

    bool endless = false;
    bool shellLoadingSpedUp = false;
    std::vector<std::string> dealerItems;
    int currentShotgunDamage = 1;
    bool dealerAtTable = false;
    bool dealerHasGreeted = false;
    bool dealerCuffed = false;
    bool playerCuffed = false;
    bool playerAboutToBreakFree = false;
    bool waitingForDealerReturn = false;
    bool barrelSawedOff = false;
    bool defibCutterReady = false;
    bool trueDeathActive = false;
    std::vector<std::string> playerCurrentTurnItemArray;

    void _ready() override;                                  // gd: L62-L66
    bool has_process() const override { return true; }
    void _process(double delta) override;                    // gd: L68-L70

    bool counting = false;
    double initial_time = 0;
    void InitialTimer();                                     // gd: L74-L75
    void BeginMainGame();                                    // gd: L77-L78
    void HideDealer();                                       // gd: L80-L82

    bool lerping = false;
    bool enteringFromWaiver = false;
    seq::Task MainBatchSetup(bool dealerEnterAtStart);       // gd: L87-L134

    Amounts* amounts = nullptr;
    void ParseMainGameAmounts();                             // gd: L137-L139
    int curhealth = 0;
    void GenerateRandomBatches();                            // gd: L142-L157
    void SetupRoundArray();                                  // gd: L160-L169
    seq::Task RoundIndicator();                              // gd: L173-L193
    seq::Task StartRound(bool gettingNext);                  // gd: L196-L234
    void ReturnFromItemGrabbing();                           // gd: L237-L239
    void LoadShells();                                       // gd: L241-L243
    // PORT: omitted CheckIfOutOfHealth (EndTurn's only call is commented out), gd: L245-L253

    bool waitingForReturn = false;
    bool waitingForHealthCheck = false;
    bool waitingForHealthCheck2 = false;
    bool requestedWireCut = false;
    std::string wireToCut;
    bool wireIsCut_dealer = false;
    bool wireIsCut_player = false;

    bool ignoring = false;
    seq::Task EndTurn(bool playerCanGoAgain);                // gd: L264-L295
    seq::Task ReturnFromCuffCheck(bool brokeFree);           // gd: L297-L306
    seq::Task BeginPlayerTurn();                             // gd: L308-L334

    godot::Control* deskUI_parent = nullptr;
    godot::Control* deskUI_shotgun = nullptr;
    godot::Control* deskUI_briefcase = nullptr;
    std::vector<godot::Control*> deskUI_grids;
    void SetupDeskUI();                                      // gd: L340-L348
    void ClearDeskUI(bool includingParent);                  // gd: L350-L355
    seq::Task OutOfHealth(std::string who);                  // gd: L357-L362

    bool doubling = false;
    int64_t prevscore = 0;
    int64_t mainscore = 0;
    double elapsed = 0;
    double dur = 3;
    int double_or_nothing_rounds_beat = 0;
    int64_t double_or_nothing_score = 0;
    int64_t double_or_nothing_initial_score = 0;
    bool doubled = false;

    bool lerpingscore = false;
    int64_t startscore = 0;
    int64_t endscore = 0;
    godot::Label3D* ui_score = nullptr;
    godot::Label3D* ui_doubleornothing = nullptr;
    godot::AudioStreamPlayer* speaker_key = nullptr;
    godot::AudioStreamPlayer* speaker_show = nullptr;
    godot::AudioStreamPlayer* speaker_hide = nullptr;

    godot::Control* btnParent_doubleor = nullptr;
    godot::Control* btn_yes = nullptr;
    seq::Task BeginScoreLerp();                              // gd: L385-L426
    void RevertDoubleUI();                                   // gd: L428-L429

    Achievement* ach = nullptr;
    seq::Task Response(bool rep);                            // gd: L432-L457
    void LerpScore();                                        // gd: L459-L464
    seq::Task RestartBatch();                                // gd: L466-L480
    seq::Task EndMainBatch();                                // gd: L482-L528

    int port_playerTurnsBegun = 0;
};

}  // namespace scripts
