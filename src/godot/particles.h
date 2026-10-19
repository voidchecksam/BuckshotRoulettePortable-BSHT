// GPUParticles3D's renderer-side state, which decides when Godot draws the
// global RNG for a system that restarts (the simulation and the look are
// godot/particle_sim.h). Transcribed from
// servers/rendering/renderer_rd/storage_rd/particles_storage.{h,cpp} at
// 4.1.4-stable, because which frame the draw lands on decides every random
// value after it: the dealer's medicine roll follows Godot's only with these
// draws in place.
//
// Frame order (rendering_server_default.cpp:83-91): update_particles() runs
// before the viewports are culled, and the cull queues a visible system for
// the next frame's update. SceneTree::render_step drives both, in that order,
// after the frame's scripts and timers.
#pragma once

#include "godot/node.h"

#include <vector>

namespace godot {

class GPUParticles3D : public GeometryInstance3D {
public:
    static constexpr uint32_t kClassBit = kClassGPUParticles3D;
    explicit GPUParticles3D(std::string name)
        : GeometryInstance3D(std::move(name), kClassGPUParticles3D) { set_class("GPUParticles3D"); }

    // gpu_particles_3d.cpp:373: particles_restart, then particles_set_emitting(true).
    // What the CPU billboards draw (export_runtime_scene.gd _particle_look).
    // Curves are 9 even samples over the lifetime.
    struct Look {
        int amount = 0;
        float explosiveness = 0.0f, randomness = 0.0f;
        bool local_coords = false;
        float direction[3] = {1.0f, 0.0f, 0.0f};
        float spread = 45.0f;
        float velocity[2] = {0.0f, 0.0f};
        float gravity[3] = {0.0f, -9.8f, 0.0f};
        float scale[2] = {1.0f, 1.0f};
        float scale_curve[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
        float size[2] = {1.0f, 1.0f};
        float albedo[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        std::string texture;
        bool unshaded = false;
    };
    const Look& get_look() const { return look_; }
    void set_look(const Look& l) { look_ = l; }
    // Moves on every clear (restart): the CPU simulation starts over.
    uint32_t get_restart_count() const { return restarts_; }
    // The seed the clear drew (random_seed = Math::rand()), for the simulation.
    uint32_t get_random_seed() const { return seed_; }
    // Scaled seconds the system has run since its last clear.
    double get_system_time() const { return time_; }
    // The system time emission stopped at (emitting turned off, or a one-shot
    // ran its cycle); +infinity while it emits. A particle born before it was
    // emitted, however seldom the simulation looks (a 0.3 s window can fall
    // between two looks).
    double get_emit_end_time() const { return emit_end_; }

    void restart() {
        restart_request_ = true;
        emitting_ = true;
    }
    // gpu_particles_3d.cpp:39 forwards straight to the storage.
    void set_emitting(bool e) { emitting_ = e; }
    bool is_emitting() const { return emitting_; }

    void   set_one_shot(bool o) { one_shot_ = o; }
    bool   get_one_shot() const { return one_shot_; }
    void   set_lifetime(double l) { lifetime_ = l; }
    double get_lifetime() const { return lifetime_; }
    void   set_speed_scale(double s) { speed_scale_ = s; }
    void   set_fixed_fps(int fps) { fixed_fps_ = fps; }
    void   set_pre_process_time(double t) { pre_process_time_ = t; }
    // The visibility AABB, local: the culled instance's bounds
    // (particles_get_aabb returns custom_aabb).
    void set_visibility_aabb(const Vector3& position, const Vector3& size) {
        aabb_pos_ = position;
        aabb_size_ = size;
    }
    const Vector3& get_visibility_aabb_position() const { return aabb_pos_; }
    const Vector3& get_visibility_aabb_size() const { return aabb_size_; }

    // ParticlesStorage::update_particles for this system (:1377-1525). Returns
    // true when it was processed (it may then be queued again by the cull).
    void update(double frame_delta, bool zero_time_scale);
    // renderer_scene_cull.cpp:2791: `!emitting && inactive` is not queued.
    bool wants_process() const { return !(!emitting_ && inactive_); }
    bool dirty = false;   // Particles::dirty: on the update list

private:
    void process(double delta);   // _particles_process, the parts that matter

    // GPUParticles3D::GPUParticles3D (gpu_particles_3d.cpp:628-647).
    bool   emitting_ = true;
    bool   one_shot_ = false;
    double lifetime_ = 1.0;
    double speed_scale_ = 1.0;
    int    fixed_fps_ = 30;
    double pre_process_time_ = 0.0;
    Vector3 aabb_pos_{-4.0f, -4.0f, -4.0f};
    Vector3 aabb_size_{8.0f, 8.0f, 8.0f};

    // ParticlesStorage::Particles defaults (particles_storage.h:151-225).
    bool   inactive_ = true;
    double inactive_time_ = 0.0;
    bool   restart_request_ = false;
    double phase_ = 0.0;
    double frame_remainder_ = 0.0;
    bool   clear_ = true;
    Look   look_;
    uint32_t restarts_ = 0;
    uint32_t seed_ = 0;
    double time_ = 0.0;
    double emit_end_ = 1e300;
};

}  // namespace godot
