// Whether a prop is drawn, and the data that makes the rule hold.
//
// The rule is Godot's, on Godot's own state read from the godot:: tree:
// visible in tree, on a layer the camera sees, not fully transparent. Most
// animated nodes start hidden, scripts show and hide parents, and layers and
// transparency hide the crushed dealer head and the waiver highlight, so a
// guessed rule draws the wrong things. The rule's pieces are pinned against
// the real engine by parity_load_main; this file pins the combination the
// renderer uses, render::PropIsDrawn.
#include "assets/animation.h"
#include "assets/room_file.h"
#include "godot/animation_player.h"
#include "godot/runtime_scene_loader.h"
#include "godot/scene_tree.h"
#include "render/prop_visibility.h"

#include "scripts/GlobalVariables.h"

#include <gtest/gtest.h>

#include <cstring>

#include <cstdio>
#include <memory>
#include <string>

namespace {

// ---------------------------------------------------------------------------
// The rule.

struct Fixture {
    godot::SceneTree tree;
    godot::Node3D* parent = nullptr;
    godot::MeshInstance3D* mesh = nullptr;
    Fixture() {
        parent = godot::cast_to<godot::Node3D>(
            tree.add_node(std::make_unique<godot::Node3D>("parent"), nullptr));
        mesh = godot::cast_to<godot::MeshInstance3D>(
            tree.add_node(std::make_unique<godot::MeshInstance3D>("mesh"), parent));
    }
};

constexpr uint32_t kAllLayers = 0xFFFFFu;

TEST(PropIsDrawn, AVisibleMeshOnLayerOneIsDrawn) {
    Fixture f;
    EXPECT_TRUE(render::PropIsDrawn(*f.mesh, kAllLayers));
}

TEST(PropIsDrawn, AHiddenParentHidesTheMesh) {
    Fixture f;
    f.parent->set_visible(false);
    EXPECT_FALSE(render::PropIsDrawn(*f.mesh, kAllLayers));
    f.parent->set_visible(true);   // HealthCounter.Bootup shows its panels
    EXPECT_TRUE(render::PropIsDrawn(*f.mesh, kAllLayers));
}

// dealer head crushed: layers = 0 until DealerIntelligence gives it layer 1.
TEST(PropIsDrawn, ANodeOnNoLayerIsNeverDrawn) {
    Fixture f;
    f.mesh->set_layers(0u);
    EXPECT_FALSE(render::PropIsDrawn(*f.mesh, kAllLayers));
    f.mesh->set_layer_mask_value(1, true);
    EXPECT_TRUE(render::PropIsDrawn(*f.mesh, kAllLayers));
}

TEST(PropIsDrawn, TheCameraCullMaskFiltersLayers) {
    Fixture f;
    f.mesh->set_layers(1u << 11);
    EXPECT_FALSE(render::PropIsDrawn(*f.mesh, 1u));
    EXPECT_TRUE(render::PropIsDrawn(*f.mesh, 1u << 11));
}

// bracket selection 3d_waiver: ButtonClass_Main._ready sets transparency 1.
TEST(PropIsDrawn, FullTransparencyIsNotDrawn) {
    Fixture f;
    f.mesh->set_transparency(1.0f);
    EXPECT_FALSE(render::PropIsDrawn(*f.mesh, kAllLayers));
    f.mesh->set_transparency(0.0f);
    EXPECT_TRUE(render::PropIsDrawn(*f.mesh, kAllLayers));
}

// The muzzle-flash cone: an alpha-transparent material with albedo alpha 0.
TEST(PropIsDrawn, ATransparentMaterialAtAlphaZeroIsNotDrawn) {
    Fixture f;
    godot::SurfaceMaterial m;
    m.transparency = 1;
    m.albedo = {1.0f, 1.0f, 1.0f, 0.0f};
    m.albedo_at_load = m.albedo;
    f.mesh->add_surface_material(m);
    EXPECT_FALSE(render::PropIsDrawn(*f.mesh, kAllLayers));
    f.mesh->set_surface_albedo(0, {1.0f, 1.0f, 1.0f, 1.0f});   // "fire"
    EXPECT_TRUE(render::PropIsDrawn(*f.mesh, kAllLayers));
    f.mesh->set_surface_albedo(0, {1.0f, 1.0f, 1.0f, 0.5f});
    EXPECT_TRUE(render::PropIsBlended(*f.mesh));
}

// The health counter's lightning icons: transparency ALPHA at albedo alpha 1.
// Godot sends any alpha-transparent material to the transparent pass whatever
// its albedo alpha, and the texture's alpha cuts the icon out of its quad.
// Drawn opaque, each icon is a solid square.
TEST(PropIsDrawn, AnAlphaTransparentMaterialIsBlendedAtFullAlbedoAlpha) {
    Fixture f;
    godot::SurfaceMaterial m;
    m.transparency = 1;
    f.mesh->add_surface_material(m);
    EXPECT_TRUE(render::PropIsDrawn(*f.mesh, kAllLayers));
    EXPECT_TRUE(render::PropIsBlended(*f.mesh));
}

// The adrenaline's glass: ALPHA_DEPTH_PRE_PASS over an opaque texture. Godot's
// pre-pass writes its depth, so it hides the liquid inside it; blended without
// depth, the liquid drawn after it paints over the glass.
TEST(PropIsDrawn, ADepthPrePassMaterialWritesDepth) {
    Fixture f;
    godot::SurfaceMaterial m;
    m.transparency = 4;
    f.mesh->add_surface_material(m);
    EXPECT_TRUE(render::PropIsBlended(*f.mesh));
    EXPECT_TRUE(render::PropWritesDepth(*f.mesh));
    Fixture alpha;
    m.transparency = 1;
    alpha.mesh->add_surface_material(m);
    EXPECT_FALSE(render::PropWritesDepth(*alpha.mesh));
}

TEST(PropIsDrawn, AlphaIsIgnoredOnAnOpaqueMaterial) {
    // Godot ignores albedo alpha when the material's transparency is DISABLED.
    Fixture f;
    godot::SurfaceMaterial m;
    m.transparency = 0;
    m.albedo = {1.0f, 1.0f, 1.0f, 0.0f};
    f.mesh->add_surface_material(m);
    EXPECT_TRUE(render::PropIsDrawn(*f.mesh, kAllLayers));
    EXPECT_FALSE(render::PropIsBlended(*f.mesh));
}

// ---------------------------------------------------------------------------
// The data, driven through the real chain: room -> performer -> animator.

std::string AnimPath() {
    return std::string(BSHT_SOURCE_DIR) + "/assets/extracted/animations.json";
}

std::string RoomPath() {
    return std::string(BSHT_SOURCE_DIR) + "/assets/baked/full_world.room";
}

bool Exists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

#define REQUIRE_EXPORT() \
    if (!Exists(AnimPath())) GTEST_SKIP() << "run tools/godot/export_animations.gd first"
#define REQUIRE_BAKE() \
    if (!Exists(RoomPath())) GTEST_SKIP() << "run ./build/src/bake_room first"

TEST(PropVisibility, TheLibraryReallyAnimatesVisibility) {
    REQUIRE_EXPORT();
    // If this ever drops to zero, animation no longer shows or hides anything
    // and the room is static again, with no other symptom.
    assets::AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromFile(AnimPath().c_str()));

    int visibleTracks = 0;
    for (const assets::AnimationPlayerData& p : lib.Players()) {
        for (const assets::Animation& a : p.animations) {
            for (const assets::Track& t : a.tracks) {
                if (t.property == "visible") ++visibleTracks;
            }
        }
    }
    EXPECT_GE(visibleTracks, 150)
        << "the game animates visibility 201 times; this is far short";
}

TEST(PropVisibility, MostAnimatedPropsStartHiddenSoTheRuleIsLoadBearing) {
    REQUIRE_BAKE();
    // 113 of 142 animated nodes. This is why getting visibility wrong hid
    // nearly everything that moves rather than a couple of edge cases.
    assets::Room room;
    ASSERT_EQ(assets::RoomFileError::None, assets::ReadRoomFile(RoomPath().c_str(), room));

    int hidden = 0;
    for (const assets::RoomProp& p : room.Props()) {
        if (!p.visibleAtLoad) ++hidden;
    }
    EXPECT_GT(hidden, 50) << "hardly any props start hidden; the premise of this file is wrong";
}

// v2.2's ButtonClass_Main shows and hides the five 3D focus brackets with
// `visible` (v1.2 used transparency), so the bake must keep each as a prop the
// script can reach.
TEST(PropVisibility, TheScriptShownBracketsAreProps) {
    REQUIRE_BAKE();
    assets::Room room;
    ASSERT_EQ(assets::RoomFileError::None, assets::ReadRoomFile(RoomPath().c_str(), room));
    for (const char* name : {"bracket selection 3d_waiver", "bracket selection 3d_door",
                             "bracket selection 3d_door backroom", "bracket selection 3d_pill bottle",
                             "bracket selection 3d_crt screen"}) {
        bool found = false;
        for (const assets::RoomProp& p : room.Props()) {
            const std::string& path = p.nodePath;
            if (path.size() >= std::strlen(name) && path.compare(path.size() - std::strlen(name), std::string::npos, name) == 0) {
                found = true;
                EXPECT_FALSE(p.visibleAtLoad) << name;
            }
        }
        EXPECT_TRUE(found) << name;
    }
}

TEST(PropVisibility, PlayingDealerUseInverterShowsAPropThatStartedHidden) {
    REQUIRE_EXPORT();
    REQUIRE_BAKE();
    // 'dealer use inverter' switches 11 nodes on, and every one starts hidden.
    // The tree's AnimationPlayer sets `visible` on the nodes PropIsDrawn reads.
    const std::string scene = std::string(BSHT_SOURCE_DIR) + "/assets/extracted/runtime_main.json";
    if (!Exists(scene)) GTEST_SKIP() << "run tools/godot/export_runtime_scene.gd first";
    assets::Room room;
    ASSERT_EQ(assets::RoomFileError::None, assets::ReadRoomFile(RoomPath().c_str(), room));

    assets::AnimationLibrary lib;
    ASSERT_TRUE(lib.LoadFromFile(AnimPath().c_str()));
    godot::SceneTree tree;
    ASSERT_EQ(godot::LoadRuntimeScene(scene.c_str(), tree), godot::RuntimeSceneError::None);
    godot::BindAnimationLibrary(tree, lib);

    auto* player = tree.get_node_as<godot::AnimationPlayer>(
        "dealer hands main1/animator_dealer hands");
    ASSERT_NE(player, nullptr) << "the player path changed";

    int hiddenBefore = 0;
    for (const assets::RoomProp& p : room.Props()) {
        const auto* n = tree.get_node_as<godot::Node3D>(p.nodePath);
        if (!p.visibleAtLoad && n && !n->is_visible_in_tree()) ++hiddenBefore;
    }
    player->play("dealer use inverter");
    ASSERT_TRUE(player->is_playing()) << "the animation is missing";

    for (int i = 0; i < 900 && player->is_playing(); ++i) tree.process_frame(1.0 / 60.0);

    int shown = 0;
    for (const assets::RoomProp& p : room.Props()) {
        if (p.visibleAtLoad) continue;
        const auto* n = tree.get_node_as<godot::Node3D>(p.nodePath);
        if (n && n->is_visible_in_tree()) ++shown;
    }
    ASSERT_GT(hiddenBefore, 0);
    EXPECT_GT(shown, 0) << "no hidden prop was made visible -- the animation reaches nothing";
}

TEST(PropVisibility, TheHealthCounterDigitsAreInTheShippedRoom) {
    REQUIRE_BAKE();
    // They have no animation and start hidden, so a bake that dropped hidden
    // static geometry had no digits for HealthCounter.Bootup to show.
    assets::Room room;
    ASSERT_EQ(assets::RoomFileError::None, assets::ReadRoomFile(RoomPath().c_str(), room));
    bool digit = false;
    for (const assets::RoomProp& p : room.Props()) {
        if (p.nodePath.find("health UI_dealer side/symbol_dealer2") != std::string::npos) {
            digit = true;
        }
    }
    EXPECT_TRUE(digit) << "the counter's digit meshes are not in the room";
}

}  // namespace

// The blood splatter plane (textures/blood/blood layer0.tres, blend_mode 1):
// Godot's ADD material goes to the transparent pass and adds its colour, so
// its black-backed texture leaves the view untouched where there is no blood.
// Drawn as an ordinary opaque prop it was a dark square over the view.
TEST(PropIsDrawn, AnAdditiveMaterialIsBlendedAndAdds) {
    Fixture f;
    godot::SurfaceMaterial m;
    m.blend_mode = godot::kBlendModeAdd;
    f.mesh->add_surface_material(m);
    EXPECT_TRUE(render::PropIsDrawn(*f.mesh, kAllLayers));
    EXPECT_TRUE(render::PropIsBlended(*f.mesh));
    EXPECT_TRUE(render::PropIsAdditive(*f.mesh));
}

TEST(PropIsDrawn, AMixMaterialDoesNotAdd) {
    Fixture f;
    godot::SurfaceMaterial m;
    m.transparency = 1;
    f.mesh->add_surface_material(m);
    EXPECT_FALSE(render::PropIsAdditive(*f.mesh));
}

namespace {

// v2.2 repaints the shells' live/blank materials at ready (ColorblindManager,
// ShellEjectManager._ready): every shell surface draws one of those colours,
// never the .tres's own, so each needs a baked page at all four.
TEST(PropVisibility, EveryShellSurfaceHasAPageForEachColourV22Paints) {
    REQUIRE_BAKE();
    assets::Room room;
    ASSERT_EQ(assets::RoomFileError::None, assets::ReadRoomFile(RoomPath().c_str(), room));
    using scripts::GlobalVariables;
    const godot::Color colours[] = {GlobalVariables::default_color_live, GlobalVariables::default_color_blank,
                                    GlobalVariables::colorblind_color_live, GlobalVariables::colorblind_color_blank};
    const float noEmission[3] = {0, 0, 0};
    int shells = 0;
    for (const assets::RoomProp& p : room.Props()) {
        // A shell surface: one that swaps between a red and a blue material.
        bool red = false;
        bool blue = false;
        for (const assets::PropVariant& v : p.variants) {
            red = red || (v.albedo[0] == 1.0f && v.albedo[2] < 0.5f);
            blue = blue || (v.albedo[2] == 1.0f && v.albedo[0] < 0.5f);
        }
        if (!red || !blue) continue;
        ++shells;
        for (const godot::Color& c : colours) {
            const float albedo[4] = {c.r, c.g, c.b, c.a};
            EXPECT_GE(p.VariantTexture(albedo, noEmission, 0u), 0)
                << p.nodePath << " has no page for (" << c.r << ", " << c.g << ", " << c.b << ")";
        }
    }
    EXPECT_GE(shells, 3) << "the compartment shell, the examined shell and the two ejected shells";
}

}  // namespace
