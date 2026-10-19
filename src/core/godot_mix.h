// How Godot 4.1.4's AudioServer plays a sound, as rules the port's mixer shares
// (measured against Godot's own playback with tools/compare_cues.py): the
// start ramp every new playback gets, and the cubic its resampler uses. Bank
// voices (core::SfxVoice), streams (core::StreamResampler) and gain changes
// (core::GainRamp) all take them from here.
#pragma once

#include <cstdint>

namespace core {

// Godot 4.1.4 starts every playback at volume 0 and ramps it linearly over its
// first mix buffer (512 frames), of which the first 64 are the playback's
// silent lookahead (audio_server.cpp:357-365, 412-427, 638-648). Output frame k
// of the playback therefore sounds at (k + 64) / 512 of its gain until k = 448.
// Streams and voices both apply it; the 64-frame delay itself (1.3 ms) is not
// modelled. A sound an animation adds to a running polyphonic playback gets
// no ramp (core::SfxStart::rampIn).
inline constexpr uint32_t kGodotMixBuffer = 512;
inline constexpr uint32_t kGodotLookahead = 64;

inline float StartRamp(uint32_t frame) {
    return frame + kGodotLookahead >= kGodotMixBuffer
               ? 1.0f
               : static_cast<float>(frame + kGodotLookahead) / static_cast<float>(kGodotMixBuffer);
}

// AudioStreamPlaybackResampled::mix (audio_stream.cpp:148-154): the cubic
// through y0..y3, `mu` of the way from y1 to y2.
inline float GodotCubic(float y0, float y1, float y2, float y3, float mu) {
    const float mu2 = mu * mu;
    const float a0 = 3.0f * y1 - 3.0f * y2 + y3 - y0;
    const float a1 = 2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3;
    const float a2 = y2 - y0;
    const float a3 = 2.0f * y1;
    return (a0 * mu * mu2 + a1 * mu2 + a2 * mu + a3) * 0.5f;
}

}  // namespace core
