// Godot 4.1.4's AudioFilterSW, the two modes the game hears: LOWPASS for the
// music bus's AudioEffectLowPassFilter (FilterController muffles the track at
// the table) and HIGHSHELF for a 3D player's attenuation filter
// (audio_server.cpp:614-636). One biquad stage each (FILTER_6DB,
// set_stages(1)). Transcribed from servers/audio/audio_filter_sw.cpp
// (prepare_coefficients) and audio_filter_sw.h (process_one). Headless.
//
// Kept as Godot computes it, quirk included: HIGHSHELF uses its gain as the RBJ
// cookbook's "A", so the shelf settles at gain squared: the cut asked for at
// the cutoff, twice it (in dB) far above. Checked in tests/test_godot_filter.cpp.
#pragma once

#include <cmath>

namespace core {

struct BiquadCoeffs {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
};

namespace godot_filter_detail {

struct Shape {
    double cos_v, sin_v, Q;
};

// The cutoff clamp, omega and Q exactly as prepare_coefficients takes them.
inline Shape ShapeOf(float cutoff, float resonance, float rate) {
    const double srLimit = static_cast<int>(rate / 2) + 512;
    double c = cutoff > srLimit ? srLimit : cutoff;
    if (c < 1.0) c = 1.0;
    const double omega = 6.28318530717958647692 * c / rate;
    double Q = resonance;
    if (Q <= 0.0) Q = 0.0001;
    return {std::cos(omega), std::sin(omega), Q};
}

inline BiquadCoeffs Normalise(double b0, double b1, double b2, double a1, double a2, double a0) {
    return {static_cast<float>(b0 / a0), static_cast<float>(b1 / a0), static_cast<float>(b2 / a0),
            static_cast<float>(a1 / (0.0 - a0)), static_cast<float>(a2 / (0.0 - a0))};
}

}  // namespace godot_filter_detail

inline BiquadCoeffs GodotLowPass(float cutoff, float resonance, float rate) {
    const auto s = godot_filter_detail::ShapeOf(cutoff, resonance, rate);
    const double alpha = s.sin_v / (2.0 * s.Q);
    return godot_filter_detail::Normalise((1.0 - s.cos_v) / 2.0, 1.0 - s.cos_v, (1.0 - s.cos_v) / 2.0,
                                          -2.0 * s.cos_v, 1.0 - alpha, 1.0 + alpha);
}

inline BiquadCoeffs GodotHighShelf(float cutoff, float resonance, float gain, float rate) {
    const auto s = godot_filter_detail::ShapeOf(cutoff, resonance, rate);
    double g = gain;
    if (g < 0.001) g = 0.001;
    double tmpq = std::sqrt(s.Q);
    if (tmpq <= 0.0) tmpq = 0.001;
    const double beta = std::sqrt(g) / tmpq;
    const double a0 = (g + 1.0) - (g - 1.0) * s.cos_v + beta * s.sin_v;
    return godot_filter_detail::Normalise(g * ((g + 1.0) + (g - 1.0) * s.cos_v + beta * s.sin_v),
                                          -2.0 * g * ((g - 1.0) + (g + 1.0) * s.cos_v),
                                          g * ((g + 1.0) + (g - 1.0) * s.cos_v - beta * s.sin_v),
                                          2.0 * ((g - 1.0) - (g + 1.0) * s.cos_v),
                                          (g + 1.0) - (g - 1.0) * s.cos_v - beta * s.sin_v, a0);
}

// AudioFilterSW::Processor::process_one: one channel's history.
class Biquad {
public:
    void Set(const BiquadCoeffs& c) { c_ = c; }
    void Clear() { ha1_ = ha2_ = hb1_ = hb2_ = 0.0f; }
    float Process(float x) {
        const float y = x * c_.b0 + hb1_ * c_.b1 + hb2_ * c_.b2 + ha1_ * c_.a1 + ha2_ * c_.a2;
        ha2_ = ha1_;
        hb2_ = hb1_;
        hb1_ = x;
        ha1_ = y;
        return y;
    }

private:
    BiquadCoeffs c_;
    float ha1_ = 0.0f, ha2_ = 0.0f, hb1_ = 0.0f, hb2_ = 0.0f;
};

// AudioFilterSW::Processor driven the way _mix_step_for_channel drives a 3D
// playback's shelf (audio_server.cpp:614-640): its coefficients move to a new
// target linearly across `frames` samples (update_coeffs(buffer_size) +
// process_one_interp, which filters with the current set, then steps it), and
// a fresh one starts from Coeffs' zeros with no history.
class InterpBiquad {
public:
    void Reset() {
        cur_ = BiquadCoeffs{0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        step_ = cur_;
        left_ = 0;
        ha1_ = ha2_ = hb1_ = hb2_ = 0.0f;
    }
    void Target(const BiquadCoeffs& c, int frames) {
        target_ = c;
        if (frames <= 0) {
            cur_ = c;
            left_ = 0;
            return;
        }
        const float inv = 1.0f / static_cast<float>(frames);
        step_ = {(c.b0 - cur_.b0) * inv, (c.b1 - cur_.b1) * inv, (c.b2 - cur_.b2) * inv, (c.a1 - cur_.a1) * inv,
                 (c.a2 - cur_.a2) * inv};
        left_ = frames;
    }
    float Process(float x) {
        const float y = x * cur_.b0 + hb1_ * cur_.b1 + hb2_ * cur_.b2 + ha1_ * cur_.a1 + ha2_ * cur_.a2;
        ha2_ = ha1_;
        hb2_ = hb1_;
        hb1_ = x;
        ha1_ = y;
        if (left_ > 0) {
            cur_.b0 += step_.b0;
            cur_.b1 += step_.b1;
            cur_.b2 += step_.b2;
            cur_.a1 += step_.a1;
            cur_.a2 += step_.a2;
            if (--left_ == 0) cur_ = target_;
        }
        return y;
    }

private:
    BiquadCoeffs cur_{0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    BiquadCoeffs step_{0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    BiquadCoeffs target_;
    int left_ = 0;
    float ha1_ = 0.0f, ha2_ = 0.0f, hb1_ = 0.0f, hb2_ = 0.0f;
};

}  // namespace core
