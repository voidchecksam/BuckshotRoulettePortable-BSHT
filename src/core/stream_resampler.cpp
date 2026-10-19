#include "core/stream_resampler.h"

namespace core {

void StreamResampler::Reset(int channels, double step) {
    channels_ = channels < 1 ? 1 : channels;
    step_ = step > 0.0 ? step : 1.0;
    buf_.clear();
    // The most the buffer ever holds (a chunk plus the frames a Fill keeps),
    // reserved here at stream open, off the audio thread, so the callback
    // never grows it.
    buf_.reserve(static_cast<std::size_t>(kChunkFrames + kKeptFrames) * channels_);
    have_ = 0;
    pos_ = 0.0;
    consumed_ = 0.0;
    ended_ = false;
}

}  // namespace core
