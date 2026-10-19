// source: scripts/ShellSpawner.gd (170 lines)
// PORT deviations:
//   - untyped `var spawnedShell` / `var spawnedShellObjectArray = []` hold the
//     instanced shell roots: godot::Node3D* and std::vector<godot::Node3D*>.
//   - untyped `var locationIndex` is an int.
//   - `var seq = []` shares its name with the seq:: namespace, so declarations
//     inside the class spell the coroutine type ::seq::Task.
//   - SpawnShells: add_child takes ownership of the instance (unique_ptr).
//   - CheckBackdropScaling: the locale is "EN"; the port ships English only.
//   - MainShellRoutine: `seq = sequenceArray` shares one Array in GDScript; a
//     copy here. Nothing mutates sequenceArray while PlayAudioIndicators reads
//     seq (the shuffle comes ~4 s after the indicators finish).
#pragma once

#include "godot/animation_player.h"
#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <string>
#include <vector>

namespace godot { class PackedScene; }

namespace scripts {

class ShellSpawner : public godot::Script {
public:
    static constexpr const char* kClassName = "ShellSpawner";
    const char* get_class_name() const override { return kClassName; }

    Dialogue* dialogue = nullptr;
    godot::AnimationPlayer* anim_compartment = nullptr;
    RoundManager* roundManager = nullptr;
    CameraManager* camera = nullptr;
    godot::PackedScene* shellInstance = nullptr;
    std::vector<godot::Vector3> shellLocationArray;
    godot::Node3D* spawnParent = nullptr;
    HealthCounter* healthCounter = nullptr;
    std::vector<std::string> soundArray_latchOpen;
    godot::AudioStreamPlayer* speaker_latchOpen = nullptr;
    godot::AudioStreamPlayer* speaker_audioIndicator = nullptr;
    std::vector<std::string> soundArray_indicators;
    DealerIntelligence* ai = nullptr;

    godot::Node3D* spawnedShell = nullptr;
    int locationIndex = 0;
    std::vector<std::string> tempSequence;
    std::vector<std::string> sequenceArray;
    ShellClass* shellBranch = nullptr;
    std::vector<godot::Node3D*> spawnedShellObjectArray;

    void _ready() override;                           // gd: L24-L25
    bool has_process() const override { return true; }
    void _process(double delta) override;             // gd: L28-L29

    bool skipDialoguePresented = false;
    ::seq::Task MainShellRoutine();                   // gd: L32-L94
    bool CheckBackdropScaling();                      // gd: L96-L102
    void SpawnShells(int numberOfShells, int numberOfLives, int numberOfBlanks,
                     bool shufflingArray);            // gd: L104-L139

    bool tog = false;
    void PlayLatchSound();                            // gd: L142-L146

    std::vector<std::string> seq;
    ::seq::Task PlayAudioIndicators();                // gd: L149-L155
};

}  // namespace scripts
