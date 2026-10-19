// The music bus's low-pass on a small speaker.
//
// The original muffles the table's music with Godot's 2-pole low-pass at
// 156 Hz (batch 3: 436 Hz; the lobby: none). Exact on headphones, but the
// Vita's speaker plays little below a few hundred Hz, where that filter is
// already 21-32 dB down (500 Hz / 1 kHz), and batches 1-2 play no audible
// music. The Vita scales the cutoff rather than dropping it, so the shape
// stays (the table more muffled than batch 3, the lobby open):
//   x5: 156 -> 780 Hz (500 Hz -3.0 dB, 1 kHz -8.5 dB, 2 kHz -17.7 dB)
//       436 -> 2180 Hz (1 kHz -1.6 dB, 2 kHz -5.3 dB, 4 kHz -13.0 dB)
// The desktop keeps Godot's cutoff (scale 1). GameApp logs both values.
#pragma once

#include <algorithm>

namespace app {

inline constexpr float kVitaMusicCutoffScale = 5.0f;
inline constexpr float kMaxMusicCutoffHz = 20000.0f;

// 0 (or less) means the bus has no low-pass and stays that way.
inline float ScaledMusicCutoff(float godotCutoffHz, float scale) {
    if (godotCutoffHz <= 0.0f) return godotCutoffHz;
    return std::min(godotCutoffHz * scale, kMaxMusicCutoffHz);
}

}  // namespace app
