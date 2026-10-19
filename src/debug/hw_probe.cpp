#include "debug/hw_probe.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace debug {

namespace {

// A frame that took longer than one vblank plus slack missed it.
constexpr double kMissMs = 17.5;
constexpr std::size_t kSlowestKept = 10;
const char* const kVariantNames[kProbeVariants] = {"base", "dyn", "notex", "batches"};

void Append(std::string& out, const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    out += buf;
    out += '\n';
}

}  // namespace

HwProbe::HwProbe(std::vector<float> freezeAt, int warmupFrames, int framesPerVariant)
    : freezeAt_(std::move(freezeAt)), warmup_(warmupFrames), perVariant_(framesPerVariant) {
    cells_.resize(freezeAt_.size() * kProbeVariants);
}

HwProbe::Directive HwProbe::Next(float roundTime) {
    if (!inBench_ && nextFreeze_ < freezeAt_.size() && roundTime >= freezeAt_[nextFreeze_]) {
        inBench_ = true;
        benchFrame_ = 0;
        benchTimes_.push_back(roundTime);
    }
    current_ = Directive{};
    if (!inBench_) return current_;
    const int perCell = warmup_ + perVariant_;
    const int variant = benchFrame_ / perCell;
    current_.freeze = true;
    current_.vsync = false;
    current_.variant = static_cast<ProbeVariant>(variant);
    return current_;
}

void HwProbe::Record(const ProbeFrame& f) {
    if (inBench_) {
        const int perCell = warmup_ + perVariant_;
        const int variant = benchFrame_ / perCell;
        if (benchFrame_ % perCell >= warmup_) {
            Cell& c = cells_[nextFreeze_ * kProbeVariants + static_cast<std::size_t>(variant)];
            c.sum += f.frameMs;
            c.cpuSum += f.updateMs + f.drawMs;
            c.worst = std::max(c.worst, f.frameMs);
            ++c.frames;
            c.draws = f.draws;
            c.binds = f.binds;
            c.tris = f.tris;
        }
        if (++benchFrame_ >= perCell * kProbeVariants) {
            inBench_ = false;
            ++nextFreeze_;
        }
        return;
    }
    ++playFrames_;
    const bool missed = f.frameMs > kMissMs;
    playMissed_ += missed ? 1 : 0;
    Bucket& b = buckets_[std::min(f.draws / 100, 5)];
    ++b.frames;
    b.missed += missed ? 1 : 0;
    b.sum += f.frameMs;
    b.cpuSum += f.updateMs + f.drawMs;
    if (f.dyn) {
        ++dynFrames_;
        dynMissed_ += missed ? 1 : 0;
        dynSum_ += f.frameMs;
    }
    slowest_.push_back(f);
    std::sort(slowest_.begin(), slowest_.end(),
              [](const ProbeFrame& a, const ProbeFrame& b) { return a.frameMs > b.frameMs; });
    if (slowest_.size() > kSlowestKept) slowest_.resize(kSlowestKept);
}

std::string HwProbe::FormatBench() const {
    std::string out;
    Append(out, "--- bench: vsync OFF, the game frozen, the same frame per variant (ms mean / worst, cpu) ---");
    for (std::size_t z = 0; z < freezeAt_.size(); ++z) {
        const float at = z < benchTimes_.size() ? benchTimes_[z] : freezeAt_[z];
        std::string line;
        char head[64];
        std::snprintf(head, sizeof head, "t=%5.1fs ", static_cast<double>(at));
        line += head;
        for (int v = 0; v < kProbeVariants; ++v) {
            const Cell& c = cells_[z * kProbeVariants + static_cast<std::size_t>(v)];
            char cell[96];
            if (c.frames == 0) {
                std::snprintf(cell, sizeof cell, "| %s -- ", kVariantNames[v]);
            } else {
                std::snprintf(cell, sizeof cell, "| %s %.2f / %.2f cpu %.2f ", kVariantNames[v], c.sum / c.frames,
                              c.worst, c.cpuSum / c.frames);
            }
            line += cell;
        }
        Append(out, "%s", line.c_str());
        const Cell& base = cells_[z * kProbeVariants];
        const Cell& bo = cells_[z * kProbeVariants + 3];
        Append(out, "          draws %d (batches only %d), binds %d, tris %d", base.draws, bo.draws, base.binds,
               base.tris);
    }
    return out;
}

std::string HwProbe::FormatPlay() const {
    std::string out;
    Append(out, "--- play: vsync ON, %d frames, %d missed a vblank (%.1f%%) ---", playFrames_, playMissed_,
           playFrames_ ? 100.0 * playMissed_ / playFrames_ : 0.0);
    for (int i = 0; i < 6; ++i) {
        const Bucket& b = buckets_[i];
        if (b.frames == 0) continue;
        char range[24];
        if (i < 5) std::snprintf(range, sizeof range, "%d-%d", i * 100, i * 100 + 99);
        else std::snprintf(range, sizeof range, "500+");
        Append(out, "draws %s: %d frames, mean %.2f ms, cpu %.2f, %d missed (%.1f%%)", range, b.frames,
               b.sum / b.frames, b.cpuSum / b.frames, b.missed, 100.0 * b.missed / b.frames);
    }
    Append(out, "dyn program: %d frames, mean %.2f ms, %d missed", dynFrames_, dynFrames_ ? dynSum_ / dynFrames_ : 0.0,
           dynMissed_);
    Append(out, "slowest frames:");
    for (const ProbeFrame& f : slowest_) {
        Append(out, "  %.2f ms: update %.2f draw %.2f swap %.2f, %d draws, %d binds, %d tris%s, t=%.1fs", f.frameMs,
               f.updateMs, f.drawMs, f.swapMs, f.draws, f.binds, f.tris, f.dyn ? ", dyn" : "",
               static_cast<double>(f.time));
    }
    return out;
}

}  // namespace debug
