// One banked effect playing (core::SfxVoice): ADPCM decoded as it plays, at the
// bank rate over the device rate times AudioStreamPlayer.pitch_scale, mixed
// into a 32-bit bus that is clamped once, after every voice.
#include "core/sfx_voice.h"

#include "core/ima_adpcm.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

struct Fixture {
    std::vector<int16_t> pcm;
    core::AdpcmEncoded enc;
    explicit Fixture(int n, int16_t level = 8000) : pcm(static_cast<std::size_t>(n), level) {
        enc = core::EncodeImaAdpcm(pcm.data(), pcm.size());
    }
    core::SfxVoice Voice(double step, float gain = 1.0f) const { return Voice(step, gain, gain); }
    core::SfxVoice Voice(double step, float gainL, float gainR) const {
        core::SfxVoice v;
        v.Start(Play(step, gainL, gainR));
        return v;
    }
    core::SfxStart Play(double step = 1.0, float gainL = 1.0f, float gainR = 1.0f) const {
        core::SfxStart s;
        s.left = {enc.bytes.data(), enc.predictor, enc.stepIndex};
        s.frames = static_cast<uint32_t>(pcm.size());
        s.step = step;
        s.gainL = gainL;
        s.gainR = gainR;
        return s;
    }
};

int FramesUntilDone(core::SfxVoice v) {
    std::vector<int32_t> bus(2 * 64, 0);
    int frames = 0;
    while (v.Active() && frames < 100000) {
        std::fill(bus.begin(), bus.end(), 0);
        frames += v.Mix(bus.data(), 64);
    }
    return frames;
}

}  // namespace

TEST(SfxVoice, PlaysEverySampleAtStepOne) {
    const Fixture f(1000);
    EXPECT_EQ(FramesUntilDone(f.Voice(1.0)), 1000);
}

TEST(SfxVoice, PitchTwoPlaysInHalfTheFrames) {
    const Fixture f(1000);
    EXPECT_EQ(FramesUntilDone(f.Voice(2.0)), 500);
}

TEST(SfxVoice, AddsIntoTheBusWithoutClamping) {
    // Two full-scale voices sum past 16 bits; the bus keeps the sum (read past
    // the start ramp).
    const Fixture f(1024, 30000);
    std::vector<int32_t> bus(2 * 1024, 0);
    core::SfxVoice a = f.Voice(1.0), b = f.Voice(1.0);
    a.Mix(bus.data(), 1024);
    b.Mix(bus.data(), 1024);
    EXPECT_GT(bus[2 * 600], 32767);
}

// Godot's mix gives each channel its own gain (godot::OutputGain).
TEST(SfxVoice, EachChannelTakesItsOwnGain) {
    const Fixture f(256, 8000);
    std::vector<int32_t> bus(2 * 128, 0);
    core::SfxVoice v = f.Voice(1.0, 1.0f, 0.25f);
    v.Mix(bus.data(), 128);
    EXPECT_NEAR(bus[2 * 100], 8000 * 164 / 512, 40);   // still ramping in
    EXPECT_NEAR(bus[2 * 100 + 1], 2000 * 164 / 512, 10);
}

// Godot 4.1.4 starts every playback at volume 0 and ramps it over its first
// 512-frame mix buffer, the first 64 of which are the (silent) lookahead
// (audio_server.cpp:357-365, 412-427, 638-648): source frame k plays at
// (k + 64) / 512. Without it the port's attacks ran up to 8.7 dB hot.
TEST(SfxVoice, AttackRampsInOverGodotsFirstMixBuffer) {
    const Fixture f(2048, 8000);
    std::vector<int32_t> bus(2 * 1024, 0);
    core::SfxVoice v = f.Voice(1.0);
    v.Mix(bus.data(), 1024);
    EXPECT_NEAR(bus[0], 8000 * 64 / 512, 40);
    EXPECT_NEAR(bus[2 * 192], 8000 * 256 / 512, 40);
    EXPECT_NEAR(bus[2 * 447], 8000 * 511 / 512, 40);
    EXPECT_NEAR(bus[2 * 600], 8000, 40);
}

// v3 bank: a stereo sound decodes each channel from its own ADPCM run.
TEST(SfxVoice, AStereoSoundPlaysEachChannelFromItsOwnRun) {
    const Fixture left(1024, 8000), right(1024, -4000);
    std::vector<int32_t> bus(2 * 1024, 0);
    core::SfxStart play = left.Play();
    play.right = {right.enc.bytes.data(), right.enc.predictor, right.enc.stepIndex};
    core::SfxVoice v;
    v.Start(play);
    v.Mix(bus.data(), 1024);
    EXPECT_NEAR(bus[2 * 600], 8000, 40);
    EXPECT_NEAR(bus[2 * 600 + 1], -4000, 40);
}

// Godot resamples with a 4-point cubic (AudioStreamPlaybackResampled::mix,
// audio_stream.cpp:134-154); linear interpolation dulled the port's top
// octave (-0.7 dB median above 8 kHz against Godot's playback).
TEST(SfxVoice, ResamplesWithGodotsCubicSoTheTopOctaveKeepsItsLevel) {
    constexpr int kN = 8192;
    std::vector<int16_t> sine(kN);
    for (int i = 0; i < kN; ++i) {
        sine[static_cast<std::size_t>(i)] =
            static_cast<int16_t>(12000.0 * std::sin(2.0 * 3.14159265358979 * 10000.0 * i / 44100.0));
    }
    const core::AdpcmEncoded enc = core::EncodeImaAdpcm(sine.data(), sine.size());
    core::SfxStart play;
    play.left = {enc.bytes.data(), enc.predictor, enc.stepIndex};
    play.frames = kN;
    play.step = 44100.0 / 48000.0;
    core::SfxVoice v;
    v.Start(play);
    std::vector<int32_t> bus(2 * 8000, 0);
    v.Mix(bus.data(), 8000);
    double in = 0.0, out = 0.0;
    for (int i = 1000; i < 7000; ++i) out += static_cast<double>(bus[2 * i]) * bus[2 * i];
    for (int i = 1000; i < 7000; ++i) in += static_cast<double>(sine[static_cast<std::size_t>(i)]) * sine[static_cast<std::size_t>(i)];
    const double db = 10.0 * std::log10(out / in);
    // Godot's cubic itself droops 0.37 dB at 10 kHz here (linear: 1.46).
    EXPECT_NEAR(db, -0.37, 0.15) << "10 kHz came out " << db << " dB";
}

// Godot plays a .wav (AudioStreamPlaybackWAV, audio_stream_wav.cpp:
// 204-208) with LINEAR interpolation; only the resampled .ogg path is cubic.
TEST(SfxVoice, AWavSoundResamplesLinearlyAsGodotsWavPlaybackDoes) {
    constexpr int kN = 8192;
    std::vector<int16_t> sine(kN);
    for (int i = 0; i < kN; ++i) {
        sine[static_cast<std::size_t>(i)] =
            static_cast<int16_t>(12000.0 * std::sin(2.0 * 3.14159265358979 * 10000.0 * i / 44100.0));
    }
    const core::AdpcmEncoded enc = core::EncodeImaAdpcm(sine.data(), sine.size());
    core::SfxStart play;
    play.left = {enc.bytes.data(), enc.predictor, enc.stepIndex};
    play.frames = kN;
    play.step = 44100.0 / 48000.0;
    play.linear = true;
    core::SfxVoice v;
    v.Start(play);
    std::vector<int32_t> bus(2 * 8000, 0);
    v.Mix(bus.data(), 8000);
    double in = 0.0, out = 0.0;
    for (int i = 1000; i < 7000; ++i) out += static_cast<double>(bus[2 * i]) * bus[2 * i];
    for (int i = 1000; i < 7000; ++i) in += static_cast<double>(sine[static_cast<std::size_t>(i)]) * sine[static_cast<std::size_t>(i)];
    EXPECT_NEAR(10.0 * std::log10(out / in), -1.46, 0.15);
}

// A sound an animation track adds to a player that is already playing joins
// its AudioStreamPolyphonic at full volume (audio_stream_polyphonic.cpp:198-
// 211): no AudioServer start ramp.
TEST(SfxVoice, ASoundWithoutRampInStartsAtFullGain) {
    const Fixture f(2048, 8000);
    std::vector<int32_t> bus(2 * 64, 0);
    core::SfxStart play = f.Play();
    play.rampIn = false;
    core::SfxVoice v;
    v.Start(play);
    v.Mix(bus.data(), 64);
    EXPECT_NEAR(bus[0], 8000, 40);
}

// An audio key's start_offset (animation_player.cpp:882-909): playback starts
// that far into the sound, and the voice ends with it.
TEST(SfxVoice, StartsAtItsStartFrame) {
    std::vector<int16_t> ramp(4000);
    for (int i = 0; i < 4000; ++i) ramp[static_cast<std::size_t>(i)] = static_cast<int16_t>(i * 4);
    const core::AdpcmEncoded enc = core::EncodeImaAdpcm(ramp.data(), ramp.size());
    core::SfxStart play;
    play.left = {enc.bytes.data(), enc.predictor, enc.stepIndex};
    play.frames = 4000;
    play.startFrame = 3000;
    play.rampIn = false;
    core::SfxVoice v;
    v.Start(play);
    std::vector<int32_t> bus(2 * 2000, 0);
    EXPECT_EQ(v.Mix(bus.data(), 2000), 1000);
    EXPECT_NEAR(bus[0], 12000, 120);
}

// Godot stops a playback (stop(), a polyphony cut, an audio key's end) by
// ramping it to 0 across one mix buffer (FADE_OUT_TO_DELETION,
// audio_server.cpp:412-427; finish_request in the polyphonic mix).
TEST(SfxVoice, ReleaseFadesOutAcrossOneMixBuffer) {
    const Fixture f(4096, 8000);
    core::SfxStart play = f.Play();
    play.rampIn = false;
    core::SfxVoice v;
    v.Start(play);
    std::vector<int32_t> bus(2 * 1024, 0);
    v.Release();
    EXPECT_EQ(v.Mix(bus.data(), 1024), 512);
    EXPECT_NEAR(bus[0], 8000, 40);
    EXPECT_NEAR(bus[2 * 256], 4000, 40);
    EXPECT_FALSE(v.Active());
}

// An audio key's end_offset: the sound is stopped (and fades) once it has
// played to `endFrame`.
TEST(SfxVoice, ReleasesItselfAtItsEndFrame) {
    const Fixture f(4096, 8000);
    core::SfxStart play = f.Play();
    play.rampIn = false;
    play.endFrame = 1000;
    core::SfxVoice v;
    v.Start(play);
    std::vector<int32_t> bus(2 * 4096, 0);
    EXPECT_EQ(v.Mix(bus.data(), 4096), 1512);
    EXPECT_NEAR(bus[2 * 900], 8000, 40);
    EXPECT_NEAR(bus[2 * 1256], 4000, 40);
}

// A playing sound follows its player (AudioStreamPlayer::set_pitch_scale and
// the per-buffer volume read): a new gain is ramped to across one mix buffer,
// a new pitch takes effect at once.
TEST(SfxVoice, ANewGainIsRampedToAcrossOneMixBuffer) {
    const Fixture f(4096, 8000);
    core::SfxStart play = f.Play();
    play.rampIn = false;
    core::SfxVoice v;
    v.Start(play);
    v.SetTarget(0.0f, 0.5f, 1.0);
    std::vector<int32_t> bus(2 * 1024, 0);
    v.Mix(bus.data(), 1024);
    EXPECT_NEAR(bus[2 * 256], 4000, 40);          // left half way to 0
    EXPECT_NEAR(bus[2 * 600], 0, 40);             // left there
    EXPECT_NEAR(bus[2 * 600 + 1], 4000, 40);      // right at 0.5
}

TEST(SfxVoice, ANewPitchChangesTheStepAtOnce) {
    const Fixture f(1000);
    core::SfxStart play = f.Play();
    core::SfxVoice v;
    v.Start(play);
    v.SetTarget(1.0f, 1.0f, 2.0);
    EXPECT_EQ(FramesUntilDone(v), 500);
}

// a 3D playback always runs its attenuation shelf, and a new
// playback's filter starts from zeroed coefficients interpolated to the
// target across its first mix buffer (AudioFilterSW::Coeffs default 0,
// update_coeffs(buffer_size) + process_one_interp): even a flat shelf fades
// the attack in. A 2D or plain voice has no filter.
TEST(SfxVoice, AThreeDVoicesShelfFadesItsAttackIn) {
    const Fixture f(4096, 8000);
    core::SfxStart play = f.Play();
    play.rampIn = false;
    play.positional = true;
    core::SfxVoice v;
    v.Start(play);
    std::vector<int32_t> bus(2 * 1024, 0);
    v.Mix(bus.data(), 1024);
    EXPECT_LT(std::abs(bus[0]), 400) << "the coefficients start at 0";
    EXPECT_NEAR(bus[2 * 900], 8000, 200) << "settled flat by the end of the first buffer";

    core::SfxStart flat = f.Play();
    flat.rampIn = false;
    core::SfxVoice w;
    w.Start(flat);
    std::vector<int32_t> bus2(2 * 64, 0);
    w.Mix(bus2.data(), 64);
    EXPECT_NEAR(bus2[0], 8000, 40) << "a 2D voice: no filter";
}
