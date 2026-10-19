// Renders every cue in the sound bank as the port's mixer plays it
// (core::SfxVoice at gain 1, resampled to the device's 48 kHz) to one WAV per
// cue, for comparison with Godot's own playback (tools/godot/record_cues.gd,
// tools/compare_cues.py). Host only; game audio, so the output stays local.
//
//   render_cues <sfx.bank> <out dir>
// Writes <out dir>/<cue>.wav and <out dir>/cues.txt (one name per line).
#include "assets/sound_bank.h"
#include "core/bus_clamp.h"
#include "core/sfx_voice.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr int kDeviceRate = 48000;   // platform::kOutputRate

void Put32(std::FILE* f, uint32_t v) {
    const uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16),
                          static_cast<uint8_t>(v >> 24)};
    std::fwrite(b, 1, 4, f);
}

void Put16(std::FILE* f, uint16_t v) {
    const uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
    std::fwrite(b, 1, 2, f);
}

bool WriteWav(const std::string& path, const std::vector<int16_t>& stereo) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t bytes = static_cast<uint32_t>(stereo.size() * sizeof(int16_t));
    std::fwrite("RIFF", 1, 4, f);
    Put32(f, 36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    Put32(f, 16);
    Put16(f, 1);   // PCM
    Put16(f, 2);
    Put32(f, kDeviceRate);
    Put32(f, kDeviceRate * 4);
    Put16(f, 4);
    Put16(f, 16);
    std::fwrite("data", 1, 4, f);
    Put32(f, bytes);
    std::fwrite(stereo.data(), 1, bytes, f);
    std::fclose(f);
    return true;
}

std::vector<int16_t> Render(const assets::SoundBank& bank, const assets::Sound& s) {
    core::SfxVoice v;
    const auto run = [&bank, &s](int c) {
        return core::AdpcmChannel{bank.Data() + s.channel[c].offset, s.channel[c].predictor, s.channel[c].stepIndex};
    };
    // As a script's play(): ramped in, the sound's own interpolation.
    core::SfxStart start;
    start.left = run(0);
    if (s.channels == 2) start.right = run(1);
    start.frames = s.frames;
    start.step = static_cast<double>(s.rate) / kDeviceRate;
    start.linear = s.linear;
    v.Start(start);
    constexpr int kBlock = 1024;
    std::vector<int32_t> bus(kBlock * 2);
    std::vector<int16_t> out;
    while (v.Active()) {
        std::fill(bus.begin(), bus.end(), 0);
        const int n = v.Mix(bus.data(), kBlock);
        const std::size_t at = out.size();
        out.resize(at + static_cast<std::size_t>(n) * 2);
        core::ClampBus(bus.data(), out.data() + at, n * 2, 1.0f);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: render_cues <sfx.bank> <out dir>\n");
        return 2;
    }
    assets::SoundBank bank;
    if (!bank.LoadFromFile(argv[1])) {
        std::fprintf(stderr, "render_cues: could not load %s\n", argv[1]);
        return 1;
    }
    const std::string dir = argv[2];
    std::FILE* list = std::fopen((dir + "/cues.txt").c_str(), "w");
    if (!list) {
        std::fprintf(stderr, "render_cues: could not write %s/cues.txt (does the directory exist?)\n", dir.c_str());
        return 1;
    }
    int written = 0;
    for (const assets::Sound& s : bank.Sounds()) {
        if (!WriteWav(dir + "/" + s.name + ".wav", Render(bank, s))) {
            std::fprintf(stderr, "render_cues: could not write %s\n", s.name.c_str());
            continue;
        }
        std::fprintf(list, "%s\n", s.name.c_str());
        ++written;
    }
    std::fclose(list);
    std::printf("render_cues: %d of %lu cues -> %s\n", written, static_cast<unsigned long>(bank.Count()),
                dir.c_str());
    return written == static_cast<int>(bank.Count()) ? 0 : 1;
}
