// The CPU side of GPUParticles3D: Godot simulates particles on the GPU with
// ParticleProcessMaterial's shader; the port runs the same lifecycle on the
// CPU and draws camera-facing quads. Per particle i of `amount`:
//   restart phase rp = i / amount * (1 - explosiveness)   (particles.glsl)
//   it is (re)born at system time (k + rp) * lifetime, k = 0, 1, ... (k = 0
//   only for one_shot), from the emitter's transform at that moment
//   (local_coords off), with a direction inside `spread` of `direction`
//   (process_particles: the spread cone) and a speed in [velocity min, max];
//   position = origin + v t + g t^2 / 2 (no damping in any of the systems),
//   size = mix(scale min, max, r) * scale_curve(t / lifetime).
// Randomness comes from the seed the restart drew (random_seed), hashed per
// particle and cycle, never the global RNG, which Godot does not touch here.
#pragma once

#include "godot/particles.h"
#include "godot/scene_tree.h"

#include <cstdint>
#include <vector>

namespace godot {

struct ParticleQuad {
    float pos[3];
    float size;      // world units, the full quad edge
    int system;      // index into ParticleSim::Systems()
};

class ParticleSim {
public:
    // Every GPUParticles3D in the tree with a look; call again after frees.
    void Bind(const SceneTree& tree);
    // Advances every system to its node's state now and lists the live quads.
    void Step();
    const std::vector<ParticleQuad>& Quads() const { return quads_; }
    struct System {
        const GPUParticles3D* node = nullptr;
        uint32_t restarts = ~0u;
        struct P {
            int cycle = -1;
            double birth = 0.0;
            float origin[3] = {0, 0, 0};
            float vel[3] = {0, 0, 0};
            float scaleRand = 0.0f;
        };
        std::vector<P> particles;
    };
    const std::vector<System>& Systems() const { return systems_; }

private:
    void Spawn(System& s, std::size_t i, int cycle, double birth) const;
    std::vector<System> systems_;
    std::vector<ParticleQuad> quads_;
};

}  // namespace godot
