// The gunshots are .wav. These pin the decoder the bake relies on for them.
#include "assets/wav.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void Put16(Bytes& b, int v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void Put32(Bytes& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}

void PutTag(Bytes& b, const char* tag) { b.insert(b.end(), tag, tag + 4); }

// A RIFF/WAVE file with an optional junk chunk before the data.
Bytes MakeWav(int format, int channels, int rate, int bits,
              const std::vector<int16_t>& samples, int junkLength = -1) {
    Bytes b;
    PutTag(b, "RIFF");
    Put32(b, 0);   // the RIFF size is never trusted, so leaving it 0 proves that
    PutTag(b, "WAVE");
    PutTag(b, "fmt ");
    Put32(b, 16);
    Put16(b, format);
    Put16(b, channels);
    Put32(b, static_cast<std::uint32_t>(rate));
    Put32(b, static_cast<std::uint32_t>(rate * channels * bits / 8));
    Put16(b, channels * bits / 8);
    Put16(b, bits);
    if (junkLength >= 0) {
        PutTag(b, "LIST");
        Put32(b, static_cast<std::uint32_t>(junkLength));
        for (int i = 0; i < junkLength; ++i) b.push_back(0xAB);
        if (junkLength & 1) b.push_back(0);
    }
    PutTag(b, "data");
    Put32(b, static_cast<std::uint32_t>(samples.size() * 2));
    for (int16_t s : samples) Put16(b, s);
    return b;
}

}  // namespace

TEST(Wav, DecodesStereoSixteenBitPcm) {
    const Bytes file = MakeWav(1, 2, 44100, 16, {1, -1, 32767, -32768});
    assets::WavPcm pcm;
    ASSERT_EQ(assets::DecodeWav(file.data(), file.size(), pcm), assets::WavError::None);
    EXPECT_EQ(pcm.channels, 2);
    EXPECT_EQ(pcm.sampleRate, 44100);
    EXPECT_EQ(pcm.Frames(), 2);
    EXPECT_EQ(pcm.samples, (std::vector<int16_t>{1, -1, 32767, -32768}));
}

TEST(Wav, SkipsUnknownChunksIncludingTheirPadByte) {
    const Bytes file = MakeWav(1, 1, 22050, 16, {7, 8, 9}, /*junkLength=*/3);
    assets::WavPcm pcm;
    ASSERT_EQ(assets::DecodeWav(file.data(), file.size(), pcm), assets::WavError::None);
    EXPECT_EQ(pcm.samples, (std::vector<int16_t>{7, 8, 9}));
}

TEST(Wav, AcceptsTheExtensibleHeaderForSixteenBitPcm) {
    const Bytes file = MakeWav(0xFFFE, 1, 44100, 16, {42});
    assets::WavPcm pcm;
    EXPECT_EQ(assets::DecodeWav(file.data(), file.size(), pcm), assets::WavError::None);
}

TEST(Wav, RefusesWhatItCannotDecodeCorrectly) {
    assets::WavPcm pcm;
    const Bytes eightBit = MakeWav(1, 1, 44100, 8, {});
    EXPECT_EQ(assets::DecodeWav(eightBit.data(), eightBit.size(), pcm), assets::WavError::NotPcm16);

    const Bytes floats = MakeWav(3, 1, 44100, 16, {});
    EXPECT_EQ(assets::DecodeWav(floats.data(), floats.size(), pcm), assets::WavError::NotPcm16);

    const Bytes ogg = {'O', 'g', 'g', 'S', 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_EQ(assets::DecodeWav(ogg.data(), ogg.size(), pcm), assets::WavError::NotRiffWave);
    EXPECT_EQ(assets::DecodeWav(nullptr, 0, pcm), assets::WavError::NotRiffWave);
}

TEST(Wav, RefusesADataChunkThatRunsPastTheFile) {
    Bytes file = MakeWav(1, 1, 44100, 16, {1, 2, 3, 4});
    file.resize(file.size() - 3);
    assets::WavPcm pcm;
    EXPECT_EQ(assets::DecodeWav(file.data(), file.size(), pcm), assets::WavError::Truncated);
    EXPECT_TRUE(pcm.samples.empty());
}

TEST(Wav, DecodesTheGamesOwnGunshot) {
    // The real file, so a header quirk in what actually ships cannot hide:
    // bsht-extract's rebuild, the one the sound bank is made from.
    const std::string path = BSHT_SOURCE_DIR "/assets/extracted/audio/temp gunshot_live.wav";
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) GTEST_SKIP() << "run bsht-extract and refresh assets/ first: " << path;
    Bytes file;
    std::uint8_t buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) file.insert(file.end(), buf, buf + n);
    std::fclose(f);

    assets::WavPcm pcm;
    ASSERT_EQ(assets::DecodeWav(file.data(), file.size(), pcm), assets::WavError::None);
    EXPECT_EQ(pcm.channels, 2);
    EXPECT_EQ(pcm.sampleRate, 44100);
    EXPECT_GT(pcm.Frames(), 44100 * 2) << "the live gunshot is over two seconds long";
}
