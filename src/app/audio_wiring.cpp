#include "app/audio_wiring.h"

#include <cstdio>

namespace app {

void WireAudio(AppConfig& config, platform::Audio& audio) {
    config.playSound = [&audio](const platform::SoundPlay& play) { return audio.PlaySound(play); };
    config.stopVoice = [&audio](int voice) { audio.StopVoice(voice); };
    config.stopAllVoices = [&audio] { audio.StopAllVoices(); };
    config.updateVoice = [&audio](int voice, float gainL, float gainR, float pitch, float shelf) {
        audio.UpdateVoice(voice, gainL, gainR, pitch, shelf);
    };
    config.setMusicGain = [&audio](float gainL, float gainR) { audio.SetMusicGain(gainL, gainR); };
    config.setMusicCutoff = [&audio](float hz) { audio.SetMusicCutoff(hz); };
    config.setMasterGain = [&audio](float gain) { audio.SetMasterGain(gain); };
    config.playMusic = [&audio](const char* path, float gainL, float gainR) {
        audio.PlayMusic(path, true, gainL, gainR);
    };
    // Stream voice 0 is the music; the router's voices follow it.
    config.playStream = [&audio](int voice, const char* path, bool loop, float gainL, float gainR, float pitch,
                                 bool musicBus) { audio.PlayStream(voice + 1, path, loop, gainL, gainR, pitch, musicBus); };
    config.stopStream = [&audio](int voice) { audio.StopStream(voice + 1); };
    config.setStreamGain = [&audio](int voice, float gainL, float gainR) {
        audio.SetStreamGain(voice + 1, gainL, gainR);
    };
    config.isBanked = [&audio](const char* name) { return audio.HasSound(name); };
}

std::string AudioExitSummary(platform::Audio& audio, GameApp& game) {
    char line[160];
    std::string out = "\n--- audio ---\n";
    // NO %zu: newlib prints a literal "zu" on the Vita.
    std::snprintf(line, sizeof line, "mix peak %.0f us per 21.33 ms callback = %.1f%% of one core\n",
                  audio.PeakMixMicros(), audio.PeakMixMicros() / 21333.0 * 100.0);
    out += line;
    std::snprintf(line, sizeof line, "animation sounds %d, missing cues %d\n",
                  game.Tree().animation_stats().audio_plays, audio.MissingCues());
    out += line;
    for (const std::string& m : audio.MissingNames()) out += "  missing cue: " + m + "\n";
    return out + audio.Summary() + "\n";
}

}  // namespace app
