// source: scripts/IntroManager.gd (327 lines)
// PORT deviations:
//   - Coroutines keep their GDScript names and return seq::Task. A call made
//     without await in the .gd is a spawn owned by this node
//     (get_tree()->spawn(X(), get_node_owner())): freeing the intro manager
//     drops them, as Godot drops a freed instance's function states.
//   - FileAccess.file_exists(unlocker.savepath) is unlocker->Unlocked(). Those
//     branches need the Double or Nothing unlock, which only the ending writes.
#pragma once

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/node.h"
#include "godot/physics.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <string>
#include <vector>

namespace scripts {

class IntroManager : public godot::Script {
public:
    static constexpr const char* kClassName = "IntroManager";
    const char* get_class_name() const override { return kClassName; }

    // v2.2
    Statue* statue = nullptr;
    PipelineManager* pipeline = nullptr;

    godot::Node3D* cameraParent = nullptr;
    InteractionBranch* intbranch_bathroomdoor = nullptr;
    InteractionBranch* intbranch_backroomdoor = nullptr;
    InteractionBranch* intbranch_pillbottle = nullptr;
    godot::Node3D* parent_pills = nullptr;
    CursorManager* cursor = nullptr;
    RoundManager* roundManager = nullptr;
    godot::AudioStreamPlayer* speaker_amb_restroom = nullptr;
    godot::Control* viewblocker = nullptr;
    godot::AnimationPlayer* animator_camera = nullptr;
    godot::AnimationPlayer* animator_smokerdude = nullptr;
    MusicManager* musicmanager = nullptr;
    FilterController* filter = nullptr;
    BpmLight* bpmlight = nullptr;
    godot::Node3D* smokerdude_revival = nullptr;
    godot::AudioStreamPlayer* speaker_defib = nullptr;
    godot::Vector3 pos_floor;
    godot::Vector3 rot_floor;
    godot::AnimationPlayer* animator_pp = nullptr;
    CameraShaker* cameraShaker = nullptr;
    Dialogue* dia = nullptr;
    Blockout* blockout = nullptr;
    godot::AnimationPlayer* animator_hint = nullptr;
    godot::AnimationPlayer* animator_pillchoice = nullptr;
    godot::AudioStreamPlayer* speaker_pillchoice = nullptr;
    InteractionBranch* intbranch_pillyes = nullptr;
    InteractionBranch* intbranch_pillno = nullptr;
    InteractionBranch* intbranch_crt = nullptr;
    godot::AudioStreamPlayer* speaker_pillselect = nullptr;
    godot::AnimationPlayer* anim_revert = nullptr;
    Endless* endlessmode = nullptr;
    godot::Control* btn_bathroomdoor = nullptr;
    godot::Control* btn_pills = nullptr;
    godot::Control* btn_pillsYes = nullptr;
    godot::Control* btn_pillsNo = nullptr;
    godot::Control* btn_backroom = nullptr;
    godot::Control* btn_screen = nullptr;
    ControllerManager* controller = nullptr;
    Unlocker* unlocker = nullptr;
    CRT* crtManager = nullptr;
    std::vector<godot::CollisionShape3D*> col_pillchoice;
    godot::AnimationPlayer* anim_pillflicker = nullptr;
    std::vector<InteractionBranch*> intbs_crtbuttons;
    Achievement* ach = nullptr;
    Unlocker* pill_unlock = nullptr;

    bool allowingPills = false;
    bool counting = false;
    double count_current = 0;
    double count_max = 60;
    bool fs1 = false;

    void _ready() override;                     // gd: L47-L63 (spawns ReadyRoutine)
    seq::Task ReadyRoutine();                   // gd: L47-L63 (the awaiting body)
    bool has_process() const override { return true; }
    void _process(double delta) override;       // gd: L69-L70
    void CountTimer();                          // gd: L72-L76
    void SetControllerState();                  // gd: L78-L80
    seq::Task MainBathroomStart();              // gd: L82-L100
    seq::Task Hint();                           // gd: L102-L106
    seq::Task RevivalBathroomStart();           // gd: L108-L153
    void MainTrackLoad();                       // gd: L155-L160
    seq::Task Interaction_PillBottle();         // gd: L162-L185
    seq::Task Interaction_CRT();                // gd: L188-L201
    seq::Task StartSound();                     // gd: L203-L206
    void EnabledInteractionCRT();               // gd: L208-L210
    void DisableInteractionCrt();               // gd: L212-L214
    seq::Task SelectedPill(bool selected);      // gd: L218-L253
    seq::Task RevertCRT();                      // gd: L255-L275
    void Interaction_BackroomDoor();            // gd: L278-L285
    seq::Task Interaction_BathroomDoor();       // gd: L287-L303
    void BeginGame();                           // gd: L306-L308
    void PanFilter();                           // gd: L310-L312
    void KickDoorBackroom();                    // gd: L314-L315
    seq::Task KickDoorLobby();                  // gd: L317-L321
    void RestRoomIdle();                        // gd: L324-L327

    // ANIMATION METHOD TRACKS ("camera enter backroom" and friends).
    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;

private:
    void Spawn(seq::Task&& task);   // a call without await: owned by this node
};

}  // namespace scripts
