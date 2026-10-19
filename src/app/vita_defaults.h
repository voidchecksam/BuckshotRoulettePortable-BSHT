// What the Vita build sets differently from the desktop, in one place: the
// entry point calls ApplyVitaDefaults(config) once, and every Vita-only choice
// is listed here with its reason instead of scattered through main_vita.cpp
// (which stays platform wiring, <= 400 lines).
#pragma once

#include "app/game_app.h"
#include "app/music_cutoff.h"

namespace app {

inline void ApplyVitaDefaults(AppConfig& config) {
    config.hideDesktopOptions = true;   // pad only, one screen mode
    config.vitaPadNames = true;         // the exit hint names the pad button
    config.padResetsRun = false;        // Triangle is "reset" in the original: a stray press wiped the run
    config.deviceDefaults = true;       // only the paths proven on a Vita (app/device_features.h)
    config.musicCutoffScale = kVitaMusicCutoffScale;   // the speaker hears the music (app/music_cutoff.h)
}

// A scenario boot (app/scenario_boot.h): the table straight away, and each shot
// held long enough for Vita3K's window grab (app/shot_hold.h).
inline void ApplyDeviceScenario(AppConfig& config, const std::string& scenarioJson) {
    config.scenarioJson = scenarioJson;
    config.skipMenu = true;
    config.scenarioShotHoldFrames = 90;
}

}  // namespace app
