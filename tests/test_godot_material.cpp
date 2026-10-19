// Shared material resources. In Godot a material is a Resource: a script that
// holds it and a mesh that draws with it hold one object, so a write through
// the script changes what the mesh draws. SegmentManager fades the cut barrel
// exactly this way (mat.albedo_color on res://materials/overwrite.tres, which
// is the segment's material_override).
#include "godot/material.h"
#include "godot/node.h"
#include "godot/scene_tree.h"

#include <gtest/gtest.h>

#include <memory>

TEST(GodotMaterial, TheSameResourceIsOneObject) {
    godot::MaterialLibrary::reset();
    godot::SurfaceMaterial init;
    init.albedo = {1, 1, 1, 1};
    godot::SurfaceMaterial* a = godot::MaterialLibrary::get("res://m.tres", init);
    godot::SurfaceMaterial* b = godot::MaterialLibrary::get("res://m.tres", init);
    EXPECT_EQ(a, b);
    EXPECT_NE(a, godot::MaterialLibrary::get("res://other.tres", init));
}

// A material with no resource path is the mesh's own: two of them must not
// become one shared object.
TEST(GodotMaterial, MaterialsWithoutAResourceAreNotShared) {
    godot::MaterialLibrary::reset();
    godot::SurfaceMaterial init;
    godot::SurfaceMaterial* a = godot::MaterialLibrary::get("", init);
    godot::SurfaceMaterial* b = godot::MaterialLibrary::get("", init);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a, b);
}

TEST(GodotMaterial, WritingTheResourceChangesWhatTheMeshDraws) {
    godot::MaterialLibrary::reset();
    godot::SceneTree t;
    auto* mesh = static_cast<godot::MeshInstance3D*>(
        t.add_node(std::make_unique<godot::MeshInstance3D>("segment"), t.get_root()));
    godot::SurfaceMaterial init;
    init.albedo = {1, 1, 1, 1};
    init.transparency = 1;   // alpha-transparent, as overwrite.tres is
    mesh->add_surface_material(init);
    godot::SurfaceMaterial* shared = godot::MaterialLibrary::get("res://materials/overwrite.tres", init);
    mesh->bind_material_override(shared);
    EXPECT_FLOAT_EQ(mesh->get_draw_alpha(), 1.0f);
    shared->albedo = {1, 1, 1, 0};
    EXPECT_FLOAT_EQ(mesh->get_draw_alpha(), 0.0f);
}

// A script swaps a material's albedo_texture (EndlessMode's rounds display,
// CrtIcon). The texture is named by the key the baker reads, derived from the
// resource path as texture_export.gd derives it.
TEST(GodotMaterial, ATextureResourceMapsToTheBakersKey) {
    EXPECT_EQ(godot::TextureKeyForResource("res://misc/display rounds endless png.png"),
              "inline_textures/misc_display_rounds_endless_png.png");
    EXPECT_EQ(godot::TextureKeyForResource("res://misc/crt icon_top leaderboard2.png"),
              "inline_textures/misc_crt_icon_top_leaderboard2.png");
    EXPECT_EQ(godot::TextureKeyForResource("res://a/b.jpg"), "inline_textures/a_b.jpg.png");
    EXPECT_EQ(godot::TextureKeyForResource(""), "");
}

TEST(GodotMaterial, TheActiveTextureFollowsTheSharedOverride) {
    godot::MaterialLibrary::reset();
    godot::SceneTree t;
    auto* mesh = static_cast<godot::MeshInstance3D*>(
        t.add_node(std::make_unique<godot::MeshInstance3D>("display"), t.get_root()));
    godot::SurfaceMaterial init;
    init.albedo_texture = "inline_textures/misc_display_rounds.png";
    mesh->add_surface_material(init);
    mesh->set_material_override(init);
    godot::SurfaceMaterial* shared = godot::MaterialLibrary::get("res://misc/mat.tres", init);
    mesh->bind_material_override(shared);
    EXPECT_EQ(mesh->get_active_surface_texture(0), "inline_textures/misc_display_rounds.png");
    shared->albedo_texture = "inline_textures/misc_display_rounds_endless_png.png";
    EXPECT_EQ(mesh->get_active_surface_texture(0), "inline_textures/misc_display_rounds_endless_png.png");
    mesh->set_override_albedo_texture("inline_textures/x.png");
    EXPECT_EQ(shared->albedo_texture, "inline_textures/x.png") << "writes go through the shared resource";
}
