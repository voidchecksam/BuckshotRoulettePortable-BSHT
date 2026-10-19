// The loader, first on a hand-written document (always runs), then on the real
// export (skips until export_runtime_scene.gd has been run). The real-data cases
// read the scene the way the renderer reads it.
#include "godot/runtime_scene_loader.h"
#include "assets/animation.h"
#include "scripts/item_scenes.h"

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/packed_scene.h"

#include "assets/room.h"
#include "assets/room_file.h"
#include "core/trs.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {

std::string ExportPath() {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/runtime_main.json";
}

const char* kTiny = R"({"format":4,"scene":"t","capture_frame":3,"nodes":[
 {"path":".","parent":-1,"class":"Node3D","script":"","visible":true,
  "xform":[1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1],"rotation":[0,0,0],"scale":[1,1,1]},
 {"path":"Camera","parent":0,"class":"Camera3D","script":"CameraManager","visible":true,
  "xform":[1,0,0,0, 0,1,0,0, 0,0,1,0, -162,52,-90,1],"rotation":[0,0,0],"scale":[1,1,1],
  "current":true,"fov":65.5,"cull_mask":1048575},
 {"path":"plain","parent":0,"class":"Node","script":""},
 {"path":"plain/head","parent":2,"class":"MeshInstance3D","script":"","visible":true,
  "xform":[1,0,0,0, 0,1,0,0, 0,0,1,0, 1,2,3,1],"rotation":[0,1.5,0],"scale":[1,1,1],
  "layers":0,"transparency":1.0,"exports":{"visible":false,"x":{"node":"Camera"}}}
]})";

}  // namespace

TEST(GodotRuntimeScene, BuildsTypedNodesWithGodotState) {
    godot::SceneTree t;
    ASSERT_EQ(godot::LoadRuntimeSceneFromString(kTiny, t), godot::RuntimeSceneError::None);
    EXPECT_EQ(t.get_node_count(), 4u);

    auto* cam = t.get_node_as<godot::Camera3D>("Camera");
    ASSERT_NE(cam, nullptr);
    EXPECT_TRUE(cam->is_current());
    EXPECT_FLOAT_EQ(cam->get_fov(), 65.5f);
    EXPECT_EQ(cam->get_cull_mask(), 1048575u);
    EXPECT_EQ(t.get_current_camera(), cam);

    auto* head = t.get_node_as<godot::MeshInstance3D>("plain/head");
    ASSERT_NE(head, nullptr);
    EXPECT_EQ(head->get_layers(), 0u);
    EXPECT_FLOAT_EQ(head->get_transparency(), 1.0f);
    EXPECT_FLOAT_EQ(head->get_rotation().y, 1.5f);
    EXPECT_FLOAT_EQ(head->get_transform().m[13], 2.0f);
    // An @export named `visible` must not be read as the node's own flag.
    EXPECT_TRUE(head->is_visible());
    EXPECT_EQ(head->get_class(), "MeshInstance3D");
    EXPECT_EQ(godot::cast_to<godot::Node3D>(t.get_node("plain")), nullptr);
}

TEST(GodotRuntimeScene, RejectsAWrongFormatAndABadParent) {
    godot::SceneTree a;
    EXPECT_EQ(godot::LoadRuntimeSceneFromString(R"({"format":99,"nodes":[]})", a),
              godot::RuntimeSceneError::BadVersion);
    godot::SceneTree b;
    EXPECT_EQ(godot::LoadRuntimeSceneFromString(
                  R"({"format":4,"nodes":[{"path":".","parent":5,"class":"Node"}]})", b),
              godot::RuntimeSceneError::BadParent);
    godot::SceneTree c;
    EXPECT_EQ(godot::LoadRuntimeScene("no/such/file.json", c),
              godot::RuntimeSceneError::FileNotFound);
    // A format-3 export predates the GUI and collision fields: re-export, never
    // guess.
    godot::SceneTree e;
    EXPECT_EQ(godot::LoadRuntimeSceneFromString(R"({"format":3,"nodes":[]})", e),
              godot::RuntimeSceneError::BadVersion);
    // A format-2 export predates Controls and mesh names: re-export, never guess.
    godot::SceneTree d;
    EXPECT_EQ(godot::LoadRuntimeSceneFromString(R"({"format":2,"nodes":[]})", d),
              godot::RuntimeSceneError::BadVersion);
}

TEST(GodotRuntimeScene, BuildsControlsTextMeshNamesAndKeepsExports) {
    const char* json = R"({"format":4,"nodes":[
      {"path":".","parent":-1,"class":"Node","script":""},
      {"path":"UI","parent":0,"class":"Control","script":"","visible":false,"scale":[1,1],"modulate":[1,1,1,1]},
      {"path":"UI/text","parent":1,"class":"Label","script":"","visible":true,
       "text":"AB\nC","visible_characters":-1,"scale":[1,1],"modulate":[1,1,1,0.5]},
      {"path":"UI/back","parent":1,"class":"ColorRect","script":"","visible":true,"scale":[2,1],"modulate":[1,1,1,1]},
      {"path":"UI/tex","parent":1,"class":"TextureRect","script":"","visible":true,"scale":[1,1],"modulate":[1,1,1,1]},
      {"path":"UI/btn","parent":1,"class":"Button","script":"","visible":true,"text":"","scale":[1,1],"modulate":[1,1,1,1]},
      {"path":"lab","parent":0,"class":"Label3D","script":"","visible":true,
       "xform":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"rotation":[0,0,0],"scale":[1,1,1],
       "layers":1,"transparency":0,"materials":[],"text":"DEALER","modulate":[0.4,0.4,0.4,1]},
      {"path":"m","parent":0,"class":"MeshInstance3D","script":"ShellBranch",
       "xform":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"rotation":[0,0,0],"scale":[1,1,1],
       "visible":true,"layers":1,"transparency":0,"mesh":"shotgun shells export1_Cylinder_022",
       "materials":[{"albedo":[1,1,1,1],"transparency":0},{"albedo":[1,0.28,0.3,1],"transparency":0}],
       "exports":{"isLive":true,"mesh":{"node":"m"}}}]})";
    godot::SceneTree tree;
    ASSERT_EQ(godot::LoadRuntimeSceneFromString(json, tree), godot::RuntimeSceneError::None);

    auto* label = tree.get_node_as<godot::Label>("UI/text");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->get_class(), "Label");
    EXPECT_EQ(label->get_text(), "AB\nC");
    EXPECT_EQ(label->get_visible_characters(), -1);
    EXPECT_FLOAT_EQ(label->get_modulate().a, 0.5f);
    EXPECT_FALSE(label->is_visible_in_tree());   // its parent Control is hidden
    tree.get_node_as<godot::Control>("UI")->set_visible(true);
    EXPECT_TRUE(label->is_visible_in_tree());

    ASSERT_NE(tree.get_node_as<godot::ColorRect>("UI/back"), nullptr);
    EXPECT_FLOAT_EQ(tree.get_node_as<godot::ColorRect>("UI/back")->get_scale().x, 2.0f);
    EXPECT_NE(tree.get_node_as<godot::TextureRect>("UI/tex"), nullptr);
    EXPECT_NE(tree.get_node_as<godot::Button>("UI/btn"), nullptr);
    // A Label is not a ColorRect.
    EXPECT_EQ(tree.get_node_as<godot::ColorRect>("UI/text"), nullptr);

    EXPECT_EQ(tree.get_node_as<godot::Label3D>("lab")->get_text(), "DEALER");
    // TextInteraction fades the shooting choice by writing Label3D.modulate.
    EXPECT_FLOAT_EQ(tree.get_node_as<godot::Label3D>("lab")->get_modulate().r, 0.4f);
    EXPECT_FLOAT_EQ(tree.get_node_as<godot::Label3D>("lab")->get_modulate().a, 1.0f);

    auto* m = tree.get_node_as<godot::MeshInstance3D>("m");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->get_mesh_name(), "shotgun shells export1_Cylinder_022");
    EXPECT_EQ(m->get_script_file(), "ShellBranch");
    EXPECT_NE(m->get_exports_json().find("isLive"), std::string::npos);
    EXPECT_TRUE(tree.get_node("UI")->get_exports_json().empty());
}

// The spatial-mix keys (godot/audio_spatial.h). Absent keys keep Godot's
// defaults, so an export from before them still loads every player.
TEST(GodotRuntimeScene, AudioPlayersCarryTheirSpatialMix) {
    const char* json = R"({"format":4,"nodes":[
 {"path":".","parent":-1,"class":"Node3D","script":""},
 {"path":"gun","parent":0,"class":"AudioStreamPlayer2D","script":"",
  "audio":{"stream":"gunshot.wav","volume_db":5,"pitch_scale":1,"playing":false,
           "global_position":[12,-3],"max_distance":1500,"attenuation":2,"panning_strength":0}},
 {"path":"hum","parent":0,"class":"AudioStreamPlayer3D","script":"",
  "xform":[1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1],"rotation":[0,0,0],"scale":[1,1,1],
  "audio":{"stream":"hum.ogg","volume_db":-12,"pitch_scale":0.49,"playing":false,
           "unit_size":4,"max_db":1,"max_distance":30,"attenuation_model":2,"panning_strength":0.57}},
 {"path":"old","parent":0,"class":"AudioStreamPlayer2D","script":"",
  "audio":{"stream":"x.ogg","volume_db":0,"pitch_scale":1,"playing":false}},
 {"path":"plain","parent":0,"class":"AudioStreamPlayer","script":"",
  "audio":{"stream":"y.ogg","volume_db":0,"pitch_scale":1,"playing":false,"bus":"music","max_polyphony":60}}
]})";
    godot::SceneTree tree;
    ASSERT_EQ(godot::LoadRuntimeSceneFromString(json, tree), godot::RuntimeSceneError::None);
    const godot::AudioPlayback* gun = tree.get_node("gun")->as_audio_playback();
    ASSERT_NE(gun, nullptr);
    EXPECT_EQ(gun->space(), godot::AudioSpace::Flat2D);
    EXPECT_FLOAT_EQ(gun->spatial_2d().global_position.x, 12.0f);
    EXPECT_FLOAT_EQ(gun->spatial_2d().global_position.y, -3.0f);
    EXPECT_FLOAT_EQ(gun->spatial_2d().max_distance, 1500.0f);
    EXPECT_FLOAT_EQ(gun->spatial_2d().attenuation, 2.0f);
    EXPECT_FLOAT_EQ(gun->spatial_2d().panning_strength, 0.0f);
    const godot::AudioPlayback* hum = tree.get_node("hum")->as_audio_playback();
    ASSERT_NE(hum, nullptr);
    EXPECT_EQ(hum->space(), godot::AudioSpace::Positional3D);
    EXPECT_FLOAT_EQ(hum->spatial_3d().unit_size, 4.0f);
    EXPECT_FLOAT_EQ(hum->spatial_3d().max_db, 1.0f);
    EXPECT_FLOAT_EQ(hum->spatial_3d().max_distance, 30.0f);
    EXPECT_EQ(hum->spatial_3d().attenuation_model, 2);
    EXPECT_FLOAT_EQ(hum->spatial_3d().panning_strength, 0.57f);
    const godot::AudioPlayback* old = tree.get_node("old")->as_audio_playback();
    EXPECT_EQ(old->space(), godot::AudioSpace::Flat2D);
    EXPECT_FLOAT_EQ(old->spatial_2d().max_distance, 2000.0f);
    EXPECT_FLOAT_EQ(old->spatial_2d().panning_strength, 1.0f);
    EXPECT_EQ(tree.get_node("plain")->as_audio_playback()->space(), godot::AudioSpace::Plain);
    EXPECT_EQ(tree.get_node("plain")->as_audio_playback()->get_bus(), "music");
    EXPECT_EQ(tree.get_node("plain")->as_audio_playback()->get_max_polyphony(), 60);
    EXPECT_EQ(tree.get_node("old")->as_audio_playback()->get_bus(), "Master") << "Godot's default bus";
}

TEST(GodotRuntimeScene, TheRealExportHoldsTheWholeTree) {
    godot::SceneTree t;
    const auto e = godot::LoadRuntimeScene(ExportPath().c_str(), t);
    if (e == godot::RuntimeSceneError::FileNotFound) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    ASSERT_EQ(e, godot::RuntimeSceneError::None) << godot::RuntimeSceneErrorText(e);
    EXPECT_EQ(t.get_node_count(), 1798u);   // v2.2
    ASSERT_NE(t.get_current_camera(), nullptr);
}

// The upright shotgun: its rotation is radians, and composing it as radians
// reproduces the exact authored matrix.
TEST(GodotRuntimeScene, ShotgunRotationIsRadians) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    auto* gun = t.get_node_as<godot::Node3D>("tabletop parent/shotgun main parent");
    ASSERT_NE(gun, nullptr);
    const godot::Vector3 r = gun->get_rotation();
    const godot::Vector3 s = gun->get_scale();
    const float pos[3] = {gun->get_position().x, gun->get_position().y, gun->get_position().z};
    const float rot[3] = {r.x, r.y, r.z};
    const float scl[3] = {s.x, s.y, s.z};
    const core::Mat4 composed = core::ComposeTRSRad(pos, rot, scl);
    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(composed.m[i], gun->get_transform().m[i], 1e-4f) << "m[" << i << "]";
    }
}

TEST(GodotRuntimeScene, CrushedHeadIsOnNoLayerAndWaiverBracketIsHidden) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    auto* crushed = t.get_node_as<godot::GeometryInstance3D>(
        "dealer model parent/dealer head rigged1/Armature/Skeleton3D/dealer head mesh/dealer head crushed");
    ASSERT_NE(crushed, nullptr);
    EXPECT_EQ(crushed->get_layers(), 0u);

    int brackets = 0;
    for (godot::Node* n : t.nodes_in_tree_order()) {
        if (n->get_name() != "bracket selection 3d_waiver") continue;
        auto* g = godot::cast_to<godot::GeometryInstance3D>(n);
        ASSERT_NE(g, nullptr);
        EXPECT_FALSE(g->is_visible()) << "v2.2 hides it";
        ++brackets;
    }
    EXPECT_EQ(brackets, 1);
}

// Geometry Godot does not draw when the scene loads must stay a prop, or the
// renderer can never read its state. scene_graph.json has neither layers nor
// transparency, so merged, the crushed head and the waiver highlight would
// draw over everything whatever the godot:: tree said.
TEST(GodotRuntimeScene, GeometryGodotDoesNotDrawAtLoadIsNeverMerged) {
    assets::Room room;
    const std::string roomPath = std::string(BSHT_SOURCE_DIR) + "/assets/baked/full_world.room";
    if (assets::ReadRoomFile(roomPath.c_str(), room) != assets::RoomFileError::None) {
        GTEST_SKIP() << "run ./build/src/bake_room first";
    }
    const char* const mustBeProps[] = {
        "bracket selection 3d_waiver",   // transparency 1 after ButtonClass_Main._ready
        "dealer model parent/dealer head rigged1/Armature/Skeleton3D/dealer head mesh/dealer head crushed",  // layers 0
    };
    for (const char* path : mustBeProps) {
        bool found = false;
        for (const assets::RoomProp& p : room.Props()) found |= p.nodePath == path;
        EXPECT_TRUE(found) << path << " was merged into a static batch";
    }
}

// The muzzle-flash cone is hidden by material alpha alone: visible, layer 1,
// transparency 0, albedo (1,1,1,0) on an alpha-transparent StandardMaterial3D.
TEST(GodotRuntimeScene, TheMuzzleFlashConeCarriesItsTransparentMaterial) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    auto* cone = t.get_node_as<godot::GeometryInstance3D>(
        "tabletop parent/shotgun main parent/shotgun parent new/muzzle flash test1/Cylinder");
    ASSERT_NE(cone, nullptr);
    ASSERT_GE(cone->surface_count(), 1);
    EXPECT_FLOAT_EQ(cone->get_surface_material(0).albedo.a, 0.0f);
    EXPECT_NE(cone->get_surface_material(0).transparency, 0);
}

TEST(GodotRuntimeScene, AnimationPlayersAndAudioPlayersAreTypedNodes) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    int players = 0, audio = 0, audio3d = 0;
    for (godot::Node* n : t.nodes_in_tree_order()) {
        if (godot::cast_to<godot::AnimationPlayer>(n)) ++players;
        if (n->as_audio_playback()) ++audio;
        // AudioStreamPlayer3D is a Node3D in Godot, and parity_load_main
        // captures its transform: modelling it as a plain Node lost 8 nodes.
        if (auto* a3 = godot::cast_to<godot::AudioStreamPlayer3D>(n)) {
            ++audio3d;
            EXPECT_NE(godot::cast_to<godot::Node3D>(n), nullptr) << a3->get_path();
        }
    }
    EXPECT_EQ(players, 60);
    EXPECT_EQ(audio, 96);    // 88 AudioStreamPlayer2D + 8 AudioStreamPlayer3D
    EXPECT_EQ(audio3d, 8);
    // The dealer's head bob autoplays: by the capture frame it is playing.
    auto* bob = t.get_node_as<godot::AnimationPlayer>("dealer model parent/animator_dealer head bob");
    ASSERT_NE(bob, nullptr);
    EXPECT_TRUE(bob->is_playing());
    EXPECT_EQ(bob->get_assigned_animation(), "dealer head bob");
}

// The renderer binds every baked prop by path. One that does not resolve would
// silently vanish, so every one must.
TEST(GodotRuntimeScene, EveryBakedPropResolvesToGeometry) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    assets::Room room;
    const std::string roomPath = std::string(BSHT_SOURCE_DIR) + "/assets/baked/full_world.room";
    if (assets::ReadRoomFile(roomPath.c_str(), room) != assets::RoomFileError::None) {
        GTEST_SKIP() << "run ./build/src/bake_room first";
    }
    // Template props have no node: their key must name a MeshInstance3D of a
    // clone of the scene they were baked from.
    std::vector<std::string> cloneKeys;
    const auto addCloneKeys = [&cloneKeys](const godot::PackedScene* scene) {
        std::unique_ptr<godot::Node> clone = scene->instantiate();
        std::vector<godot::Node*> pending{clone.get()};
        while (!pending.empty()) {
            godot::Node* n = pending.back();
            pending.pop_back();
            for (int i = 0; i < n->get_child_count(); ++i) pending.push_back(n->get_child(i));
            if (auto* m = godot::cast_to<godot::MeshInstance3D>(n)) cloneKeys.push_back(m->get_template_key());
        }
    };
    const std::string extracted = std::string(BSHT_SOURCE_DIR) + "/assets/extracted/";
    if (const godot::PackedScene* shell =
            godot::PackedScene::load("res://instances/shell.tscn", (extracted + "runtime_shell.json").c_str())) {
        addCloneKeys(shell);
    }
    // The nine item scenes are template props too, baked from the list
    // ItemManager itself names, so the check reads the same list.
    if (godot::Node* itemManager = t.get_node("standalone managers/item manager")) {
        for (const std::string& res : scripts::ItemScenePaths(*itemManager)) {
            const std::string file = extracted + scripts::ItemSceneExportFile(res);
            if (const godot::PackedScene* item = godot::PackedScene::load(res, file.c_str())) {
                addCloneKeys(item);
            }
        }
    }
    int unresolved = 0;
    for (const assets::RoomProp& p : room.Props()) {
        if (p.IsTemplate()) {
            if (std::find(cloneKeys.begin(), cloneKeys.end(), p.templateKey) == cloneKeys.end()) {
                ADD_FAILURE() << "template prop matches no clone mesh: " << p.templateKey;
                if (++unresolved > 10) break;
            }
            continue;
        }
        if (!t.get_node_as<godot::GeometryInstance3D>(p.nodePath)) {
            ADD_FAILURE() << "prop does not resolve: " << p.nodePath;
            if (++unresolved > 10) break;
        }
    }
    EXPECT_EQ(unresolved, 0);
}

// The surface bake reads every light's colour, decay and shadow state, and
// every surface's roughness, metallic and emission, from the live export.
TEST(GodotRuntimeScene, LightsCarryWhatTheSurfaceBakeNeeds) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    auto* l = t.get_node_as<godot::OmniLight3D>("light parent/light_tabletop interior LS");
    ASSERT_NE(l, nullptr);
    EXPECT_NEAR(l->get_light_color().r, 220.0f / 255.0f, 1e-6f);   // authored as bytes
    EXPECT_NEAR(l->get_light_color().g, 106.0f / 255.0f, 1e-6f);
    EXPECT_NEAR(l->get_light_color().b, 31.0f / 255.0f, 1e-6f);
    EXPECT_NEAR(l->get_light_energy(), 31.009f, 1e-2f);   // v2.2 (v1.2: 32.77)
    EXPECT_NEAR(l->get_omni_range(), 16.9f, 1e-2f);
    EXPECT_FLOAT_EQ(l->get_omni_attenuation(), 1.0f);
    EXPECT_TRUE(l->has_shadow());
}

TEST(GodotRuntimeScene, SurfacesCarryWhatTheSurfaceBakeNeeds) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    auto* table = t.get_node_as<godot::GeometryInstance3D>("tabletop parent/main tabletop");
    ASSERT_NE(table, nullptr);
    ASSERT_GE(table->surface_count(), 1);
    EXPECT_NEAR(table->get_surface_material(0).roughness, 0.8f, 1e-6f);
    EXPECT_FLOAT_EQ(table->get_surface_material(0).metallic, 0.0f);
    EXPECT_EQ(table->get_surface_material(0).diffuse_mode, 0);   // Burley

    // A scene-authored surface keeps its material: v2.2 draws the blinker
    // unshaded red (v1.2: emissive).
    auto* blinker = t.get_node_as<godot::GeometryInstance3D>(
        "tabletop parent/tabletop interior_table pivot/tabletop interior device parent/"
        "tabletop interior blinker");
    ASSERT_NE(blinker, nullptr);
    EXPECT_FLOAT_EQ(blinker->get_surface_material(0).albedo.r, 1.0f);
    EXPECT_FLOAT_EQ(blinker->get_surface_material(0).albedo.g, 0.0f);
    EXPECT_EQ(blinker->get_surface_material(0).shading_mode, 0);   // unshaded

    // The normal map (main.tscn sub_resource 1190 "wire adapter": normal_enabled,
    // normal_texture ExtResource 449, normal_scale left at 1).
    auto* counter = t.get_node_as<godot::GeometryInstance3D>(
        "health counter main1/main counter/health counter parent");
    ASSERT_NE(counter, nullptr);
    ASSERT_GE(counter->surface_count(), 4);
    EXPECT_EQ(counter->get_surface_material(3).normal_texture,
              "inline_textures/models_health_counter_main_health_counter_main1_"
              "TexturesCom_MetalSheet_Aged_512_normal.png");
    EXPECT_FLOAT_EQ(counter->get_surface_material(3).normal_scale, 1.0f);
    EXPECT_TRUE(counter->get_surface_material(1).normal_texture.empty());   // "void.001"
}

// The backroom light reaches only layers 1, 11 and 12 (main.tscn:46231), and
// the surface bake must respect it (scene_forward_clustered.glsl:1924).
TEST(GodotRuntimeScene, LightsCarryTheirCullMaskAndGeometryItsShadowCasting) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    auto* l = t.get_node_as<godot::OmniLight3D>("light parent/lght_backroom main2");
    ASSERT_NE(l, nullptr);
    EXPECT_EQ(l->get_cull_mask(), 4293921793u);
    auto* table = t.get_node_as<godot::GeometryInstance3D>("tabletop parent/main tabletop");
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->get_cast_shadow(), 1);   // SHADOW_CASTING_SETTING_ON
}

// The Environment's adjustments, and the animations that move them:
// animator_pp's brightness (revival, adrenaline, fades) and the muzzle flash's
// contrast.
TEST(GodotRuntimeScene, WorldEnvironmentCarriesItsAdjustmentsAndAnimatesThem) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    }
    auto* env = t.get_node_as<godot::WorldEnvironment>("WorldEnvironment");
    ASSERT_NE(env, nullptr);
    EXPECT_NEAR(env->get_adjustment_brightness(), 1.49f, 1e-6f);
    EXPECT_NEAR(env->get_adjustment_contrast(), 0.8f, 1e-6f);
    EXPECT_NEAR(env->get_adjustment_saturation(), 0.26f, 1e-6f);

    assets::AnimationLibrary anims;
    ASSERT_TRUE(anims.LoadFromFile((std::string(BSHT_SOURCE_DIR) + "/assets/extracted/animations.json").c_str()));
    godot::BindAnimationLibrary(t, anims);
    auto* pp = t.get_node_as<godot::AnimationPlayer>("WorldEnvironment/animator_pp");
    ASSERT_NE(pp, nullptr);
    pp->play("revival brightness");
    pp->seek(2.0, true);   // 5.44 -> 1.49 over 4 s, linear
    EXPECT_NEAR(env->get_adjustment_brightness(), 5.44f + (1.49f - 5.44f) * 0.5f, 1e-4f);

    auto* flash = t.get_node_as<godot::AnimationPlayer>("tabletop parent/shotgun main parent/animator_muzzle flash");
    ASSERT_NE(flash, nullptr);
    flash->play("muzzle flash fire");
    flash->seek(0.0, true);
    EXPECT_NEAR(env->get_adjustment_contrast(), 2.65f, 1e-4f);
}

// The menu's Environment lights with an ambient colour (menu.tscn: source 2,
// Color(0.321569, 0.329412, 0.156863), energy 1.29), which the bake needs;
// main's energy is 0.
TEST(RuntimeSceneLoader, TheMenuEnvironmentCarriesItsAmbientLight) {
    godot::SceneTree tree;
    const std::string path = std::string(BSHT_SOURCE_DIR) + "/assets/extracted/runtime_menu.json";
    if (godot::LoadRuntimeScene(path.c_str(), tree) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "export runtime_menu.json first";
    }
    const auto* env = tree.get_node_as<godot::WorldEnvironment>("WorldEnvironment");
    ASSERT_NE(env, nullptr);
    EXPECT_EQ(env->get_ambient_light_source(), 2);
    EXPECT_NEAR(env->get_ambient_light_energy(), 1.29f, 1e-5f);
    EXPECT_NEAR(env->get_ambient_light_color().r, 0.321569f, 1e-5f);
    EXPECT_NEAR(env->get_adjustment_contrast(), 0.95f, 1e-5f);
}

// BaseMaterial3D.blend_mode reaches the tree: the blood splatter plane adds
// (blend_mode 1), every other material mixes.
TEST(GodotRuntimeScene, TheBloodSplatterPlaneAdds) {
    godot::SceneTree t;
    if (godot::LoadRuntimeScene(ExportPath().c_str(), t) != godot::RuntimeSceneError::None) {
        GTEST_SKIP() << "export runtime_main.json first";
    }
    const auto* plane = t.get_node_as<godot::GeometryInstance3D>("Camera/blood splatter plane");
    ASSERT_NE(plane, nullptr);
    EXPECT_EQ(plane->get_material_override().blend_mode, godot::kBlendModeAdd);
    EXPECT_EQ(plane->get_active_material_blend_mode(), godot::kBlendModeAdd);
    const auto* table = t.get_node_as<godot::GeometryInstance3D>("tabletop parent/main tabletop");
    if (table) EXPECT_EQ(table->get_active_material_blend_mode(), 0);
}
