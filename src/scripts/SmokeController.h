// source: scripts/SmokeController.gd (15 lines)
// PORT deviations:
//   - godot::GPUParticles3D also models what a restart does to the GLOBAL RNG
//     in Godot's renderer (godot/particles.h), which every random value after a
//     shot depends on. The smoke itself is godot::ParticleSim's.
#pragma once

#include "godot/node.h"
#include "godot/particles.h"
#include "godot/script.h"

#include <string>

namespace scripts {

class SmokeController : public godot::Script {
public:
    static constexpr const char* kClassName = "SmokeController";
    const char* get_class_name() const override { return kClassName; }

    godot::GPUParticles3D* particle_barrel = nullptr;
    godot::GPUParticles3D* particle_chamber = nullptr;

    void _ready() override;                    // gd: L6-L8
    void SpawnSmoke(std::string alias);        // gd: L10-L15
};

}  // namespace scripts
