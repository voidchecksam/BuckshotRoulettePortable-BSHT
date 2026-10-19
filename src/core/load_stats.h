// What a background loader did, as one report line: how many loads finished,
// were dropped (superseded while loading) or failed, the last and peak load
// time, and the last path that failed. platform::Audio's stream loader keeps
// one, since its printf never reaches the Vita's report.
//
// Not thread-safe by itself: whoever holds it locks around every call.
#pragma once

#include <algorithm>
#include <cstdio>
#include <string>

namespace core {

class LoadStats {
public:
    void Loaded(double ms) {
        ++loaded_;
        lastMs_ = ms;
        peakMs_ = std::max(peakMs_, ms);
    }
    void Dropped() { ++dropped_; }
    void Failed(const std::string& path) {
        ++failed_;
        lastFailure_ = path;
    }

    std::string Format() const {
        char line[160];
        std::snprintf(line, sizeof(line), "streams: %d loaded, %d dropped, %d failed", loaded_, dropped_, failed_);
        std::string out = line;
        if (loaded_ > 0) {
            std::snprintf(line, sizeof(line), ", last %.0f ms, peak %.0f ms off the frame", lastMs_, peakMs_);
            out += line;
        }
        if (!lastFailure_.empty()) out += ", last failure " + lastFailure_;
        return out;
    }

private:
    int loaded_ = 0;
    int dropped_ = 0;
    int failed_ = 0;
    double lastMs_ = 0.0;
    double peakMs_ = 0.0;
    std::string lastFailure_;
};

}  // namespace core
