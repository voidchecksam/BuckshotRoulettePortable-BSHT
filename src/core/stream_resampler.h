// A streamed sound's resampler: pulls interleaved 16-bit frames from a decoder
// at the file's rate and renders them at the device's rate with Godot's cubic
// (core::GodotCubic), keeping its fractional place across buffers so no source
// frame is dropped between callbacks. Headless: the decoder is a callback.
#pragma once

#include "core/godot_mix.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace core {

class StreamResampler {
public:
    // `step` = source rate / output rate.
    void Reset(int channels, double step);

    // Renders `frames` output frames as interleaved stereo floats (a mono
    // source on both sides). Returns the frames rendered; fewer than asked
    // only when the source ended. `pull(int16_t* dst, int maxFrames)` writes up
    // to maxFrames interleaved frames and returns how many (0: ended). A
    // template, not a std::function: this runs in the audio callback, and on
    // ARM32 the device's pull outgrows std::function's inline buffer, a heap
    // allocation per live stream per callback.
    template <class Pull>
    int Render(int frames, Pull&& pull, float* outLR) {
        int i = 0;
        for (; i < frames; ++i) {
            // Godot's cubic needs the frame before the current one and the
            // two after it.
            while (static_cast<int>(pos_) + 2 >= have_) {
                if (!Fill(pull)) break;
            }
            const int i0 = static_cast<int>(pos_);
            if (i0 >= have_) break;   // ended and played out
            const float t = static_cast<float>(pos_ - i0);
            const float l = Cubic(i0, 0, t);
            const float r = channels_ > 1 ? Cubic(i0, 1, t) : l;
            outLR[i * 2] = l;
            outLR[i * 2 + 1] = r;
            pos_ += step_;
        }
        return i;
    }

    // The source frame being played, counted from the start (tests).
    double Position() const { return consumed_ + pos_; }

private:
    static constexpr int kChunkFrames = 2048;
    // Fill runs once fewer than three frames are left past the current one,
    // and keeps the frame before it: at most this many frames survive a Fill.
    static constexpr int kKeptFrames = 3;

    // Channel `ch` at `i0 + t` through core::GodotCubic; the window's ends
    // clamp to the buffer (the first frame of a stream, the last of a one-shot).
    float Cubic(int i0, int ch, float t) const {
        const auto at = [this, ch](int k) {
            k = k < 0 ? 0 : (k >= have_ ? have_ - 1 : k);
            return static_cast<float>(buf_[static_cast<std::size_t>(k) * channels_ + ch]);
        };
        return GodotCubic(at(i0 - 1), at(i0), at(i0 + 1), at(i0 + 2), t);
    }

    // Drops the frames already played (keeping the one before the current
    // frame, which the cubic reads) and appends a chunk from the source.
    template <class Pull>
    bool Fill(Pull& pull) {
        if (ended_) return false;
        const int keepFrom = std::clamp(static_cast<int>(pos_) - 1, 0, have_);
        const int keep = have_ - keepFrom;
        if (keepFrom > 0 && keep > 0) {
            std::memmove(buf_.data(), buf_.data() + static_cast<std::size_t>(keepFrom) * channels_,
                         static_cast<std::size_t>(keep) * channels_ * sizeof(int16_t));
        }
        consumed_ += keepFrom;
        pos_ -= keepFrom;
        have_ = keep;
        buf_.resize(static_cast<std::size_t>(have_ + kChunkFrames) * channels_);
        const int got = pull(buf_.data() + static_cast<std::size_t>(have_) * channels_, kChunkFrames);
        if (got <= 0) {
            ended_ = true;
            return false;
        }
        have_ += got;
        return true;
    }

    std::vector<int16_t> buf_;
    int have_ = 0;          // frames in buf_
    double pos_ = 0.0;      // fractional frame within buf_
    double consumed_ = 0.0; // frames dropped off the front of buf_ so far
    int channels_ = 2;
    double step_ = 1.0;
    bool ended_ = false;
};

}  // namespace core
