// godot::ParticleSim: GPUParticles3D's lifecycle on the CPU. These pin how many
// particles a system switched on by `emitting = true` (as the cigarette's
// animation does) keeps alive.
#include "godot/particle_sim.h"

#include "godot/particles.h"
#include "godot/scene_tree.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <memory>

namespace {

// GPU particles process only once the camera's cull has seen them
// (SceneTree::render_step): a current camera at the origin looking down -Z,
// the system 3 units in front of it.
void AddCamera(godot::SceneTree& tree) {
    auto* cam = static_cast<godot::Camera3D*>(
        tree.add_node(std::make_unique<godot::Camera3D>("Camera"), tree.get_root()));
    cam->set_current(true);
}

godot::GPUParticles3D* AddSystem(godot::SceneTree& tree, int amount, double lifetime, float explosiveness) {
    auto* p = static_cast<godot::GPUParticles3D*>(
        tree.add_node(std::make_unique<godot::GPUParticles3D>("smoke"), tree.get_root()));
    godot::GPUParticles3D::Look look;
    look.amount = amount;
    look.explosiveness = explosiveness;
    look.velocity[0] = look.velocity[1] = 1.0f;
    look.texture = "res://textures/smoke.png";
    p->set_look(look);
    p->set_lifetime(lifetime);
    p->set_one_shot(false);
    p->set_emitting(false);
    p->set_position({0.0f, 0.0f, -3.0f});
    return p;
}

void Frames(godot::SceneTree& tree, int n) {
    for (int i = 0; i < n; ++i) tree.process_frame(1.0 / 60.0);
}

}  // namespace

TEST(ParticleSim, ALoopingSystemSwitchedOnKeepsItsAmountAlive) {
    godot::SceneTree tree;
    AddCamera(tree);
    godot::GPUParticles3D* p = AddSystem(tree, 26, 0.41, 0.0f);
    godot::ParticleSim sim;
    sim.Bind(tree);
    p->set_emitting(true);    // the animation's `emitting` key
    Frames(tree, 60);         // a second: well past one lifetime
    sim.Step();
    EXPECT_GE(sim.Quads().size(), 20u);
    EXPECT_LE(sim.Quads().size(), 26u);
}

TEST(ParticleSim, ShortEmissionStillEmitsWhatItsWindowAllows) {
    // The cigarette: emitting for 0.3 s of a 0.88 s lifetime, 26 particles.
    godot::SceneTree tree;
    AddCamera(tree);
    godot::GPUParticles3D* p = AddSystem(tree, 26, 0.88, 0.21f);
    godot::ParticleSim sim;
    sim.Bind(tree);
    p->set_emitting(true);
    Frames(tree, 18);         // 0.3 s
    p->set_emitting(false);
    Frames(tree, 6);
    sim.Step();
    EXPECT_GE(sim.Quads().size(), 6u) << "26 / 0.88 s * (1 - 0.21) over 0.3 s of emission";
}
