// One banked effect playing: the sound bank's IMA ADPCM decoded as it plays
// (one run per channel; a stereo source stays stereo), resampled with Godot's
// interpolation for its kind (cubic for an .ogg, linear for a .wav), advanced
// by `step` = the sound's rate / device rate * pitch, and added into a 32-bit
// stereo bus; the mixer clamps once, after every voice. It starts and stops the
// way Godot's AudioServer does (core/godot_mix.h). Headless, so
// platform::Audio's callback and the tests run the same code.
#pragma once

#include "core/gain_ramp.h"
#include "core/godot_filter.h"
#include "core/godot_mix.h"
#include "core/ima_adpcm.h"

#include <cstdint>

namespace core {

// What to play and how (the bank's sound, plus the play's own terms).
struct SfxStart {
    AdpcmChannel left;
    AdpcmChannel right;          // bytes null: a mono sound, the same sample both sides
    uint32_t frames = 0;         // per channel
    double step = 1.0;           // sound rate / device rate
    double pitch = 1.0;          // AudioStreamPlayer.pitch_scale
    float gainL = 1.0f;          // Godot's per-channel mix gain (godot::OutputGain)
    float gainR = 1.0f;
    uint32_t startFrame = 0;     // an audio key's start_offset, in source frames
    uint32_t endFrame = 0;       // 0: play to the end; else released here (end_offset)
    bool rampIn = true;          // AudioServer's start ramp (not for a polyphonic add)
    bool linear = false;         // a .wav: AudioStreamPlaybackWAV's linear interpolation
    bool positional = false;     // a 3D player: its attenuation shelf always runs
    float shelf = 1.0f;          // that shelf's gain (1: flat)
    float shelfCutoffHz = 5000.0f;
    float deviceRate = 48000.0f; // for the shelf's coefficients
};

class SfxVoice {
public:
    void Start(const SfxStart& s) {
        stereo_ = s.right.bytes != nullptr;
        frames_ = s.frames;
        endFrame_ = s.endFrame > 0 && s.endFrame < s.frames ? s.endFrame : 0;
        baseStep_ = s.step > 0.0 ? s.step : 1.0;
        step_ = baseStep_ * (s.pitch > 0.01 ? s.pitch : 0.01);
        gain_.Reset(s.gainL, s.gainR);
        deviceRate_ = s.deviceRate;
        shelfCutoff_ = s.shelfCutoffHz;
        positional_ = s.positional;
        if (positional_) {   // from Coeffs' zeros, over the first mix buffer
            shelfL_.Reset();
            shelfR_.Reset();
            shelf_ = -1.0f;   // forces SetShelf to target
            SetShelf(s.shelf);
        }
        rampIn_ = s.rampIn;
        linear_ = s.linear;
        releasing_ = false;
        release_ = 0;
        played_ = 0;
        const uint32_t start = s.startFrame < s.frames ? s.startFrame : s.frames;
        pos_ = static_cast<double>(start);
        base_ = start;
        active_ = start < s.frames;
        if (!active_) return;
        l_.Start(s.left, s.frames, start);
        if (stereo_) r_.Start(s.right, s.frames, start);
    }

    bool Active() const { return active_; }
    void Stop() { active_ = false; }
    // The player's live gain and pitch (a fade, SpeakerController3D's pitch
    // drop, a 3D speaker as the camera moves): the gain is ramped to across
    // one mix buffer, as Godot ramps a playback's volume; the pitch is at once.
    void SetTarget(float gainL, float gainR, double pitch, float shelf = 1.0f) {
        gain_.SetTarget(gainL, gainR);
        step_ = baseStep_ * (pitch > 0.01 ? pitch : 0.01);
        SetShelf(shelf);
    }
    // Godot's stop (stop(), a polyphony cut, a key's end): a linear fade to 0
    // across one mix buffer, then silence.
    void Release() {
        if (!active_ || releasing_) return;
        releasing_ = true;
        release_ = 0;
    }

    // Adds up to `frames` stereo frames into `bus` (interleaved L, R) at
    // `extraGain` on top of the voice's own. Returns the frames written; fewer
    // than asked means the sound ended.
    int Mix(int32_t* bus, int frames, float extraGain = 1.0f) {
        for (int i = 0; i < frames; ++i) {
            const uint32_t idx = static_cast<uint32_t>(pos_);
            if (idx >= frames_ || (releasing_ && release_ >= kGodotMixBuffer)) {
                active_ = false;
                return i;
            }
            if (endFrame_ != 0 && idx >= endFrame_) Release();
            // Slide the window to the samples around the position (pitch
            // above 1 skips samples, below 1 reuses the window).
            while (base_ < idx) {
                l_.Advance(frames_);
                if (stereo_) r_.Advance(frames_);
                ++base_;
            }
            const float mu = static_cast<float>(pos_ - static_cast<double>(idx));
            float g = rampIn_ ? StartRamp(played_) : 1.0f;
            if (played_ < kGodotMixBuffer) ++played_;   // saturates: no wrap, ever
            if (releasing_) {
                g *= 1.0f - static_cast<float>(release_++) / static_cast<float>(kGodotMixBuffer);
            }
            float gainL = 1.0f, gainR = 1.0f;
            gain_.Next(gainL, gainR);
            g *= extraGain;
            float l = l_.At(mu, linear_) * g;
            float r = stereo_ ? r_.At(mu, linear_) * g : l;
            if (positional_) {   // audio_server.cpp:614-640, after the volume
                l = shelfL_.Process(l);
                r = shelfR_.Process(r);
            }
            bus[i * 2 + 0] += static_cast<int32_t>(l * gainL);
            bus[i * 2 + 1] += static_cast<int32_t>(r * gainR);
            pos_ += step_;
        }
        return frames;
    }

private:
    // A new shelf gain is reached across one mix buffer, as Godot interpolates
    // a playback's coefficients every buffer. Only a 3D voice has one.
    void SetShelf(float shelf) {
        if (!positional_ || shelf == shelf_) return;
        shelf_ = shelf;
        const BiquadCoeffs c = GodotHighShelf(shelfCutoff_, 1.0f, shelf_, deviceRate_);
        shelfL_.Target(c, static_cast<int>(kGodotMixBuffer));
        shelfR_.Target(c, static_cast<int>(kGodotMixBuffer));
    }

    // One channel's decoder and its four-sample window: y[1] is the sample at
    // the position, y[0] the one before, y[2] and y[3] the next two. Past the
    // end the last sample repeats.
    struct Channel {
        AdpcmDecoder decoder;
        float y[4] = {0.0f, 0.0f, 0.0f, 0.0f};

        // ADPCM decodes forward only: a start offset is decoded through.
        void Start(AdpcmChannel c, uint32_t frames, uint32_t start) {
            decoder = AdpcmDecoder(c.bytes, c.predictor, c.stepIndex);
            float prev = 0.0f;
            for (uint32_t k = 0; k < start; ++k) prev = decoder.Next();
            y[1] = decoder.Next();
            y[0] = start > 0 ? prev : y[1];
            y[2] = Next(frames, y[1]);
            y[3] = Next(frames, y[2]);
        }
        float Next(uint32_t frames, float hold) {
            return decoder.Decoded() < frames ? static_cast<float>(decoder.Next()) : hold;
        }
        void Advance(uint32_t frames) {
            y[0] = y[1];
            y[1] = y[2];
            y[2] = y[3];
            y[3] = Next(frames, y[3]);
        }
        float At(float mu, bool linear) const {
            return linear ? y[1] + (y[2] - y[1]) * mu : GodotCubic(y[0], y[1], y[2], y[3], mu);
        }
    };

    Channel l_;
    Channel r_;
    bool stereo_ = false;
    bool rampIn_ = true;
    bool linear_ = false;
    bool releasing_ = false;
    uint32_t frames_ = 0;
    uint32_t endFrame_ = 0;
    double pos_ = 0.0;
    double baseStep_ = 1.0;
    double step_ = 1.0;
    GainRamp gain_;
    bool positional_ = false;
    float shelf_ = 1.0f;
    float shelfCutoff_ = 5000.0f;
    float deviceRate_ = 48000.0f;
    InterpBiquad shelfL_;
    InterpBiquad shelfR_;
    uint32_t base_ = 0;
    uint32_t played_ = 0;    // output frames since Start, for StartRamp; stops at kGodotMixBuffer
    uint32_t release_ = 0;   // output frames since Release
    bool active_ = false;
};

}  // namespace core
