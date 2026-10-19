// source: scripts/CrtManager.gd (171 lines)
// PORT deviations:
//   - Interaction awaits (the exit branch): a coroutine, spawned where the
//     original calls it without awaiting.
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

class CRT : public godot::Script {
public:
    static constexpr const char* kClassName = "CRT";
    const char* get_class_name() const override { return kClassName; }

    ExitManager* exit = nullptr;
    bool viewing = false;

    IntroManager* intro = nullptr;
    godot::VisualInstance3D* bathroom_normal = nullptr;
    godot::VisualInstance3D* bathroom_broken = nullptr;
    godot::Node3D* mask = nullptr;

    std::vector<godot::Node3D*> objarray_normal;
    std::vector<godot::Node3D*> objarray_broken;
    godot::AnimationPlayer* anim_intro = nullptr;
    std::vector<godot::Node3D*> array_bootup;
    std::vector<godot::Node3D*> array_bootuplogo;
    std::vector<PartitionBranch*> array_partbranch;
    std::vector<godot::Node3D*> array_stats;
    godot::Node3D* screenparent_leaderboard = nullptr;
    godot::Node3D* screenparent_stats = nullptr;

    int window_index = 3;
    std::vector<CrtIcon*> iconbranches;
    godot::AnimationPlayer* anim_iconfade = nullptr;
    Board* board = nullptr;

    godot::AudioStreamPlayer* speaker_playerwalk = nullptr;
    godot::AudioStreamPlayer* speaker_bootuploop = nullptr;
    godot::AudioStreamPlayer* speaker_shutdown = nullptr;
    godot::AudioStreamPlayer* speaker_consolebeep = nullptr;
    godot::AudioStreamPlayer* speaker_buttonpress = nullptr;
    godot::AudioStreamPlayer* speaker_navbeep = nullptr;
    godot::AudioStreamPlayer* speaker_melody = nullptr;
    godot::AudioStreamPlayer* speaker_melodyhide = nullptr;

    int selection_range1 = 1;
    int selection_range2 = 12;

    void _ready() override;                                       // gd: L38-L40
    bool has_unhandled_input() const override { return true; }
    void _unhandled_input(const godot::InputEventRef& event) override;   // gd: L42-L52
    void DisableCRT();                                            // gd: L54-L55
    void SetCRT(bool state);                                      // gd: L57-L69

    InteractionBranch* branch_right = nullptr;
    InteractionBranch* branch_left = nullptr;
    InteractionBranch* branch_window = nullptr;
    InteractionBranch* branch_exit = nullptr;

    bool has_exited = false;
    seq::Task Interaction(std::string alias);                     // gd: L77-L104
    void CycleWindow();                                           // gd: L106-L126
    seq::Task Bootup();                                           // gd: L128-L171
};

}  // namespace scripts
