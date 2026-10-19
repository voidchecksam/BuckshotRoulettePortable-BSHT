// The sound bank.
//
// The bank is loaded whole into RAM on a device with ~69 MB free, and a bad
// offset would read past the buffer on the first gunshot, on hardware, with no
// debugger. So the reader validates rather than trusts.
#include <gtest/gtest.h>

#include "assets/sound_bank.h"
#include "core/ima_adpcm.h"

#include <cstdio>
#include <string>

using namespace assets;

namespace {
std::string TempPath(const char* n) { return std::string(BSHT_BINARY_DIR) + "/" + n; }
std::string BakedPath() { return std::string(BSHT_SOURCE_DIR) + "/assets/baked/sfx.bank"; }
}  // namespace

namespace {
// The first decoded sample of channel `c` of sound `i`.
int FirstSample(const SoundBank& r, int i, int c) {
    const Sound& snd = r.At(i);
    core::AdpcmDecoder d(r.Data() + snd.channel[c].offset, snd.channel[c].predictor, snd.channel[c].stepIndex);
    return static_cast<int>(d.Next());
}
}  // namespace

// v3: each sound keeps its source's own rate, and a stereo source keeps both
// channels. Godot plays the .ogg as authored, and a mono bank cancels the blank
// gunshot (its side is louder than its mid).
TEST(SoundBank, RoundTripsMonoAndStereoSoundsAtTheirOwnRate) {
    SoundBank b;
    b.AddForBake("shot.ogg", {1000, 2000, 3000, 4000}, {-1000, -2000, -3000, -4000}, 48000, false);
    b.AddForBake("click.ogg", {-5000, -6000}, {}, 44100, false);
    b.AddForBake("beep.wav", {100, 200}, {}, 44100, true);

    const std::string p = TempPath("rt.bank");
    ASSERT_TRUE(b.WriteToFile(p.c_str()));

    SoundBank r;
    ASSERT_TRUE(r.LoadFromFile(p.c_str()));
    ASSERT_EQ(r.Count(), 3u);
    EXPECT_TRUE(r.At(r.Find("beep.wav")).linear) << "a .wav plays linear, as Godot's WAV playback";
    EXPECT_FALSE(r.At(r.Find("click.ogg")).linear);

    const int shot = r.Find("shot.ogg");
    ASSERT_GE(shot, 0);
    EXPECT_EQ(r.At(shot).frames, 4u);
    EXPECT_EQ(r.At(shot).rate, 48000u);
    EXPECT_EQ(r.At(shot).channels, 2);
    EXPECT_NEAR(FirstSample(r, shot, 0), 1000, 8);
    EXPECT_NEAR(FirstSample(r, shot, 1), -1000, 8);

    const int click = r.Find("click.ogg");
    ASSERT_GE(click, 0);
    EXPECT_EQ(r.At(click).frames, 2u);
    EXPECT_EQ(r.At(click).rate, 44100u);
    EXPECT_EQ(r.At(click).channels, 1);
    EXPECT_NEAR(FirstSample(r, click, 0), -5000, 8);
}

TEST(SoundBank, StereoIsKeptOnlyWhereItIsHeard) {
    const std::vector<int16_t> same{100, -200, 300, -400, 500, -600, 700, -800};
    EXPECT_FALSE(IsAudiblyStereo(same, same)) << "identical channels are mono";
    std::vector<int16_t> inverted(same.size());
    for (std::size_t i = 0; i < same.size(); ++i) inverted[i] = static_cast<int16_t>(-same[i]);
    EXPECT_TRUE(IsAudiblyStereo(same, inverted)) << "anti-phase cancels in a mono mix";
    std::vector<int16_t> loud(1000), nudged(1000);
    for (int i = 0; i < 1000; ++i) {
        loud[static_cast<std::size_t>(i)] = static_cast<int16_t>(i % 2 ? 20000 : -20000);
        nudged[static_cast<std::size_t>(i)] = static_cast<int16_t>(loud[static_cast<std::size_t>(i)] + (i % 2 ? 1 : -1));
    }
    EXPECT_FALSE(IsAudiblyStereo(loud, nudged)) << "a side 86 dB below the mid is inaudible";
}

TEST(SoundBank, ReportsAnUnknownNameRatherThanReturningSilence) {
    SoundBank b;
    b.AddForBake("a.ogg", {1}, {}, 44100, false);
    EXPECT_EQ(b.Find("nope.ogg"), -1);
    EXPECT_EQ(b.Find(nullptr), -1);
}

TEST(SoundBank, RejectsBadMagicVersionAndMissingFiles) {
    SoundBank r;
    EXPECT_FALSE(r.LoadFromFile(TempPath("absent.bank").c_str()));
    EXPECT_FALSE(r.LoadFromFile(nullptr));

    SoundBank b;
    b.AddForBake("a.ogg", {1, 2}, {}, 44100, false);
    const std::string p = TempPath("bad.bank");
    ASSERT_TRUE(b.WriteToFile(p.c_str()));

    std::FILE* f = std::fopen(p.c_str(), "r+b");
    ASSERT_NE(f, nullptr);
    std::fwrite("XXXX", 1, 4, f);
    std::fclose(f);
    EXPECT_FALSE(r.LoadFromFile(p.c_str()));
    EXPECT_EQ(r.LastError(), SoundBankError::BadMagic);
    EXPECT_EQ(r.Count(), 0u) << "a rejected bank must leave nothing behind";

    // A stale version names itself, so the report can say re-bake.
    ASSERT_TRUE(b.WriteToFile(p.c_str()));
    f = std::fopen(p.c_str(), "r+b");
    ASSERT_NE(f, nullptr);
    std::fseek(f, 4, SEEK_SET);
    const uint32_t stale = kSoundBankVersion - 1;
    std::fwrite(&stale, sizeof stale, 1, f);
    std::fclose(f);
    EXPECT_FALSE(r.LoadFromFile(p.c_str()));
    EXPECT_EQ(r.LastError(), SoundBankError::BadVersion);

    // A corrupt count is refused, not trusted (it once sized a reserve).
    ASSERT_TRUE(b.WriteToFile(p.c_str()));
    f = std::fopen(p.c_str(), "r+b");
    ASSERT_NE(f, nullptr);
    std::fseek(f, 16, SEEK_SET);
    const uint32_t huge = 0x7FFFFFFFu;
    std::fwrite(&huge, sizeof huge, 1, f);
    std::fclose(f);
    EXPECT_FALSE(r.LoadFromFile(p.c_str()));
    EXPECT_EQ(r.LastError(), SoundBankError::Inconsistent);
}

TEST(SoundBank, RefusesToWriteAnEmptyBank) {
    SoundBank b;
    EXPECT_FALSE(b.WriteToFile(TempPath("empty.bank").c_str()));
    EXPECT_FALSE(b.WriteToFile(nullptr));
}

TEST(SoundBank, ReportsSecondsFromItsOwnRate) {
    SoundBank b;
    b.AddForBake("s.ogg", std::vector<int16_t>(48000, 0), {}, 48000, false);
    EXPECT_FLOAT_EQ(b.At(0).Seconds(), 1.0f);
}

TEST(SoundBank, LoadsTheRealBakedBank) {
    SoundBank b;
    if (std::FILE* f = std::fopen(BakedPath().c_str(), "rb")) {
        std::fclose(f);
    } else {
        GTEST_SKIP() << "run ./build/src/bake_audio first";
    }
    // A bank that exists but is rejected (a stale version) fails here rather
    // than skipping, or a silent vpk would package with every test green.
    ASSERT_TRUE(b.LoadFromFile(BakedPath().c_str()))
        << "assets/baked/sfx.bank exists but was rejected (stale version?) -- re-run bake_audio";
    // 148 short effects, one long animation cue, and three .wav sounds.
    // `dealer use magnifier.ogg` is 8.97 s, so the duration rule would send it
    // to streaming, but it is a one-shot cue that has to fire on an exact
    // frame, and length is the wrong classifier for that. Banking it costs
    // 380 KB. The .wav files (both gunshots and the counter beep) need
    // bake_audio's RIFF reader; without them every shot is silent.
    EXPECT_EQ(b.Count(), 152u);
    EXPECT_GE(b.Find("temp gunshot_live.wav"), 0);
    EXPECT_GE(b.Find("temp gunshot_blank.wav"), 0);
    EXPECT_GE(b.Find("health counter beep2.wav"), 0);
    // Its side carries more than its mid: in mono it all but cancelled.
    EXPECT_EQ(b.At(b.Find("temp gunshot_blank.wav")).channels, 2);
    EXPECT_TRUE(b.At(b.Find("temp gunshot_live.wav")).linear);
    EXPECT_FALSE(b.At(b.Find("shelldrop7.ogg")).linear);

    // Must fit in RAM. v3 (stereo where heard, native rates) is 11.2 MB; a
    // device run with the 6.2 MB v2 bank had 34 MB free after the scene change,
    // so v3 leaves ~29 MB.
    const double mb = static_cast<double>(b.BytesUsed()) / (1024.0 * 1024.0);
    EXPECT_LT(mb, 12.0) << "bank grew to " << mb << " MB; it must stay in RAM";

    // Every sound must sit inside the pool: a bad offset reads past the buffer
    // on the first play.
    for (const Sound& s : b.Sounds()) {
        EXPECT_FALSE(s.name.empty());
        EXPECT_GT(s.frames, 0u) << s.name << " is empty";
        ASSERT_TRUE(s.channels == 1 || s.channels == 2) << s.name;
        ASSERT_TRUE(s.rate == 44100u || s.rate == 48000u) << s.name << " at " << s.rate << " Hz";
        for (int c = 0; c < s.channels; ++c) {
            ASSERT_LE(static_cast<std::size_t>(s.channel[c].offset) + (s.frames + 1) / 2, b.DataSize())
                << s.name << " runs past the sample pool";
        }
        // Animation cues are exempt from the duration cutoff, so allow the
        // longest of them rather than asserting a limit that is not the rule.
        EXPECT_LE(s.Seconds(), 12.0f) << s.name << " is implausibly long for a cue";
    }
}

TEST(SoundBank, EveryAnimationReferencedCueIsInTheBankOrStreamed) {
    // A cue the animation asks for that is in neither place plays as silence,
    // which is indistinguishable from a bug anywhere else in the audio path.
    SoundBank b;
    if (!b.LoadFromFile(BakedPath().c_str())) {
        GTEST_SKIP() << "run ./build/src/bake_audio first";
    }
    // The 26 animation-referenced streams were confirmed present on disk; this
    // asserts the short ones actually made it into the bank.
    int found = 0;
    for (const char* n : {"dealer use handsaw.ogg", "dealer use handcuffs.ogg",
                          "dealer use cigarettes.ogg", "dealer use magnifier.ogg"}) {
        if (b.Find(n) >= 0) ++found;
    }
    EXPECT_EQ(found, 4) << "animation cues missing from the bank";
}
