// The hardware probe: everything one device run should answer.
//
// 1. A bench early in the first round: at each freeze time the game stops
//    updating, vsync goes off, and the same frame is drawn under four variants
//    (normal, the animated-light DYN program forced on, textures off for the
//    baked pages' fetch cost, props off for the draw-call cost), warm-up
//    frames first, then measured ones.
// 2. Play statistics for the rest of the run, vsync on: every frame's update /
//    draw-submit / swap split, frame time bucketed by draw count, the slowest
//    frames with their split, and how often the DYN program ran.
// Headless: the caller times the frame and applies the directive.
#pragma once

#include <string>
#include <vector>

namespace debug {

enum class ProbeVariant { Base = 0, DynForced = 1, NoTextures = 2, BatchesOnly = 3 };
inline constexpr int kProbeVariants = 4;

struct ProbeFrame {
    double updateMs = 0.0;
    double drawMs = 0.0;    // scene + interface submission, before the swap
    double swapMs = 0.0;    // the swap: GPU catch-up and, with vsync, the wait
    double frameMs = 0.0;
    int draws = 0;
    int binds = 0;
    int tris = 0;
    bool dyn = false;       // the DYN program was bound
    float time = 0.0f;      // seconds into the round
};

class HwProbe {
public:
    HwProbe(std::vector<float> freezeAt, int warmupFrames, int framesPerVariant);

    struct Directive {
        bool freeze = false;    // do not update the game this frame
        bool vsync = true;
        ProbeVariant variant = ProbeVariant::Base;
    };
    // Call once a frame, before updating, with the time into the round.
    Directive Next(float roundTime);
    // Call once a frame after the swap with that frame's numbers.
    void Record(const ProbeFrame& f);

    bool BenchDone() const { return nextFreeze_ >= freezeAt_.size() && !inBench_; }
    std::string FormatBench() const;
    std::string FormatPlay() const;

private:
    struct Cell {
        double sum = 0.0, worst = 0.0, cpuSum = 0.0;
        int frames = 0, draws = 0, binds = 0, tris = 0;
    };
    struct Bucket {
        int frames = 0, missed = 0;
        double sum = 0.0, cpuSum = 0.0;
    };

    std::vector<float> freezeAt_;
    int warmup_;
    int perVariant_;
    std::size_t nextFreeze_ = 0;
    bool inBench_ = false;
    int benchFrame_ = 0;        // frames into the current freeze
    Directive current_;
    std::vector<Cell> cells_;   // freeze-major, variant-minor
    std::vector<float> benchTimes_;

    Bucket buckets_[6];         // draws: <100, 100-199, ..., >=500
    std::vector<ProbeFrame> slowest_;
    int playFrames_ = 0, playMissed_ = 0, dynFrames_ = 0, dynMissed_ = 0;
    double dynSum_ = 0.0;
};

}  // namespace debug
