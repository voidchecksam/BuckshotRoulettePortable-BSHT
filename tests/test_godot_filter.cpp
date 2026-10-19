// Godot 4.1.4's AudioFilterSW (servers/audio/audio_filter_sw.cpp), transcribed
// for the music bus's low-pass and the 3D attenuation high-shelf. Measured on
// sines, so the tests pin behaviour, not the coefficient algebra.
#include "core/godot_filter.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

constexpr float kRate = 48000.0f;

// The steady-state gain, in dB, of `f` on a sine at `hz`.
double ResponseDb(core::Biquad f, double hz) {
    const int n = 48000;
    double in = 0.0, out = 0.0;
    for (int i = 0; i < n; ++i) {
        const float x = static_cast<float>(std::sin(2.0 * 3.14159265358979 * hz * i / kRate));
        const float y = f.Process(x);
        if (i >= n / 2) {
            in += static_cast<double>(x) * x;
            out += static_cast<double>(y) * y;
        }
    }
    return 10.0 * std::log10(out / in);
}

// The music bus: AudioEffectLowPassFilter, resonance 0.5, FILTER_6DB (one
// biquad stage), cutoff 156 Hz at the table (FilterController).
TEST(GodotFilter, TheLowPassPassesDcAndIsDownSixDbAtItsCutoff) {
    core::Biquad f;
    f.Set(core::GodotLowPass(156.0f, 0.5f, kRate));
    EXPECT_NEAR(ResponseDb(f, 20.0), 0.0, 0.2);
    EXPECT_NEAR(ResponseDb(f, 156.0), -6.02, 0.2);   // |H(fc)| = Q for a 2-pole low-pass
}

TEST(GodotFilter, TheLowPassFallsTwelveDbAnOctave) {
    core::Biquad a, b;
    a.Set(core::GodotLowPass(156.0f, 0.5f, kRate));
    b.Set(core::GodotLowPass(156.0f, 0.5f, kRate));
    EXPECT_NEAR(ResponseDb(a, 1248.0) - ResponseDb(b, 2496.0), 12.0, 0.6);
}

// audio_server.cpp:614-636: HIGHSHELF, resonance 1, at the player's
// attenuation_filter_cutoff_hz, gain db_to_linear((1 - min(1, gain)) * -24).
// Godot feeds that gain in as the RBJ shelf's "A", so the shelf settles at
// gain squared: the asked-for cut at the cutoff, twice it far above.
TEST(GodotFilter, TheHighShelfLeavesTheLowsAndCutsTheHighs) {
    const float shelf = std::pow(10.0f, -4.8f / 20.0f);
    core::Biquad f;
    f.Set(core::GodotHighShelf(5000.0f, 1.0f, shelf, kRate));
    EXPECT_NEAR(ResponseDb(f, 200.0), 0.0, 0.2);
    EXPECT_NEAR(ResponseDb(f, 5000.0), -4.8, 0.3);
    EXPECT_NEAR(ResponseDb(f, 18000.0), -9.7, 0.4);
}

TEST(GodotFilter, AHighShelfAtGainOneIsFlat) {
    core::Biquad f;
    f.Set(core::GodotHighShelf(5000.0f, 1.0f, 1.0f, kRate));
    EXPECT_NEAR(ResponseDb(f, 300.0), 0.0, 0.05);
    EXPECT_NEAR(ResponseDb(f, 12000.0), 0.0, 0.05);
}

}  // namespace
