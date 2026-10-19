// The mixer's one clamp: the 32-bit stereo bus at the master bus gain down to
// 16 bits. Clamp, never wrap: a wrapped sample is a loud click, and a mix of
// several loud sounds is exactly when it happens. Returns how many samples
// clipped; the device log reports it, because a hot mix is heard as
// distortion, not seen as an error.
#pragma once

#include <cstdint>

namespace core {

inline int ClampBus(const int32_t* bus, int16_t* out, int samples, float masterGain) {
    int clipped = 0;
    for (int i = 0; i < samples; ++i) {
        const int v = static_cast<int>(static_cast<float>(bus[i]) * masterGain);
        if (v > 32767) {
            out[i] = 32767;
            ++clipped;
        } else if (v < -32768) {
            out[i] = -32768;
            ++clipped;
        } else {
            out[i] = static_cast<int16_t>(v);
        }
    }
    return clipped;
}

}  // namespace core
