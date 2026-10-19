// GPUParticles3D's one visible effect on the game: the renderer draws the
// global RNG. servers/rendering/renderer_rd/storage_rd/particles_storage.cpp
// at 4.1.4-stable, _particles_process:789: `random_seed = Math::rand()`
// whenever a system is processed with `clear` set, i.e. after a restart.
// Which frame that lands on is decided by update_particles (:1368), which runs
// before the viewports are culled (rendering_server_default.cpp:83-91), and by
// the cull (renderer_scene_cull.cpp:2791), which queues only visible systems
// that are not `!emitting && inactive`. The dealer's medicine roll depends on
// exactly these draws (dealer_medicine_b2r1).
#include "godot/node.h"
#include "godot/particles.h"
#include "godot/rng.h"
#include "godot/scene_tree.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

constexpr uint64_t kSeed = 20260922;

// How many raw draws the global RNG made since seed(kSeed): the next value
// is looked up in a fresh stream.
int DrawsSinceSeed() {
    const uint32_t next = godot::default_rand().rand();
    godot::RandomPCG fresh;
    fresh.seed(kSeed);
    for (int i = 0; i < 64; ++i) {
        if (fresh.rand() == next) return i;
    }
    return -1;
}

struct ParticleTree : ::testing::Test {
    godot::SceneTree t;
    godot::Node* root = nullptr;
    godot::GPUParticles3D* smoke = nullptr;

    void SetUp() override {
        root = t.add_node(std::make_unique<godot::Node3D>("main"), nullptr);
        auto* cam = static_cast<godot::Camera3D*>(t.add_node(std::make_unique<godot::Camera3D>("Camera"), root));
        cam->set_current(true);   // at the origin, looking down -Z
        smoke = static_cast<godot::GPUParticles3D*>(
            t.add_node(std::make_unique<godot::GPUParticles3D>("particle_smoke muzzle"), root));
        smoke->set_position({0.0f, 0.0f, -5.0f});   // in front of the camera
        smoke->set_one_shot(true);
        smoke->set_emitting(false);
        t.set_particle_rng(true);
        // Let the constructor's `clear` be spent and the system go inactive,
        // as it is long before any restart in the game.
        for (int i = 0; i < 120; ++i) t.process_frame(1.0 / 60.0);
        godot::seed(kSeed);
    }
    void Frame() { t.process_frame(1.0 / 60.0); }
};

}  // namespace

// An inactive system is not on the update list, so the restart is processed
// one frame later: the cull of the restart frame queues it (it emits now).
TEST_F(ParticleTree, ARestartDrawsOnceOnTheNextFrame) {
    smoke->restart();
    Frame();
    EXPECT_EQ(DrawsSinceSeed(), 0) << "not yet: it was not on the update list";
    godot::seed(kSeed);
    Frame();
    EXPECT_EQ(DrawsSinceSeed(), 1);
    godot::seed(kSeed);
    for (int i = 0; i < 30; ++i) Frame();
    EXPECT_EQ(DrawsSinceSeed(), 0) << "only a restart draws; emitting does not";
}

TEST_F(ParticleTree, AnActiveSystemDrawsInTheRestartFrame) {
    smoke->restart();
    Frame();
    Frame();   // processed: active, queued again by the cull
    godot::seed(kSeed);
    smoke->restart();
    Frame();
    EXPECT_EQ(DrawsSinceSeed(), 1);
}

TEST_F(ParticleTree, AnOffScreenSystemNeverDraws) {
    smoke->set_position({0.0f, 0.0f, 50.0f});   // behind the camera
    smoke->restart();
    for (int i = 0; i < 5; ++i) Frame();
    EXPECT_EQ(DrawsSinceSeed(), 0);
}

TEST_F(ParticleTree, AHiddenSystemNeverDraws) {
    smoke->set_visible(false);
    smoke->restart();
    for (int i = 0; i < 5; ++i) Frame();
    EXPECT_EQ(DrawsSinceSeed(), 0);
}

// Headless Godot has a dummy renderer: no particles are processed, so its
// references were made without these draws.
TEST_F(ParticleTree, TheDummyRendererNeverDraws) {
    t.set_particle_rng(false);
    smoke->restart();
    for (int i = 0; i < 5; ++i) Frame();
    EXPECT_EQ(DrawsSinceSeed(), 0);
}

// A one-shot system stops emitting at the end of its cycle and goes inactive
// lifetime * 1.2 later; after that a restart waits for the cull again.
TEST_F(ParticleTree, AOneShotGoesInactiveAfterItsCycle) {
    smoke->set_lifetime(0.5);
    smoke->restart();
    for (int i = 0; i < 120; ++i) Frame();
    EXPECT_FALSE(smoke->is_emitting());
    godot::seed(kSeed);
    smoke->restart();
    Frame();
    EXPECT_EQ(DrawsSinceSeed(), 0);
}
