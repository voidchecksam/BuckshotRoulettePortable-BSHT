// A frame-time distribution: mean, worst, a percentile and a miss count.
//
// Under vsync every good frame reads 16.67 ms, so a mean hides a third of the
// frames missing; the probe needs the tail. Keeps every sample (a few thousand
// doubles per measurement) so the percentile is exact, not estimated.
#pragma once

#include <cstddef>
#include <vector>

namespace debug {

class FrameTiming {
public:
    void Add(double ms);
    void Clear() { samples_.clear(); total_ = 0.0; worst_ = 0.0; }

    std::size_t Frames() const { return samples_.size(); }
    double MeanMs() const;
    double WorstMs() const { return worst_; }
    // The sample at rank ceil(p * n) of the sorted set (p in (0,1]).
    double PercentileMs(double p) const;
    std::size_t CountOver(double ms) const;

private:
    std::vector<double> samples_;
    double total_ = 0.0;
    double worst_ = 0.0;
};

}  // namespace debug
