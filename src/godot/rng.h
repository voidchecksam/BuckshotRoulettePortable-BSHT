// Godot 4.1's RandomPCG and the global random functions GDScript calls.
//
// Transcribed from core/math/random_pcg.{h,cpp} and thirdparty/misc/pcg.cpp at
// 4.1.4-stable. Every random decision in the translated scripts (the dealer's
// coin flip, shell order, item draws, expired medicine) must go through this,
// and parity_rng diffs it against the real engine.
#pragma once

#include <chrono>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace godot {

class RandomPCG {
public:
    static constexpr uint64_t kDefaultSeed = 12047754176567800795ULL;
    static constexpr uint64_t kDefaultInc  = 1442695040888963407ULL;   // PCG_DEFAULT_INC_64

    explicit RandomPCG(uint64_t seed = kDefaultSeed, uint64_t inc = kDefaultInc)
        : current_inc_(inc) {
        this->seed(seed);
    }

    // pcg32_srandom_r(&pcg, seed, current_inc).
    void seed(uint64_t s) {
        current_seed_ = s;
        state_ = 0u;
        inc_ = (current_inc_ << 1u) | 1u;
        rand();
        state_ += s;
        rand();
    }
    uint64_t get_seed() const { return current_seed_; }

    // random_pcg.cpp:40 at 4.1.4:
    //   seed((get_unix_time() + get_ticks_usec()) * pcg.state + PCG_DEFAULT_INC_64)
    // OS::get_unix_time() is whole seconds since the epoch (system_clock);
    // OS::get_ticks_usec() is microseconds since the engine started (steady_clock,
    // measured from the first call in this process).
    void randomize() {
        using namespace std::chrono;
        static const steady_clock::time_point start = steady_clock::now();
        const uint64_t unixTime =
            static_cast<uint64_t>(duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
        const uint64_t ticksUsec =
            static_cast<uint64_t>(duration_cast<microseconds>(steady_clock::now() - start).count());
        seed((unixTime + ticksUsec) * state_ + kDefaultInc);
    }

    // pcg32_random_r: XSH RR on the OLD state.
    uint32_t rand() {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + (inc_ | 1u);
        const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
    }

    // pcg32_boundedrand_r: rejection sampling, uniform in [0, bound).
    uint32_t rand(uint32_t bound) {
        const uint32_t threshold = (0u - bound) % bound;
        for (;;) {
            const uint32_t r = rand();
            if (r >= threshold) return r % bound;
        }
    }

    float randf() {
        const uint32_t proto = rand();
        if (proto == 0u) return 0.0f;
        return std::ldexp(static_cast<float>(rand() | 0x80000001u),
                          -32 - std::countl_zero(proto));
    }

    // PORT: Godot writes `((uint64_t)rand() << 32) | rand()`, whose operand
    // order C++ leaves unspecified. High word first here; parity_rng pins it.
    double randd() {
        const uint32_t proto = rand();
        if (proto == 0u) return 0.0;
        const uint64_t hi = rand();
        const uint64_t lo = rand();
        const uint64_t significand = (hi << 32u) | lo | 0x8000000000000001ULL;
        return std::ldexp(static_cast<double>(significand),
                          -64 - std::countl_zero(proto));
    }

    int random(int from, int to) {
        if (from == to) return from;
        return static_cast<int>(rand(static_cast<uint32_t>(std::abs(from - to) + 1))) +
               (from < to ? from : to);
    }
    double random(double from, double to) { return randd() * (to - from) + from; }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 0;
    uint64_t current_seed_ = 0;
    uint64_t current_inc_ = kDefaultInc;
};

// Math::default_rand. One per process, as in Godot.
inline RandomPCG& default_rand() {
    static RandomPCG r;
    return r;
}

// The GDScript utility functions, as variant_utility.cpp maps them.
inline void    seed(uint64_t s) { default_rand().seed(s); }
// Math::randomize (math_funcs.cpp:48). The GDScript randomize() and, once per
// scene tree, SceneTree's constructor (scene_tree.cpp:1696).
inline void    randomize() { default_rand().randomize(); }
inline int64_t randi() { return default_rand().rand(); }

// Math::randf() (math_funcs.h:507), not RandomPCG::randf(): one draw over
// RANDOM_32BIT_MAX in float. The two-draw class method would shift the stream
// by one value per call (parity_rng checks it).
inline double randf() {
    return static_cast<float>(default_rand().rand()) / static_cast<float>(0xFFFFFFFFu);
}
inline int64_t randi_range(int64_t from, int64_t to) {
    return default_rand().random(static_cast<int>(from), static_cast<int>(to));
}
inline double randf_range(double from, double to) { return default_rand().random(from, to); }

}  // namespace godot
