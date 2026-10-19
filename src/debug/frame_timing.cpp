#include "debug/frame_timing.h"

#include <algorithm>
#include <cmath>

namespace debug {

void FrameTiming::Add(double ms) {
    samples_.push_back(ms);
    total_ += ms;
    if (ms > worst_) worst_ = ms;
}

double FrameTiming::MeanMs() const {
    return samples_.empty() ? 0.0 : total_ / static_cast<double>(samples_.size());
}

double FrameTiming::PercentileMs(double p) const {
    if (samples_.empty()) return 0.0;
    std::vector<double> sorted = samples_;
    std::sort(sorted.begin(), sorted.end());
    const double rank = std::ceil(p * static_cast<double>(sorted.size()));
    const std::size_t i = rank < 1.0 ? 0 : static_cast<std::size_t>(rank) - 1;
    return sorted[std::min(i, sorted.size() - 1)];
}

std::size_t FrameTiming::CountOver(double ms) const {
    return static_cast<std::size_t>(
        std::count_if(samples_.begin(), samples_.end(), [ms](double s) { return s > ms; }));
}

}  // namespace debug
