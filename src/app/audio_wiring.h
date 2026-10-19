// The audio half of AppConfig, identical on both platforms: sounds, the music
// streamer, its bus gain and low-pass, and the master bus (Godot's bus 0, set
// by OptionsManager's volume). Both entry points had these lines word for
// word, which entry_points_stay_platform_only cannot see (it counts lines).
#pragma once

#include "app/game_app.h"
#include "platform/audio.h"

namespace app {

void WireAudio(AppConfig& config, platform::Audio& audio);

// The report's closing audio block: the mixer's peak cost per 1024-frame
// callback at 48 kHz (out of the callback's 21.33 ms: the share of one core
// the mixer takes), animation sounds, missing cues by name, and the streams'
// summary.
std::string AudioExitSummary(platform::Audio& audio, GameApp& game);

}  // namespace app
