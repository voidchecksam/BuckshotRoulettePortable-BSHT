// The per-system frame profiler. Fixed sections, each a sum of the scopes
// timed into it this frame; EndFrame folds the frame into a window kept as mean
// and worst. GameApp logs Format() every few seconds, which reaches the desktop
// console and the Vita report alike:
//
//   prof: 600 frames (ms mean/max) update 2.10/4.30 tree 1.50/3.10 anim 0.40/0.90 ...
//
// Sections nest (anim is inside tree, tree inside update): each one is the
// inclusive time of its own scopes. Headless; the clock is injected, so tests
// drive it exactly and the device can hand it its own microsecond counter.
//
//   core::ProfScope scope(profiler, core::ProfSection::Props);   // null: no-op
//
// Allocations: an executable that counts its operator new calls
// (platform/alloc_count.cpp) registers the count here before main; GameApp
// hands it to SetAllocCounter and the line gains "allocs mean/max" per frame.
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace core {

enum class ProfSection : uint8_t {
    Update,       // GameApp::Update, all of it
    Tree,         // SceneTree's node pass: scripts' _process + internal process
    Animation,    //   AnimationPlayers' internal process (inside Tree)
    Coroutines,   // seq::Scheduler::Tick: every await's continuation
    Deferred,     // call_deferred flushes and the delete queue
    ParticleSim,  // SceneTree::render_step: particle restarts and simulation
    AudioSync,    // MusicRouter / StreamRouter polling
    Draw,         // DrawScene + DrawInterface: CPU submission
    Props,        //   SceneRenderer: batches and props
    Skinning,     //     CPU skinning (inside Props)
    Particles,    //   ParticleRenderer
    Labels,       //   Label3DRenderer
    Ui,           //   the 2D interface
    kCount
};

inline const char* ProfSectionName(ProfSection s) {
    static constexpr const char* kNames[] = {"update", "tree", "anim", "seq",    "deferred", "psim", "audio",
                                             "draw",   "props", "skin", "parts", "labels",   "ui"};
    static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<std::size_t>(ProfSection::kCount));
    return kNames[static_cast<std::size_t>(s)];
}

inline uint64_t SteadyMicros() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

// The executable's allocation counter, if it registered one. Single writer
// (a static initialiser, before main); read once when GameApp starts.
using AllocCounter = uint32_t (*)();
inline AllocCounter& RegisteredAllocCounter() {
    static AllocCounter counter = nullptr;
    return counter;
}

class Profiler {
public:
    using Clock = uint64_t (*)();
    explicit Profiler(Clock now = &SteadyMicros) : now_(now) {}

    uint64_t Now() const { return now_(); }
    // Counts from here on; null detaches it.
    void SetAllocCounter(AllocCounter counter) {
        allocs_ = counter;
        lastAllocs_ = counter ? counter() : 0;
    }
    void Add(ProfSection s, uint64_t micros) { frame_[Index(s)] += micros; }

    void EndFrame() {
        for (std::size_t i = 0; i < kSections; ++i) {
            sum_[i] += frame_[i];
            if (frame_[i] > max_[i]) max_[i] = frame_[i];
            frame_[i] = 0;
        }
        if (allocs_) {
            const uint32_t now = allocs_();
            const uint32_t frame = now - lastAllocs_;   // unsigned: a wrap is not a jump
            lastAllocs_ = now;
            allocSum_ += frame;
            if (frame > allocMax_) allocMax_ = frame;
        }
        ++frames_;
    }
    void ResetWindow() {
        sum_.fill(0);
        max_.fill(0);
        frame_.fill(0);
        frames_ = 0;
        allocSum_ = 0;
        allocMax_ = 0;
    }

    int Frames() const { return frames_; }
    double MeanMs(ProfSection s) const {
        return frames_ > 0 ? static_cast<double>(sum_[Index(s)]) / frames_ / 1000.0 : 0.0;
    }
    double MaxMs(ProfSection s) const { return static_cast<double>(max_[Index(s)]) / 1000.0; }
    double MeanAllocs() const { return frames_ > 0 ? static_cast<double>(allocSum_) / frames_ : 0.0; }
    uint32_t MaxAllocs() const { return allocMax_; }

    // One line, sections that never ran left out.
    std::string Format() const {
        std::string line = "prof: " + std::to_string(frames_) + " frames (ms mean/max)";
        char buf[64];
        for (std::size_t i = 0; i < kSections; ++i) {
            if (max_[i] == 0) continue;
            const auto s = static_cast<ProfSection>(i);
            std::snprintf(buf, sizeof buf, " %s %.2f/%.2f", ProfSectionName(s), MeanMs(s), MaxMs(s));
            line += buf;
        }
        if (allocs_) {
            std::snprintf(buf, sizeof buf, " allocs %.1f/%lu", MeanAllocs(), static_cast<unsigned long>(allocMax_));
            line += buf;
        }
        return line;
    }

private:
    static constexpr std::size_t kSections = static_cast<std::size_t>(ProfSection::kCount);
    static std::size_t Index(ProfSection s) { return static_cast<std::size_t>(s); }

    Clock now_;
    std::array<uint64_t, kSections> frame_{};
    std::array<uint64_t, kSections> sum_{};
    std::array<uint64_t, kSections> max_{};
    int frames_ = 0;
    AllocCounter allocs_ = nullptr;
    uint32_t lastAllocs_ = 0;
    uint64_t allocSum_ = 0;
    uint32_t allocMax_ = 0;
};

// Times its own lifetime into one section. A null profiler costs a branch.
class ProfScope {
public:
    ProfScope(Profiler* p, ProfSection s) : p_(p), s_(s), start_(p ? p->Now() : 0) {}
    ~ProfScope() {
        if (p_) p_->Add(s_, p_->Now() - start_);
    }
    ProfScope(const ProfScope&) = delete;
    ProfScope& operator=(const ProfScope&) = delete;

private:
    Profiler* p_;
    ProfSection s_;
    uint64_t start_;
};

}  // namespace core
