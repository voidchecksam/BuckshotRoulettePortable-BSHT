// RIFF/WAVE decoding for the offline audio bake.
//
// Three of the game's sounds are .wav, not .ogg: the two gunshots and the
// health counter's beep. stb_vorbis cannot read them.
//
// Only what the game ships: uncompressed 16-bit PCM, any channel count, any
// rate. Anything else is refused with a reason rather than decoded wrongly.
// Headless and allocation-bounded by the input, so it is unit-tested.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace assets {

enum class WavError {
    None,
    NotRiffWave,     // not a RIFF/WAVE file at all
    NoFormat,        // no `fmt ` chunk before `data`
    NotPcm16,        // compressed, float, or not 16 bits per sample
    NoData,          // no `data` chunk
    Truncated,       // a chunk claims more bytes than the file has
};

const char* WavErrorText(WavError e);

struct WavPcm {
    int channels = 0;
    int sampleRate = 0;
    std::vector<int16_t> samples;   // interleaved

    int Frames() const {
        return channels > 0 ? static_cast<int>(samples.size()) / channels : 0;
    }
};

WavError DecodeWav(const std::uint8_t* data, std::size_t size, WavPcm& out);

}  // namespace assets
