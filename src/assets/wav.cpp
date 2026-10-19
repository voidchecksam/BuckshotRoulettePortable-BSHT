#include "assets/wav.h"

#include <cstring>

namespace assets {
namespace {

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;

std::uint16_t U16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t U32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

const char* WavErrorText(WavError e) {
    switch (e) {
    case WavError::None:        return "ok";
    case WavError::NotRiffWave: return "not a RIFF/WAVE file";
    case WavError::NoFormat:    return "no fmt chunk before the data";
    case WavError::NotPcm16:    return "not 16-bit PCM";
    case WavError::NoData:      return "no data chunk";
    case WavError::Truncated:   return "a chunk runs past the end of the file";
    }
    return "unknown";
}

WavError DecodeWav(const std::uint8_t* data, std::size_t size, WavPcm& out) {
    out = WavPcm{};
    if (!data || size < 12 || std::memcmp(data, "RIFF", 4) != 0 ||
        std::memcmp(data + 8, "WAVE", 4) != 0) {
        return WavError::NotRiffWave;
    }

    bool haveFormat = false;
    std::size_t at = 12;
    while (at + 8 <= size) {
        const std::uint8_t* chunk = data + at;
        const std::size_t length = U32(chunk + 4);
        const std::size_t body = at + 8;
        if (length > size - body) return WavError::Truncated;

        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (length < 16) return WavError::Truncated;
            const std::uint16_t format = U16(data + body);
            const std::uint16_t bits = U16(data + body + 14);
            // Extensible headers carry the same 16-bit PCM for our purposes;
            // the sub-format GUID only matters for float, which bits rules out.
            if ((format != kFormatPcm && format != kFormatExtensible) || bits != 16) {
                return WavError::NotPcm16;
            }
            out.channels = U16(data + body + 2);
            out.sampleRate = static_cast<int>(U32(data + body + 4));
            if (out.channels <= 0 || out.sampleRate <= 0) return WavError::NotPcm16;
            haveFormat = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            if (!haveFormat) return WavError::NoFormat;
            const std::size_t frames = length / (2u * static_cast<std::size_t>(out.channels));
            out.samples.resize(frames * static_cast<std::size_t>(out.channels));
            for (std::size_t i = 0; i < out.samples.size(); ++i) {
                out.samples[i] = static_cast<int16_t>(U16(data + body + 2 * i));
            }
            return WavError::None;
        }
        // Chunks are word-aligned: an odd length is followed by a pad byte.
        at = body + length + (length & 1u);
    }
    return haveFormat ? WavError::NoData : WavError::NoFormat;
}

}  // namespace assets
