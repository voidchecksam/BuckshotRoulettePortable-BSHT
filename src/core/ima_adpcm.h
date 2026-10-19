// IMA ADPCM (the Interactive Multimedia Association's 4-bit DVI ADPCM): the
// sound bank's sample format. Two samples a byte, low nibble first; the step
// and index tables are the standard ones. Pure and headless: the baker encodes
// with it, the mixer decodes with it one sample at a time.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace core {

namespace adpcm_detail {

inline constexpr int kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

inline constexpr int kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

inline int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Applies one nibble to (predictor, index): the decoder, and the encoder's
// reconstruction, so the two cannot disagree.
inline void Step(int nibble, int& predictor, int& index) {
    const int step = kStepTable[index];
    int diff = step >> 3;
    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;
    predictor = Clamp(nibble & 8 ? predictor - diff : predictor + diff, -32768, 32767);
    index = Clamp(index + kIndexTable[nibble], 0, 88);
}

}  // namespace adpcm_detail

// One channel's ADPCM run as the decoder starts it. `bytes` null: no channel.
struct AdpcmChannel {
    const uint8_t* bytes = nullptr;
    int16_t predictor = 0;
    uint8_t stepIndex = 0;
};

struct AdpcmEncoded {
    std::vector<uint8_t> bytes;   // (samples + 1) / 2
    int16_t predictor = 0;        // the decoder's starting state
    uint8_t stepIndex = 0;
};

inline AdpcmEncoded EncodeImaAdpcm(const int16_t* pcm, std::size_t n) {
    AdpcmEncoded out;
    out.bytes.assign((n + 1) / 2, 0);
    int predictor = n > 0 ? pcm[0] : 0;
    int index = 0;
    out.predictor = static_cast<int16_t>(predictor);
    out.stepIndex = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const int step = adpcm_detail::kStepTable[index];
        int diff = pcm[i] - predictor;
        int nibble = 0;
        if (diff < 0) {
            nibble = 8;
            diff = -diff;
        }
        if (diff >= step) { nibble |= 4; diff -= step; }
        if (diff >= step >> 1) { nibble |= 2; diff -= step >> 1; }
        if (diff >= step >> 2) nibble |= 1;
        adpcm_detail::Step(nibble, predictor, index);
        out.bytes[i / 2] |= static_cast<uint8_t>(i % 2 == 0 ? nibble : nibble << 4);
    }
    return out;
}

// Decodes forward, one sample per Next(). Cheap enough for the mixer: a few
// integer operations a sample, no tables beyond the two above.
class AdpcmDecoder {
public:
    AdpcmDecoder() = default;
    AdpcmDecoder(const uint8_t* bytes, int16_t predictor, uint8_t stepIndex)
        : bytes_(bytes), predictor_(predictor), index_(stepIndex) {}

    int16_t Next() {
        const uint8_t b = bytes_[pos_ / 2];
        const int nibble = pos_ % 2 == 0 ? (b & 0x0F) : (b >> 4);
        ++pos_;
        adpcm_detail::Step(nibble, predictor_, index_);
        return static_cast<int16_t>(predictor_);
    }
    uint32_t Decoded() const { return pos_; }

private:
    const uint8_t* bytes_ = nullptr;
    uint32_t pos_ = 0;
    int predictor_ = 0;
    int index_ = 0;
};

}  // namespace core
