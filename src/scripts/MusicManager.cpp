#include "scripts/MusicManager.h"

#include "scripts/FilterController.h"
#include "scripts/RoundManager.h"
#include "scripts/SpeakerController.h"
#include "scripts/resources.h"

#include <string>

namespace scripts {

// gd: L9-L11
void MusicManager::_ready() {}

// gd: L13-L14
void MusicManager::EndTrack() {
    speaker_music->stop();
}

// gd: L16-L19
void MusicManager::LoadTrack_FadeIn() {
    speakerController_music->SnapVolume(false);
    LoadTrack();
    speakerController_music->FadeIn();
}

// gd: L22-L34
void MusicManager::LoadTrack() {
    const int currentBatch = roundManager->playerData->currentBatchIndex;
    std::string currentTrack;
    const TrackInstance& track = *trackArray[static_cast<std::size_t>(currentBatch)];
    if (roundManager->playerData->playerEnteringFromDeath && !trackset) {
        currentTrack = track.audiofile_secondloop;
        trackset = true;
    } else {
        currentTrack = track.audiofile;
    }
    filter->lowPassDefaultValue = track.defaultLowPassHz;
    filter->effect_lowPass_cutoff_hz = filter->lowPassDefaultValue;
    filter->moving = false;
    speaker_music->set_stream(currentTrack);
    speaker_music->play();
}

}  // namespace scripts
