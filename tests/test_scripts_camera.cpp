// scripts/CameraManager.gd translated: a plain lerp of position, rotation_degrees
// and fov with ease(c, 0.2), from wherever the camera is.
#include "scripts/CameraManager.h"

#include "godot/math_funcs.h"
#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "scripts/resources.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

struct CameraFixture {
    godot::SceneTree tree;
    godot::Camera3D* cam = nullptr;
    godot::AudioStreamPlayer* pan = nullptr;
    scripts::CameraManager* camera = nullptr;
    scripts::CameraSocket home;
    scripts::CameraSocket enemy;

    CameraFixture() {
        home.socketName = "home";
        home.pos = {1.0f, 2.0f, 3.0f};
        home.rot = {-49.0f, -90.0f, 0.0f};
        home.fov = 65.5;
        enemy.socketName = "enemy";
        enemy.pos = {10.0f, 0.0f, 0.0f};
        enemy.rot = {0.0f, 90.0f, 0.0f};
        enemy.fov = 40.0;

        godot::Node* root = tree.add_node(std::make_unique<godot::Node>("main"), nullptr);
        cam = godot::cast_to<godot::Camera3D>(tree.add_node(std::make_unique<godot::Camera3D>("Camera"), root));
        cam->set_fov(75.0f);
        pan = godot::cast_to<godot::AudioStreamPlayer>(
            tree.add_node(std::make_unique<godot::AudioStreamPlayer>("pan"), root));

        auto script = std::make_unique<scripts::CameraManager>();
        script->cam = cam;
        script->speaker_pan = pan;
        script->socketArray = {&home, &enemy};
        script->dur = 0.6;
        script->isPlayingSound = true;
        camera = script.get();
        auto node = std::make_unique<godot::Node>("camera manager");
        node->set_script(std::move(script));
        tree.add_node(std::move(node), root);
        tree.ready_scripts();
    }

    void frames(int n) {
        for (int i = 0; i < n; ++i) tree.process_frame(1.0 / 60.0);
    }
};

}  // namespace

TEST(ScriptsCameraManager, LerpFollowsGodotsEaseCurve) {
    CameraFixture f;
    f.camera->BeginLerp("enemy");
    f.frames(18);   // elapsed 0.3 of 0.6
    const double c = godot::ease(0.5, 0.2);
    EXPECT_NEAR(f.cam->get_position().x, 10.0 * c, 1e-4);
    EXPECT_NEAR(f.cam->get_rotation_degrees().y, 90.0 * c, 1e-3);
    EXPECT_NEAR(f.cam->get_fov(), 75.0 + (40.0 - 75.0) * c, 1e-4);
    EXPECT_EQ(f.camera->activeSocket, "enemy");
}

TEST(ScriptsCameraManager, ArrivesAtTheSocketAndStaysThere) {
    CameraFixture f;
    f.camera->BeginLerp("home");
    f.frames(40);
    EXPECT_NEAR(f.cam->get_position().z, 3.0f, 1e-5);
    EXPECT_NEAR(f.cam->get_rotation_degrees().x, -49.0f, 1e-3);
    EXPECT_NEAR(f.cam->get_fov(), 65.5f, 1e-5);
    EXPECT_TRUE(f.camera->moving);   // source behaviour: never switched off
}

TEST(ScriptsCameraManager, ANewLerpStartsFromWhereTheCameraIs) {
    CameraFixture f;
    f.camera->BeginLerp("enemy");
    f.frames(18);
    const float midway = f.cam->get_position().x;
    f.camera->BeginLerp("home");
    EXPECT_FLOAT_EQ(f.camera->currentPos.x, midway);
    f.frames(1);
    EXPECT_LT(f.cam->get_position().x, midway);
}

TEST(ScriptsCameraManager, AnUnknownSocketIsRememberedButNotMovedTo) {
    CameraFixture f;
    f.camera->BeginLerp("nowhere");
    f.frames(5);
    EXPECT_EQ(f.camera->activeSocket, "nowhere");
    EXPECT_FALSE(f.camera->moving);
    EXPECT_FLOAT_EQ(f.cam->get_position().x, 0.0f);
}

TEST(ScriptsCameraManager, EveryMovePlaysThePanAtARandomPitch) {
    CameraFixture f;
    godot::seed(3);
    const double expected = godot::randf_range(0.5, 1.0);
    godot::seed(3);
    f.camera->BeginLerp("home");
    EXPECT_TRUE(f.pan->is_playing());
    EXPECT_FLOAT_EQ(f.pan->get_pitch_scale(), static_cast<float>(expected));
}
