#include "godot/particles.h"

#include "godot/rng.h"

#include <cmath>

namespace godot {

// particles_storage.cpp:1377-1525 (update_particles' body for one system).
void GPUParticles3D::update(double frame_delta, bool zero_time_scale) {
    dirty = false;
    if (restart_request_) {
        phase_ = 0.0;
        clear_ = true;
        restart_request_ = false;
    }
    if (inactive_ && !emitting_) return;
    if (emitting_) {
        if (inactive_) {   // restart system from scratch
            phase_ = 0.0;
            clear_ = true;
        }
        inactive_ = false;
        inactive_time_ = 0.0;
    } else {
        inactive_time_ += speed_scale_ * frame_delta;
        if (inactive_time_ > lifetime_ * 1.2) {
            inactive_ = true;
            return;
        }
    }

    if (clear_ && pre_process_time_ > 0.0) {
        const double frame_time = fixed_fps_ > 0 ? 1.0 / fixed_fps_ : 1.0 / 30.0;
        double todo = pre_process_time_;
        while (todo >= 0) {
            process(frame_time);
            todo -= frame_time;
        }
    }

    if (fixed_fps_ > 0) {
        const double frame_time = zero_time_scale ? 0.0 : 1.0 / fixed_fps_;
        const double decr = 1.0 / fixed_fps_;
        double delta = frame_delta;
        if (delta > 0.1) {
            delta = 0.1;   // avoid recursive stalls if fps goes below 10
        } else if (delta <= 0.0) {
            delta = 0.001;
        }
        double todo = frame_remainder_ + delta;
        while (todo >= frame_time || clear_) {
            process(frame_time);
            todo -= decr;
        }
        frame_remainder_ = todo;
    } else {
        process(zero_time_scale ? 0.0 : frame_delta);
    }
}

// particles_storage.cpp:778-795 and :1113. The draw is the whole point: one
// Math::rand() per clear.
void GPUParticles3D::process(double delta) {
    const double new_phase = std::fmod(phase_ + (delta / lifetime_) * speed_scale_, 1.0);
    if (clear_) {
        seed_ = default_rand().rand();   // p_particles->random_seed = Math::rand()
        ++restarts_;
        time_ = 0.0;
        emit_end_ = 1e300;
    } else if (new_phase < phase_) {
        if (one_shot_) emitting_ = false;
    }
    phase_ = new_phase;
    if (emitting_) {
        emit_end_ = 1e300;
    } else if (emit_end_ > time_) {
        emit_end_ = time_;   // emission stopped at the start of this step
    }
    time_ += delta * speed_scale_;
    clear_ = false;
}

}  // namespace godot
