// Offline audio bake.
//
// Decodes the short sound effects into a bank the device loads whole. Music
// and ambience stay .ogg and stream on device: decoding everything would cost
// hundreds of MB against roughly 69 MB of free RAM. The split is by duration
// (--max-seconds, 6 by default).
//
// Three sounds are .wav rather than .ogg (both gunshots and the health
// counter's beep), so the manifest lists both kinds and each is decoded by its
// own reader.
//
// Run from the repo root:
//   ls audio/*.ogg audio/*.wav > audio/_files.txt
//   bake_audio [--dir audio] [--out assets/baked/sfx.bank] [--max-seconds 6]
//              [--anims FILE] [--list music_list.txt]
#include "bake_main.h"

#include "assets/animation.h"
#include "assets/sound_bank.h"
#include "assets/wav.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "stb_vorbis.h"
}

namespace {

// Splits interleaved PCM into the bank's channels at the source's own rate:
// the left/right pair when the source is audibly stereo
// (assets::IsAudiblyStereo), else one mid channel (L + R) / 2, averaged rather
// than the left alone, or anything panned hard right would vanish.
struct BankChannels {
    std::vector<int16_t> left;
    std::vector<int16_t> right;   // empty: mono
};

BankChannels SplitForBank(const short* interleaved, int frames, int channels) {
    BankChannels out;
    if (frames <= 0 || channels <= 0) return out;
    const auto n = static_cast<std::size_t>(frames);
    out.left.resize(n);
    if (channels == 1) {
        out.left.assign(interleaved, interleaved + n);
        return out;
    }
    out.right.resize(n);
    for (std::size_t f = 0; f < n; ++f) {
        out.left[f] = interleaved[f * static_cast<std::size_t>(channels)];
        out.right[f] = interleaved[f * static_cast<std::size_t>(channels) + 1];
    }
    if (assets::IsAudiblyStereo(out.left, out.right)) return out;
    for (std::size_t f = 0; f < n; ++f) {
        out.left[f] = static_cast<int16_t>((static_cast<int>(out.left[f]) + out.right[f]) / 2);
    }
    out.right.clear();
    return out;
}

bool EndsWith(const std::string& s, const char* suffix) {
    const std::size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// Decoded interleaved PCM, whichever container it came from.
struct Decoded {
    std::vector<int16_t> samples;
    int channels = 0;
    int rate = 0;
    int Frames() const { return channels > 0 ? static_cast<int>(samples.size()) / channels : 0; }
};

bool DecodeOgg(const std::string& path, Decoded& out) {
    short* pcm = nullptr;
    const int frames = stb_vorbis_decode_filename(path.c_str(), &out.channels, &out.rate, &pcm);
    if (frames > 0 && pcm && out.channels > 0) {
        out.samples.assign(pcm, pcm + static_cast<std::size_t>(frames) * out.channels);
    }
    if (pcm) std::free(pcm);
    return !out.samples.empty();
}

bool DecodeWavFile(const std::string& path, Decoded& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<std::uint8_t> bytes;
    std::uint8_t buf[65536];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);

    assets::WavPcm pcm;
    const assets::WavError e = assets::DecodeWav(bytes.data(), bytes.size(), pcm);
    if (e != assets::WavError::None) {
        std::printf("  %s: %s\n", path.c_str(), assets::WavErrorText(e));
        return false;
    }
    out.samples = std::move(pcm.samples);
    out.channels = pcm.channels;
    out.rate = pcm.sampleRate;
    return !out.samples.empty();
}

}  // namespace

int BakeAudioMain(int argc, char** argv) {
    std::string dir = "audio";
    std::string out = "assets/baked/sfx.bank";
    std::string listPath = "assets/baked/music_list.txt";
    std::string animPath = "assets/extracted/animations.json";
    double maxSeconds = 6.0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--dir" && i + 1 < argc)              dir = argv[++i];
        else if (a == "--out" && i + 1 < argc)         out = argv[++i];
        else if (a == "--max-seconds" && i + 1 < argc) maxSeconds = std::atof(argv[++i]);
        else if (a == "--anims" && i + 1 < argc)      animPath = argv[++i];
        else if (a == "--list" && i + 1 < argc)       listPath = argv[++i];
    }

    // The file list comes from a manifest the caller generates, so this tool
    // needs no directory-walking API and behaves the same on every platform.
    const std::string manifest = dir + "/_files.txt";
    std::FILE* mf = std::fopen(manifest.c_str(), "rb");
    if (!mf) {
        std::printf("FATAL: %s not found.\n", manifest.c_str());
        std::printf("       Generate it with: ls %s/*.ogg %s/*.wav > %s/_files.txt\n",
                    dir.c_str(), dir.c_str(), dir.c_str());
        return 1;
    }

    std::vector<std::string> files;
    {
        char line[1024];
        while (std::fgets(line, sizeof(line), mf)) {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (!s.empty()) files.push_back(s);
        }
        std::fclose(mf);
    }
    std::printf("manifest lists %lu files\n", static_cast<unsigned long>(files.size()));

    // Cues fired from animation AUDIO tracks must be BANKED regardless of
    // length. They have to start on an exact frame, and a duration threshold is
    // the wrong classifier for them: `dealer use magnifier.ogg` is 8.97 s and
    // is unmistakably a sound effect, not music. Costs 395 KB.
    std::vector<std::string> animCues;
    {
        assets::AnimationLibrary lib;
        if (lib.LoadFromFile(animPath.c_str())) {
            for (const auto& pl : lib.Players()) {
                for (const auto& an : pl.animations) {
                    for (const auto& tr : an.tracks) {
                        if (tr.type != assets::TrackType::Audio) continue;
                        for (const auto& k : tr.keys) {
                            if (k.stream.empty()) continue;
                            std::string n = k.stream;
                            const std::size_t sl = n.find_last_of("/\\");
                            if (sl != std::string::npos) n = n.substr(sl + 1);
                            animCues.push_back(n);
                        }
                    }
                }
            }
            std::printf("animation references %lu audio cues\n",
                        static_cast<unsigned long>(animCues.size()));
        } else {
            std::printf("WARNING: %s missing -- long animation cues will be "
                        "streamed instead of banked and may not fire on time\n",
                        animPath.c_str());
        }
    }
    auto isAnimCue = [&animCues](const std::string& n) {
        for (const std::string& c : animCues) if (c == n) return true;
        return false;
    };

    assets::SoundBank bank;
    std::vector<std::string> streamed;
    int longCuesBanked = 0;
    int stereoSounds = 0;
    int failed = 0;
    double bakedSeconds = 0.0;

    for (const std::string& path : files) {
        Decoded pcm;
        const bool decoded = EndsWith(path, ".wav") ? DecodeWavFile(path, pcm)
                                                    : DecodeOgg(path, pcm);
        if (!decoded) {
            // Never silent: a file that fails to decode is a cue that will
            // never play, and tracing that from silence on the device is
            // expensive.
            std::printf("  FAILED to decode %s\n", path.c_str());
            ++failed;
            continue;
        }

        const int frames = pcm.Frames();
        const double seconds = static_cast<double>(frames) / pcm.rate;

        // Base name only: animation AUDIO tracks refer to files that way.
        std::string name = path;
        const std::size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos) name = name.substr(slash + 1);

        const bool cue = isAnimCue(name);
        if (seconds > maxSeconds && !cue) {
            streamed.push_back(name);
        } else {
            if (seconds > maxSeconds) ++longCuesBanked;
            const BankChannels ch = SplitForBank(pcm.samples.data(), frames, pcm.channels);
            bank.AddForBake(name, ch.left, ch.right, static_cast<uint32_t>(pcm.rate), EndsWith(path, ".wav"));
            if (!ch.right.empty()) ++stereoSounds;
            bakedSeconds += seconds;
        }
    }

    std::printf("\nSFX  baked   %4lu sounds  %6.1f s  %6.2f MB\n",
                static_cast<unsigned long>(bank.Count()), bakedSeconds,
                static_cast<double>(bank.BytesUsed()) / (1024.0 * 1024.0));
    std::printf("     stereo  %4d sounds (side within %.0f dB of mid), the rest one mid channel\n", stereoSounds,
                assets::kStereoSideThresholdDb);
    std::printf("MUSIC stream %4lu files (kept as .ogg, decoded on device)\n",
                static_cast<unsigned long>(streamed.size()));
    if (failed) std::printf("FAILED       %4d files did not decode\n", failed);

    if (!bank.WriteToFile(out.c_str())) {
        std::printf("FATAL: could not write %s (does the directory exist?)\n",
                    out.c_str());
        return 1;
    }
    std::printf("wrote %s\n", out.c_str());

    // Read it straight back. A bank that cannot load is worse than no bank, and
    // finding that out on the device costs a hardware round trip.
    assets::SoundBank check;
    if (!check.LoadFromFile(out.c_str())) {
        std::printf("FATAL: wrote a bank that will not load\n");
        return 1;
    }
    if (check.Count() != bank.Count()) {
        std::printf("FATAL: round trip lost sounds (%lu -> %lu)\n",
                    static_cast<unsigned long>(bank.Count()),
                    static_cast<unsigned long>(check.Count()));
        return 1;
    }
    std::printf("verified: reloads %lu sounds, %.2f MB\n",
                static_cast<unsigned long>(check.Count()),
                static_cast<double>(check.BytesUsed()) / (1024.0 * 1024.0));

    // The music list tells the packaging step which .ogg files to ship.
    std::FILE* lf = std::fopen(listPath.c_str(), "wb");
    if (lf) {
        for (const std::string& s : streamed) std::fprintf(lf, "%s\n", s.c_str());
        std::fclose(lf);
        std::printf("wrote %s\n", listPath.c_str());
    }
    return failed ? 1 : 0;
}
