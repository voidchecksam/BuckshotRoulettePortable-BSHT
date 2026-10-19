// source: scripts/MusicManager.gd (34 lines)
// PORT deviations:
//   - `filter.effect_lowPass.cutoff_hz` is FilterController's field
//     effect_lowPass_cutoff_hz; the music router applies it to the stream.
#pragma once

#include "godot/node.h"
#include "godot/script.h"
#include "scripts/forward.h"

#include <vector>

namespace scripts {

class MusicManager : public godot::Script {
public:
    static constexpr const char* kClassName = "MusicManager";
    const char* get_class_name() const override { return kClassName; }

    std::vector<TrackInstance*> trackArray;
    godot::AudioStreamPlayer* speaker_music = nullptr;
    SpeakerController* speakerController_music = nullptr;
    FilterController* filter = nullptr;
    RoundManager* roundManager = nullptr;

    void _ready() override;                          // gd: L9-L11
    void EndTrack();                                 // gd: L13-L14
    void LoadTrack_FadeIn();                         // gd: L16-L19

    bool trackset = false;
    void LoadTrack();                                // gd: L22-L34
};

}  // namespace scripts
