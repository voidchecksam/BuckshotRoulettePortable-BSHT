// Offline room baker.
//
// Turns the extracted .glb library (over 200 MB, most of it embedded PNG and
// TANGENT data the renderer never touches) into a .room file the Vita can
// fread and upload with no parsing. Doing the glTF parse, the light bake and
// the batch merge on a 333 MHz Cortex-A9 would cost a long load every launch.
//
// Run from the repo root:
//   bake_room [--scene NAME] [--radius R] [--out FILE] [--exposure E]
//             [--extracted DIR] [--baked DIR] [--no-pages]
//
// --scene menu bakes assets/extracted/*_menu.json into assets/baked/menu.room.
// What only main.tscn has (the shell and item templates, the reveal poses, the
// walk into the backroom, the probe grid, the four animated lights) is main's
// scene profile; any other scene bakes its static lights, props and pages,
// graded by its own Environment, lit by its ambient, with its current camera
// as the eye.
//
// The full world is the default: the scene holds every location the game
// visits, and the dealer waits at x = 60.26 until `dealer return to table`.
// --radius builds only the play area, as a diagnostic.
#include "bake_main.h"

#include "assets/dyn_lights.h"
#include "assets/moving_lights.h"
#include "assets/offstage.h"
#include "assets/scene_fog.h"
#include "assets/animated_nodes.h"
#include "assets/inline_meshes.h"
#include "assets/light_model.h"
#include "assets/room_file.h"
#include "core/mat4.h"
#include "godot/animation_player.h"
#include "godot/packed_scene.h"
#include "godot/material.h"
#include "godot/runtime_scene_loader.h"
#include "render/prop_visibility.h"
#include "scripts/GlobalVariables.h"
#include "scripts/bind_support.h"
#include "scripts/item_scenes.h"

#include "bake_surfaces.h"
#include "bake_textures.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

namespace {

// Where the exports are read and the rooms written; bsht-extract points
// both into its work folder.
std::string gExtracted = "assets/extracted";
std::string gBaked = "assets/baked";

// Diagnostic only (--radius). The dealer's waiting position is x = 60.26, just
// outside this radius, so a filtered bake has no dealer.
constexpr float kDefaultPlayRadius = 60.0f;

std::string Join(const char* a, const char* b) {
    return std::string(a) + "/" + b;
}

void Report(const char* label, const assets::Room& room) {
    const assets::RoomStats& s = room.Stats();
    const assets::RoomFileStats f = assets::MeasureRoom(room);
    std::printf("%-12s %5d batches  %4d props  %7d tris  %7lu verts  %8.2f MB\n",
                label, s.batches, s.props, s.triangles,
                static_cast<unsigned long>(f.vertices),
                static_cast<double>(f.bytes) / (1024.0 * 1024.0));
}

// Luminance histogram of the baked vertex colours.
//
// "It looks too bright" is not actionable and "it looks right now" is not
// evidence. This turns the light bake into a number: how much of the room is
// pinned at white, which is the failure mode when 29 omni lights are summed
// and clamped.
void ReportExposure(const assets::Room& room) {
    int buckets[10] = {0};
    long clipped = 0;
    long total = 0;
    double sum = 0.0;

    auto tally = [&](const assets::Mesh& m) {
        for (std::size_t i = 0; i + 2 < m.colours.size(); i += 4) {
            // colours are uint8 RGBA, not normalised floats.
            const float r = static_cast<float>(m.colours[i]) / 255.0f;
            const float g = static_cast<float>(m.colours[i + 1]) / 255.0f;
            const float b = static_cast<float>(m.colours[i + 2]) / 255.0f;
            const float y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            int slot = static_cast<int>(y * 10.0f);
            if (slot < 0) slot = 0;
            if (slot > 9) slot = 9;
            ++buckets[slot];
            if (y >= 0.99f) ++clipped;
            sum += y;
            ++total;
        }
    };

    for (const assets::RoomBatch& b : room.Batches()) tally(b.mesh);
    for (const assets::RoomProp& p : room.Props())    tally(p.mesh);
    if (total == 0) {
        std::printf("exposure: no vertex colours to measure\n");
        return;
    }

    std::printf("exposure: mean luminance %.3f, %.1f%% of vertices clipped to white\n",
                sum / static_cast<double>(total),
                100.0 * static_cast<double>(clipped) / static_cast<double>(total));
    std::printf("          ");
    for (int i = 0; i < 10; ++i) {
        std::printf("%.1f-%.1f:%4.1f%%  ", i * 0.1, (i + 1) * 0.1,
                    100.0 * static_cast<double>(buckets[i]) /
                        static_cast<double>(total));
        if (i == 4) std::printf("\n");
    }
    std::printf("\n");
}

}  // namespace

// The texture each surface is drawn with in Godot: the material override
// wins, else the surface's own material (export_runtime_scene.gd records a
// texture only when the SCENE's material sets one).
static std::vector<std::string> SurfaceTextures(const godot::MeshInstance3D& mesh) {
    std::vector<std::string> out;
    for (int i = 0; i < mesh.surface_count(); ++i) {
        out.push_back(mesh.has_material_override() ? mesh.get_material_override().albedo_texture
                                                   : mesh.get_surface_material(i).albedo_texture);
    }
    return out;
}

// A runtime surface material as the surface bake reads it.
assets::SurfaceMaterial ToSurfaceMaterial(const godot::SurfaceMaterial& m) {
    assets::SurfaceMaterial out;
    out.albedo[0] = m.albedo.r;
    out.albedo[1] = m.albedo.g;
    out.albedo[2] = m.albedo.b;
    out.alpha = m.albedo.a;
    out.roughness = m.roughness;
    out.metallic = m.metallic;
    out.unshaded = m.shading_mode == 0;
    out.normalScale = m.normal_scale;
    // The colour is sRGB (source_color); linearise it before the energy.
    out.emission[0] = assets::light::SrgbToLinear(m.emission_color.r) * m.emission_energy;
    out.emission[1] = assets::light::SrgbToLinear(m.emission_color.g) * m.emission_energy;
    out.emission[2] = assets::light::SrgbToLinear(m.emission_color.b) * m.emission_energy;
    return out;
}

// The materials the translated scripts swap onto a surface at runtime, per
// (mesh, surface), transcribed from their set_surface_override_material calls:
//   ShellBranch.gd:16/18           `mesh` surface 1: mat_live / mat_blank
//   ShellExamine.gd:18/20          `mesh` surface 1: mat_live / mat_blank
//   ShellEjectManager.gd:17-61     `mesh` surface 0: mat_brass; 1: mat_live / mat_blank
// v2.2 repaints every live/blank material at ready (ColorblindManager.gd,
// ShellEjectManager.gd:18-19) to GlobalVariables' default or colour-blind
// pair, so those are the colours baked, not the .tres's own.
//   BlinkerBranch.gd:19/21         its parent, surface 0: mat_1 / mat_2
// and the texture-only swaps, keyed by texture as well:
//   EndlessMode.gd:24              every mesh drawing `display` (a shared .tres):
//                                  that material with albedo_texture = displayrounds
//   CrtIcon.gd:17-22               its parent's material_override, surface 0:
//                                  albedo_texture = texture_active / texture_inactive
// `root` resolves the scripts' node paths: the scene root, or an instance's.
using SwapKey = std::pair<const godot::Node*, int>;

// The texture-only swaps above, appended to `out`.
void CollectTextureSwaps(godot::Node* root, std::map<SwapKey, std::vector<godot::SurfaceMaterial>>& out) {
    std::vector<godot::Node*> all;
    std::vector<godot::Node*> pending{root};
    while (!pending.empty()) {
        godot::Node* n = pending.back();
        pending.pop_back();
        all.push_back(n);
        for (int i = 0; i < n->get_child_count(); ++i) pending.push_back(n->get_child(i));
    }
    for (godot::Node* n : all) {
        const scripts::ExportReader ex(n->get_exports_json());
        if (n->get_script_file() == "CrtIcon") {
            auto* g = godot::cast_to<godot::GeometryInstance3D>(n->get_parent());
            if (!g || !g->has_material_override()) {
                std::printf("WARNING: CrtIcon at %s has no material_override parent\n", n->get_path().c_str());
                continue;
            }
            std::vector<godot::SurfaceMaterial>& list = out[{g, 0}];
            for (const char* key : {"texture_active", "texture_inactive"}) {
                godot::SurfaceMaterial m = g->get_material_override();
                m.albedo_texture = godot::TextureKeyForResource(ex.res_path(key));
                list.push_back(m);
            }
        } else if (n->get_script_file() == "ShotgunShooting") {
            // BloodSplatter swaps four textures onto mat_splatter's override.
            const std::string path = ex.node_path("mat_splatter");
            auto* g = path.empty() ? nullptr
                                   : godot::cast_to<godot::GeometryInstance3D>(scripts::Binder(root, "ShotgunShooting", nullptr).resolve(path));
            if (!g || !g->has_material_override()) {
                std::printf("WARNING: ShotgunShooting.mat_splatter has no material_override\n");
                continue;
            }
            std::vector<godot::SurfaceMaterial>& list = out[{g, 0}];
            for (const std::string& res : ex.res_paths("splatters")) {
                godot::SurfaceMaterial m = g->get_material_override();
                m.albedo_texture = godot::TextureKeyForResource(res);
                list.push_back(m);
            }
        } else if (n->get_script_file() == "EndlessMode") {
            const std::string res = ex.res_path("display");
            const godot::SurfaceMaterial* shared = godot::MaterialLibrary::get(res, ex.material("display"));
            godot::SurfaceMaterial m = *shared;
            m.albedo_texture = godot::TextureKeyForResource(ex.res_path("displayrounds"));
            int meshes = 0;
            for (godot::Node* t : all) {
                auto* g = godot::cast_to<godot::GeometryInstance3D>(t);
                if (!g || !g->has_material_override() || &g->get_material_override() != shared) continue;
                std::vector<godot::SurfaceMaterial>& list = out[{g, 0}];
                list.push_back(*shared);   // the display at load, so the pair is complete
                list.push_back(m);
                ++meshes;
            }
            if (meshes == 0) std::printf("WARNING: no mesh draws EndlessMode.display (%s)\n", res.c_str());
        }
    }
}
void CollectMaterialSwaps(godot::Node* root, std::map<SwapKey, std::vector<godot::SurfaceMaterial>>& out) {
    struct Rule {
        const char* script;
        bool parent;
        int surface;
        std::vector<const char*> mats;
        bool shellColours = false;   // mat_live / mat_blank, repainted at ready
    };
    static const Rule kRules[] = {
        {"ShellBranch", false, 1, {"mat_live", "mat_blank"}, true},
        {"ShellExamine", false, 1, {"mat_live", "mat_blank"}, true},
        {"ShellEjectManager", false, 0, {"mat_brass"}},
        {"ShellEjectManager", false, 1, {"mat_live", "mat_blank"}, true},
        {"BlinkerBranch", true, 0, {"mat_1", "mat_2"}},
    };
    using scripts::GlobalVariables;
    std::vector<godot::Node*> pending{root};
    while (!pending.empty()) {
        godot::Node* n = pending.back();
        pending.pop_back();
        for (int i = 0; i < n->get_child_count(); ++i) pending.push_back(n->get_child(i));
        for (const Rule& r : kRules) {
            if (n->get_script_file() != r.script) continue;
            const scripts::ExportReader ex(n->get_exports_json());
            const godot::Node* target = r.parent ? n->get_parent() : nullptr;
            if (!r.parent) {
                const std::string path = ex.node_path("mesh");
                // Resolved from `root` exactly as the scripts' bindings resolve it.
                target = path.empty() ? nullptr : scripts::Binder(root, r.script, nullptr).resolve(path);
            }
            if (!target) {
                std::printf("WARNING: %s at %s swaps materials onto a mesh that does not resolve\n", r.script,
                            n->get_path().c_str());
                continue;
            }
            std::vector<godot::SurfaceMaterial>& list = out[{target, r.surface}];
            for (const char* key : r.mats) {
                if (!r.shellColours) {
                    list.push_back(ex.material(key));
                    continue;
                }
                const bool live = std::string(key) == "mat_live";
                for (const godot::Color& c :
                     {live ? GlobalVariables::default_color_live : GlobalVariables::default_color_blank,
                      live ? GlobalVariables::colorblind_color_live : GlobalVariables::colorblind_color_blank}) {
                    godot::SurfaceMaterial m = ex.material(key);
                    m.albedo = c;
                    list.push_back(m);
                }
            }
        }
    }
}

// Screens whose icons read like text (the health counter, the round counter)
// are baked at their texture's own detail, not the tier cap.
// 0: the tier cap; 1: a screen, at its texture's density up to 256/u; 2: a
// casing, the same up to 128/u (the CRT's "KONI" and buttons; at 256 the
// whole CRT costs 3.5 MB over the page budget).
int DetailLevel(const std::string& nodePath) {
    static const char* const kScreens[] = {
        "tabletop parent/main tabletop/health counter/",
        "restroom_CLUB/bathroom wall main_crt hole/crt main parent/crt screen main/",
    };
    for (const char* prefix : kScreens) {
        if (nodePath.rfind(prefix, 0) == 0) return 1;
    }
    if (nodePath.rfind("restroom_CLUB/bathroom wall main_crt hole/crt main parent/", 0) == 0) return 2;
    // The blood splatter plane fills the view (a Camera child, 1.35 u ahead):
    // at the tier cap its 1920x1080 frames were a few dozen blocky texels.
    if (nodePath == "Camera/blood splatter plane") return 2;
    // The cigarette pack is held up to the camera, where Godot's "KLASSIKA
    // FILTER" reads. At 128/u (a casing) it fits the shared pages but the
    // label still blurs; 256/u costs a page over the budget.
    if (nodePath.rfind("player item interaction parent/cigarette box pivot parent/", 0) == 0) return 2;
    return 0;
}

bool IsDetailSurface(const std::string& nodePath) { return DetailLevel(nodePath) != 0; }

// The car and the highway behind it: seen only in the ending, under its grade
// and from its camera.
bool InEndingScope(const std::string& path) {
    return assets::InMovingLightScope(path) || path.rfind("player vehicle backdrop parent/", 0) == 0;
}

// Whether geometry casts its shadow into the static bake. Not when Godot would
// not shadow it at rest: casting off, no render layer (shown later by a
// script), fully transparent (GeometryInstance3D.transparency fades the shadow
// too, so the five 3D brackets ButtonClass_Main hides at 1 cast nothing), or
// moved by animation (its shadow would stay where it rested: the shotgun, the
// waiver).
bool CastsIntoBake(const godot::GeometryInstance3D* g, bool moves) {
    return g && g->get_cast_shadow() != 0 && g->get_layers() != 0 && g->get_transparency() < 1.0f && !moves;
}

// The material Godot draws `surface` with: the override, else the surface's
// own (the exporter already resolved surface overrides into it).
const godot::SurfaceMaterial* ActiveMaterial(const godot::GeometryInstance3D& g, int surface) {
    if (g.has_material_override()) return &g.get_material_override();
    if (surface >= 0 && surface < g.surface_count()) return &g.get_surface_material(surface);
    return nullptr;
}

// Every light Godot draws with at rest: visible in the tree, energy above 0.
// The animated lights sit at 0 and are handled separately.
std::vector<assets::BakedLight> RuntimeLights(godot::SceneTree& runtime) {
    std::vector<assets::BakedLight> lights;
    for (godot::Node* n : runtime.nodes_in_tree_order()) {
        const auto* l = godot::cast_to<godot::OmniLight3D>(n);
        // Visible, burning, not the moving light (lit per vertex) and not an
        // animated one (the DYN channels), whatever pose a reveal left.
        if (!l || !assets::BakesAsStaticLight(l->get_path(), l->is_visible_in_tree(), l->get_light_energy())) continue;
        std::printf("bake lights: static %s, energy %.2f\n", l->get_path().c_str(),
                    static_cast<double>(l->get_light_energy()));
        assets::BakedLight b;
        const core::Mat4 w = l->get_global_transform();
        b.position[0] = w.Tx();
        b.position[1] = w.Ty();
        b.position[2] = w.Tz();
        const godot::Color c = l->get_light_color();
        const float sign = l->is_negative() ? -1.0f : 1.0f;
        b.colour[0] = sign * assets::light::SrgbToLinear(c.r);
        b.colour[1] = sign * assets::light::SrgbToLinear(c.g);
        b.colour[2] = sign * assets::light::SrgbToLinear(c.b);
        b.energy = l->get_light_energy();
        b.range = l->get_omni_range();
        b.attenuation = l->get_omni_attenuation();
        b.shadow = l->has_shadow();
        b.size = l->get_light_size();
        b.cullMask = l->get_cull_mask();
        lights.push_back(b);
    }
    return lights;
}

// Diagnostic for --probe-light: the static light at the camera-facing side
// of `nodePath`'s geometry, light by light, with batches alone as shadow
// casters and with the visible props added. `eye` is the camera it faces:
// the table's home socket unless --probe-eye names another (the car ending's).
bool ProbeLight(const assets::Room& room, const std::vector<assets::BakedLight>& lights,
                const std::string& nodePath, uint32_t layers, const float eye[3]) {
    auto triangles = [&](bool withProps) {
        std::vector<float> tris;
        auto add = [&](const assets::Mesh& m, const core::Mat4& w) {
            for (uint16_t i : m.indices) {
                const float* p = &m.positions[static_cast<std::size_t>(i) * 3];
                for (int r = 0; r < 3; ++r) {
                    tris.push_back(w.m[r] * p[0] + w.m[4 + r] * p[1] + w.m[8 + r] * p[2] + w.m[12 + r]);
                }
            }
        };
        for (const assets::RoomBatch& b : room.Batches()) add(b.mesh, core::Mat4{});
        if (withProps) {
            for (const assets::RoomProp& p : room.Props()) {
                if (p.visibleAtLoad && !p.IsTemplate() && p.nodePath != nodePath) add(p.mesh, p.bakeWorld);
            }
        }
        return tris;
    };
    // The prop's highest camera-facing vertex.
    float pos[3] = {0, 0, 0}, nrm[3] = {0, 0, 0};
    int n = 0;
    float bestFacing = -1.0f;
    // The node is a prop, or was merged into batches (triSource names it).
    std::vector<std::pair<const assets::Mesh*, core::Mat4>> meshes;
    std::vector<std::vector<uint16_t>> ownVerts;
    for (const assets::RoomProp& p : room.Props()) {
        if (p.nodePath == nodePath) {
            meshes.emplace_back(&p.mesh, p.bakeWorld);
            ownVerts.emplace_back();
        }
    }
    for (const assets::RoomBatch& b : room.Batches()) {
        std::vector<uint16_t> verts;
        for (std::size_t t = 0; t < b.triSource.size(); ++t) {
            if (room.Sources()[static_cast<std::size_t>(b.triSource[t])].nodePath != nodePath) continue;
            for (int k = 0; k < 3; ++k) verts.push_back(b.mesh.indices[t * 3 + k]);
        }
        if (verts.empty()) continue;
        const assets::BakedTexture* bt =
            b.texture >= 0 ? &room.Textures().Textures()[static_cast<std::size_t>(b.texture)] : nullptr;
        std::printf("probe-light: %lu triangles merged into a batch of %s material %d, texture %d (%s material %d)\n",
                    static_cast<unsigned long>(verts.size() / 3), b.sourceGlb.c_str(), b.materialIndex, b.texture,
                    bt ? bt->sourceGlb.c_str() : "-", bt ? bt->materialIndex : -1);
        meshes.emplace_back(&b.mesh, core::Mat4{});
        ownVerts.push_back(std::move(verts));
    }
    for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
        const assets::Mesh& mesh = *meshes[mi].first;
        const core::Mat4& w = meshes[mi].second;
        std::vector<uint16_t> all;
        if (ownVerts[mi].empty()) {
            for (int v = 0; v < mesh.VertexCount(); ++v) all.push_back(static_cast<uint16_t>(v));
        }
        for (uint16_t v : ownVerts[mi].empty() ? all : ownVerts[mi]) {
            const float* q = &mesh.positions[static_cast<std::size_t>(v) * 3];
            const float* nn = &mesh.normals[static_cast<std::size_t>(v) * 3];
            float wp[3], wn[3];
            for (int r = 0; r < 3; ++r) {
                wp[r] = w.m[r] * q[0] + w.m[4 + r] * q[1] + w.m[8 + r] * q[2] + w.m[12 + r];
                wn[r] = w.m[r] * nn[0] + w.m[4 + r] * nn[1] + w.m[8 + r] * nn[2];
            }
            const float len = std::sqrt(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
            float toEye[3] = {eye[0] - wp[0], eye[1] - wp[1], eye[2] - wp[2]};
            const float el = std::sqrt(toEye[0] * toEye[0] + toEye[1] * toEye[1] + toEye[2] * toEye[2]);
            if (len <= 0 || el <= 0) continue;
            const float facing = (wn[0] * toEye[0] + wn[1] * toEye[1] + wn[2] * toEye[2]) / (len * el);
            if (facing < 0.3f) continue;
            // The vertex facing the camera most squarely: a real point on the
            // surface (an average of a whole face can fall inside the model).
            if (n == 0 || facing > bestFacing) {
                bestFacing = facing;
                for (int r = 0; r < 3; ++r) { pos[r] = wp[r]; nrm[r] = wn[r] / len; }
            }
            ++n;
        }
    }
    if (n == 0) {
        std::printf("probe-light: no camera-facing vertices for %s\n", nodePath.c_str());
        return false;
    }
    std::printf("probe-light: %s, %d facing vertices, point (%.2f %.2f %.2f) normal (%.2f %.2f %.2f)\n",
                nodePath.c_str(), n, pos[0], pos[1], pos[2], nrm[0], nrm[1], nrm[2]);
    assets::LightBvh batchesOnly, withProps;
    batchesOnly.Build(triangles(false));
    withProps.Build(triangles(true));
    for (const assets::BakedLight& l : lights) {
        float a[3], b[3];
        assets::StaticLight({l}, batchesOnly, pos, nrm, eye, 1.0f, a, layers);
        assets::StaticLight({l}, withProps, pos, nrm, eye, 1.0f, b, layers);
        const float dx = l.position[0] - pos[0], dy = l.position[1] - pos[1], dz = l.position[2] - pos[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist > l.range) continue;
        std::printf("  light at (%6.2f %6.2f %6.2f) d %5.1f e %6.2f r %5.1f shadow %d: batches-only %.3f %.3f %.3f  +props %.3f %.3f %.3f\n",
                    l.position[0], l.position[1], l.position[2], dist, l.energy, l.range, l.shadow ? 1 : 0,
                    a[0], a[1], a[2], b[0], b[1], b[2]);
    }
    return true;
}

// "scene_graph.json" -> "scene_graph_menu.json" for a scene that is not main.
std::string SceneFile(const std::string& scene, const char* base) {
    if (scene == "main") return base;
    const std::string b(base);
    const std::size_t dot = b.rfind('.');
    return b.substr(0, dot) + "_" + scene + b.substr(dot);
}

int BakeRoomMain(int argc, char** argv) {
    float radius = kDefaultPlayRadius;
    float exposure = -1.0f;
    std::string out;
    std::string sceneName = "main";
    bool full = true;
    // The room's albedo is downscaled to this. 128 keeps 123 materials inside a
    // few MB against ~85 MB of Vita VRAM, and suits a deliberately coarse look.
    int texEdge = 128;
    bool noInline = false;
    // Every surface is baked to its graded colour and the pages are the room's
    // textures (format v8). The pages are also written to build/pages for
    // inspection. --legacy-albedo skips the bake (fast, for debugging the
    // geometry only): the renderer then draws raw 128 px albedo, unlit.
    bool surfaces = true;
    bool surfacesPlan = false;   // stop after reporting the texel budget
    // Diagnostic: per-light static light at the camera-facing side of one
    // prop, with and without props as shadow casters, then exit.
    std::string probeLight;
    std::string pagesDir = "build/pages";   // "" with --no-pages
    float probeEye[3] = {assets::kHomeSocket[0], assets::kHomeSocket[1], assets::kHomeSocket[2]};

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--radius" && i + 1 < argc)        { radius = static_cast<float>(std::atof(argv[++i])); full = false; }
        else if (a == "--out" && i + 1 < argc)      out = argv[++i];
        else if (a == "--exposure" && i + 1 < argc) exposure = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--full")                     full = true;
        else if (a == "--tex-size" && i + 1 < argc) texEdge = std::atoi(argv[++i]);
        else if (a == "--no-textures")              texEdge = 0;
        // Diagnostic: the 76 inline meshes carry no glTF material, so they are
        // the first suspect whenever untextured white geometry appears.
        else if (a == "--no-inline")                noInline = true;
        else if (a == "--surfaces")                 surfaces = true;
        else if (a == "--legacy-albedo")            surfaces = false;
        else if (a == "--surfaces-plan")            surfaces = surfacesPlan = true;
        else if (a == "--probe-light" && i + 1 < argc) { probeLight = argv[++i]; surfaces = true; }
        else if (a == "--probe-eye" && i + 1 < argc &&
                 std::sscanf(argv[++i], "%f,%f,%f", &probeEye[0], &probeEye[1], &probeEye[2]) != 3) {
            std::printf("--probe-eye wants x,y,z\n");
            return 1;
        }
        else if (a == "--scene" && i + 1 < argc)    sceneName = argv[++i];
        else if (a == "--extracted" && i + 1 < argc) gExtracted = argv[++i];
        else if (a == "--baked" && i + 1 < argc)    gBaked = argv[++i];
        else if (a == "--no-pages")                 pagesDir.clear();
    }
    const bool isMain = sceneName == "main";
    if (out.empty()) out = gBaked + (isMain ? "/full_world.room" : "/" + sceneName + ".room");
    std::printf("scene: %s -> %s\n", sceneName.c_str(), out.c_str());

    assets::Scene scene;
    const std::string scenePath = Join(gExtracted.c_str(), SceneFile(sceneName, "scene_graph.json").c_str());
    if (!scene.LoadFromFile(scenePath.c_str())) {
        std::printf("FATAL: could not load %s (run from the repo root)\n",
                    scenePath.c_str());
        return 1;
    }

    assets::MeshIndex index;
    const std::string indexPath = Join(gExtracted.c_str(), "models/_mesh_names.json");
    if (!index.LoadFromFile(indexPath.c_str())) {
        std::printf("FATAL: could not load %s\n", indexPath.c_str());
        std::printf("       run tools/godot/export_scene_meshes.gd first\n");
        return 1;
    }

    // Animated nodes must be held out of the merge or they cannot move: a
    // node baked into a shared world-space batch has no per-node handle left.
    assets::AnimationLibrary anims;
    assets::AnimatedNodeSet animated;
    const std::string animPath = Join(gExtracted.c_str(), SceneFile(sceneName, "animations.json").c_str());
    if (anims.LoadFromFile(animPath.c_str())) {
        animated.Build(anims);
        std::printf("animation: %d players, %d animations, %d tracks; "
                    "%lu nodes need their own identity\n",
                    anims.Stats().players, anims.Stats().animations,
                    anims.Stats().tracks,
                    static_cast<unsigned long>(animated.Size()));
    } else {
        // Loud, because the resulting room would look correct and be
        // completely static.
        std::printf("WARNING: %s missing -- every prop will be merged and "
                    "NOTHING will be able to move\n", animPath.c_str());
        std::printf("         run tools/godot/export_animations.gd\n");
    }

    // Geometry for the nodes whose meshes have no name and are in no .glb.
    // Without it they are counted and never drawn.
    assets::InlineMeshLibrary inlineMeshes;
    const std::string inlinePath = Join(gExtracted.c_str(), SceneFile(sceneName, "inline_meshes.json").c_str());
    if (inlineMeshes.LoadFromFile(inlinePath.c_str())) {
        std::printf("inline: %d nodes, %d surfaces, %d triangles\n",
                    inlineMeshes.Stats().nodes, inlineMeshes.Stats().surfaces,
                    inlineMeshes.Stats().triangles);
    } else {
        std::printf("WARNING: %s missing -- 76 nodes will be holes in the room\n",
                    inlinePath.c_str());
        std::printf("         run tools/godot/export_inline_meshes.gd\n");
    }

    // What Godot does not draw at load, from the live export and by the
    // renderer's own rule. Such geometry must stay a prop: merged, it has no
    // handle the renderer could hide, and scene_graph.json alone cannot see
    // layers 0 (the crushed head) or transparency 1 (the waiver highlight).
    std::unordered_set<std::string> notDrawn;
    godot::SceneTree runtime;
    const std::string runtimePath = Join(gExtracted.c_str(), ("runtime_" + sceneName + ".json").c_str());
    const godot::RuntimeSceneError runtimeErr =
        godot::LoadRuntimeScene(runtimePath.c_str(), runtime);
    if (runtimeErr != godot::RuntimeSceneError::None) {
        std::printf("FATAL: %s: %s\n", runtimePath.c_str(),
                    godot::RuntimeSceneErrorText(runtimeErr));
        std::printf("       run tools/godot/export_runtime_scene.gd\n");
        return 1;
    }
    const godot::Camera3D* camera = runtime.get_current_camera();
    const uint32_t cullMask = camera ? camera->get_cull_mask() : 0xFFFFFu;
    for (godot::Node* n : runtime.nodes_in_tree_order()) {
        const godot::GeometryInstance3D* g = godot::cast_to<godot::GeometryInstance3D>(n);
        if (g && !render::PropIsDrawn(*g, cullMask)) notDrawn.insert(g->get_path());
    }
    std::printf("runtime: %lu nodes, %lu geometry nodes Godot does not draw at load\n",
                static_cast<unsigned long>(runtime.get_node_count()),
                static_cast<unsigned long>(notDrawn.size()));

    // A translated script may write its PARENT's material: BlinkerBranch
    // flashes surface 0 of the mesh it hangs under, 112 times over. Merged,
    // that mesh has no per-node handle and could never change colour.
    std::unordered_set<std::string> scriptDriven;
    for (godot::Node* n : runtime.nodes_in_tree_order()) {
        if (n->get_script_file() != "BlinkerBranch") continue;
        if (const godot::Node* parent = n->get_parent()) scriptDriven.insert(parent->get_path());
    }
    // CrtManager.SetCRT swaps the restroom wall for the broken one:
    // bathroom_normal off layer 1, objarray_normal hidden, objarray_broken and
    // bathroom_broken shown. Merged, the normal wall, mirror and faucets could
    // never go, and would draw over the broken ones.
    for (godot::Node* n : runtime.nodes_in_tree_order()) {
        if (n->get_script_file() != "CrtManager") continue;
        const scripts::ExportReader ex(n->get_exports_json());
        const scripts::Binder binder(runtime.get_root(), "CrtManager", nullptr);
        std::vector<std::string> roots;
        for (const char* key : {"bathroom_normal", "bathroom_broken"}) roots.push_back(ex.node_path(key));
        for (const char* key : {"objarray_normal", "objarray_broken"}) {
            for (const std::string& p : ex.node_paths(key)) roots.push_back(p);
        }
        for (const std::string& path : roots) {
            godot::Node* root = path.empty() ? nullptr : binder.resolve(path);
            if (!root) {
                std::printf("WARNING: CrtManager toggles %s, which does not resolve\n", path.c_str());
                continue;
            }
            // The node itself (bathroom_normal's layer is its OWN) and every
            // mesh under it (visibility hides the subtree).
            std::vector<godot::Node*> pending{root};
            while (!pending.empty()) {
                godot::Node* m = pending.back();
                pending.pop_back();
                if (godot::cast_to<godot::GeometryInstance3D>(m)) scriptDriven.insert(m->get_path());
                for (int i = 0; i < m->get_child_count(); ++i) pending.push_back(m->get_child(i));
            }
        }
    }
    // A detail surface is planned per prop, so a detail mesh merged into a
    // batch would keep the tier cap: keep every one a prop.
    for (godot::Node* n : runtime.nodes_in_tree_order()) {
        if (godot::cast_to<godot::GeometryInstance3D>(n) && IsDetailSurface(n->get_path())) {
            scriptDriven.insert(n->get_path());
        }
        // The moving light's scope (the car) stays props: the renderer lights
        // only them with it (assets/moving_lights.h).
        if (godot::cast_to<godot::GeometryInstance3D>(n) && assets::InMovingLightScope(n->get_path())) {
            scriptDriven.insert(n->get_path());
        }
        // A skinned mesh is skinned per frame, so it is never merged.
        if (const auto* mi = godot::cast_to<godot::MeshInstance3D>(n); mi && mi->has_skin()) {
            scriptDriven.insert(n->get_path());
        }
    }
    std::printf("runtime: %lu meshes a script recolours or toggles\n",
                static_cast<unsigned long>(scriptDriven.size()));

    assets::RoomBuildOptions opts;
    opts.modelDir = Join(gExtracted.c_str(), "models");
    opts.scriptDriven = &scriptDriven;
    if (inlineMeshes.Size() > 0 && !noInline) opts.inlineMeshes = &inlineMeshes;
    if (animated.Size() > 0) opts.animated = &animated;
    opts.notDrawnAtLoad = &notDrawn;
    // Scripts show geometry that has no animation (the health counter's
    // digits, the shells). Hidden at load is not hidden forever.
    opts.keepHiddenAsProps = true;
    if (exposure >= 0.0f) opts.bake.exposure = exposure;
    if (!full) opts.filter.radius = radius;

    assets::Room room;
    if (!room.Build(scene, index, opts)) {
        std::printf("FATAL: room built empty\n");
        return 1;
    }

    const assets::RoomStats& s = room.Stats();
    std::printf("nodes: %d considered, %d drawn, %d outside filter, "
                "%d inline (%d drawn, %d with no geometry), %d hidden, "
                "%d unresolved\n",
                s.nodesConsidered, s.nodesDrawn, s.outsideFilter,
                s.inlinePrimitives, s.inlineDrawn, s.inlineMissing,
                s.invisible, s.unresolved);
    if (s.Accounted() != s.nodesConsidered) {
        // Every node lands in exactly one bucket. If that stops being true, a
        // chunk of room goes missing with nothing saying so.
        std::printf("FATAL: accounting does not close: %d of %d nodes\n",
                    s.Accounted(), s.nodesConsidered);
        return 1;
    }
    std::printf("models loaded from disk: %d\n", s.modelsLoaded);

    // Which runtime mesh each template prop came from, for its material. The
    // instances are kept alive until the surface bake reads them.
    std::unordered_map<std::size_t, const godot::MeshInstance3D*> templateMesh;
    std::vector<std::unique_ptr<godot::Node>> keepInstances;

    // Template props: instances/shell.tscn's meshes, baked once and drawn for
    // every shell ShellSpawner.SpawnShells instantiates. Main's profile only.
    // PORT: they are lit with the shell at the first compartment slot, posed
    // as SpawnShells poses it, where the shells are first seen.
    if (isMain) {
        const std::string shellPath = Join(gExtracted.c_str(), "runtime_shell.json");
        const godot::PackedScene* shell =
            godot::PackedScene::load("res://instances/shell.tscn", shellPath.c_str());
        godot::Node* spawner = runtime.get_node("standalone managers/shell spawner");
        const auto* compartment =
            runtime.get_node_as<godot::Node3D>("tabletop parent/main tabletop/compartment_shells");
        if (!shell || !spawner || !compartment) {
            std::printf("FATAL: %s, the shell spawner or compartment_shells is missing\n",
                        shellPath.c_str());
            std::printf("       run tools/godot/export_runtime_scene.gd -- instances/shell.tscn "
                        "%s/runtime_shell.json\n", gExtracted.c_str());
            return 1;
        }
        const std::vector<godot::Vector3> slots =
            scripts::ExportReader(spawner->get_exports_json()).vector3s("shellLocationArray");
        std::unique_ptr<godot::Node> instance = shell->instantiate();
        auto* shellRoot = godot::cast_to<godot::Node3D>(instance.get());
        if (!shellRoot || slots.empty()) {
            std::printf("FATAL: the shell scene has no 3D root, or ShellSpawner no slots\n");
            return 1;
        }
        shellRoot->set_position(slots[0]);
        shellRoot->set_rotation_degrees({-90.0f, -90.0f, 180.0f});

        int templates = 0;
        std::vector<godot::Node*> pending{instance.get()};
        while (!pending.empty()) {
            godot::Node* n = pending.back();
            pending.pop_back();
            for (int i = 0; i < n->get_child_count(); ++i) pending.push_back(n->get_child(i));
            const auto* mesh = godot::cast_to<godot::MeshInstance3D>(n);
            if (!mesh || mesh->get_mesh_name().empty()) continue;
            const core::Mat4 world =
                core::Mul(compartment->get_global_transform(), mesh->get_global_transform());
            const std::size_t before = room.Props().size();
            const int added = room.AddTemplateProps(mesh->get_template_key(), mesh->get_mesh_name(),
                                                    index, world, scene, opts, SurfaceTextures(*mesh));
            for (std::size_t pi = before; pi < room.Props().size(); ++pi) templateMesh[pi] = mesh;
            if (added == 0) {
                std::printf("FATAL: template mesh %s (%s) did not resolve\n",
                            mesh->get_mesh_name().c_str(), mesh->get_template_key().c_str());
                return 1;
            }
            templates += added;
        }
        keepInstances.push_back(std::move(instance));
        std::printf("templates: %d props for instances/shell.tscn\n", templates);
    }

    // Template props: the nine item scenes. Items are not main.tscn geometry
    // (ItemManager.GrabItem instantiates instances/item_*.tscn), so each gets
    // the shell's treatment. The scene list is ItemManager's own
    // (scripts::ItemScenePaths), not a table here.
    if (isMain) {
        godot::Node* itemManager = runtime.get_node("standalone managers/item manager");
        if (!itemManager) {
            std::printf("FATAL: standalone managers/item manager is missing from runtime_main.json\n");
            return 1;
        }
        const scripts::ExportReader managerEx(itemManager->get_exports_json());
        const auto* spawnParent =
            runtime.get_node_as<godot::Node3D>(managerEx.node_path("itemSpawnParent"));
        if (!spawnParent) {
            std::printf("FATAL: ItemManager.itemSpawnParent does not resolve\n");
            return 1;
        }
        // Where each scene is first seen, from the player's array.
        std::vector<std::pair<std::string, scripts::ExportReader>> poses;
        for (const scripts::ExportReader& r : managerEx.resource_array("instanceArray")) {
            poses.emplace_back(r.res_path("instance"), r);
        }

        const std::vector<std::string> gridPaths = managerEx.node_paths("gridParentArray");
        const auto* gridSlot =
            gridPaths.empty() ? nullptr : runtime.get_node_as<godot::Node3D>(gridPaths.front());
        if (!gridSlot) {
            std::printf("FATAL: ItemManager.gridParentArray[0] does not resolve\n");
            return 1;
        }
        std::printf("templates: items lit in grid slot %s\n", gridPaths.front().c_str());
        const std::vector<std::string> scenes = scripts::ItemScenePaths(*itemManager);
        if (scenes.empty()) {
            std::printf("FATAL: ItemManager names no item scenes\n");
            return 1;
        }
        int itemTemplates = 0;
        for (const std::string& res : scenes) {
            const std::string file = Join(gExtracted.c_str(), scripts::ItemSceneExportFile(res).c_str());
            const godot::PackedScene* item = godot::PackedScene::load(res, file.c_str());
            if (!item) {
                std::printf("FATAL: %s did not load from %s\n", res.c_str(), file.c_str());
                std::printf("       run the exporters (they write runtime_item_*.json)\n");
                return 1;
            }
            std::unique_ptr<godot::Node> instance = item->instantiate();
            auto* itemRoot = godot::cast_to<godot::Node3D>(instance.get());
            if (!itemRoot) {
                std::printf("FATAL: %s has no 3D root\n", res.c_str());
                return 1;
            }
            // Lit where it lies on the table: in the player's first grid slot,
            // placed as ItemManager places it (gd:316-317). Posed in the
            // briefcase instead, every item bakes black, since the compartment
            // rests under the table. The probe ratio carries it to other slots.
            for (const auto& [path, r] : poses) {
                if (path != res) continue;
                if (gridSlot) {
                    const godot::Vector3 o = gridSlot->get_position();
                    const godot::Vector3 d = r.vector3("pos_offset", {});
                    const godot::Vector3 g = gridSlot->get_rotation_degrees();
                    const godot::Vector3 e = r.vector3("rot_offset", {});
                    itemRoot->set_position({o.x + d.x, o.y + d.y, o.z + d.z});
                    itemRoot->set_rotation_degrees({g.x + e.x, g.y + e.y, g.z + e.z});
                } else {
                    itemRoot->set_position(r.vector3("pos_inBriefcase", {}));
                    itemRoot->set_rotation_degrees(r.vector3("rot_inBriefcase", {}));
                }
                break;
            }

            int added = 0;
            std::vector<godot::Node*> pending{instance.get()};
            while (!pending.empty()) {
                godot::Node* n = pending.back();
                pending.pop_back();
                for (int i = 0; i < n->get_child_count(); ++i) pending.push_back(n->get_child(i));
                const auto* mesh = godot::cast_to<godot::MeshInstance3D>(n);
                if (!mesh || mesh->get_mesh_name().empty()) continue;
                const core::Mat4 world =
                    core::Mul(spawnParent->get_global_transform(), mesh->get_global_transform());
                const std::size_t before = room.Props().size();
                const int n_added = room.AddTemplateProps(mesh->get_template_key(),
                                                          mesh->get_mesh_name(),
                                                          index, world, scene, opts, SurfaceTextures(*mesh));
                for (std::size_t pi = before; pi < room.Props().size(); ++pi) templateMesh[pi] = mesh;
                if (n_added == 0) {
                    std::printf("FATAL: item mesh %s (%s) in %s did not resolve\n",
                                mesh->get_mesh_name().c_str(), mesh->get_template_key().c_str(),
                                res.c_str());
                    return 1;
                }
                added += n_added;
            }
            if (added == 0) {
                std::printf("FATAL: %s has no meshes to bake\n", res.c_str());
                return 1;
            }
            itemTemplates += added;
            keepInstances.push_back(std::move(instance));
        }
        std::printf("templates: %d props for %lu item scenes\n", itemTemplates,
                    static_cast<unsigned long>(scenes.size()));
    }

    Report(full ? "FULL WORLD" : "PLAY AREA", room);
    ReportExposure(room);

    // Albedo, decoded and fitted to --tex-size (assets/texture_pool.h). It is
    // what --legacy-albedo draws; without it the room renders white, which
    // looks like a broken light bake and is not one.
    if (texEdge > 0) {
        const tools::TextureBakeStats ts =
            tools::BakeRoomTextures(opts.modelDir, gExtracted, room, room.Textures(), texEdge);
        std::printf("textures: %d materials -> %d decoded, %d shared, "
                    "%d flat colour, %d failed\n",
                    ts.materialsSeen, ts.decoded, ts.shared, ts.colourOnly,
                    ts.failed);
        std::printf("          %lu entries, %.2f MB at %dpx; "
                    "%d/%lu batches and %d/%lu props textured\n",
                    static_cast<unsigned long>(room.Textures().Size()),
                    static_cast<double>(room.Textures().ByteSize()) / 1048576.0,
                    texEdge, ts.batchesTextured,
                    static_cast<unsigned long>(room.Batches().size()),
                    ts.propsTextured,
                    static_cast<unsigned long>(room.Props().size()));
        // Anything still untextured draws WHITE, which on a large surface
        // reads as a lighting bug rather than a missing material. Name them.
        for (const assets::RoomBatch& b : room.Batches()) {
            if (b.texture < 0) {
                std::printf("  untextured batch: %s mat %d, %d tris\n",
                            b.sourceGlb.c_str(), b.materialIndex,
                            b.mesh.TriangleCount());
            }
        }
        for (const assets::RoomProp& pr : room.Props()) {
            if (pr.texture < 0) {
                std::printf("  untextured prop: %s (%s mat %d), %d tris\n",
                            pr.nodePath.c_str(), pr.sourceGlb.c_str(),
                            pr.materialIndex, pr.mesh.TriangleCount());
            }
        }
        // Geometry with NO UVs samples texel (0,0) forever. Before textures
        // that was invisible; now it paints a whole surface with one corner
        // pixel, which is usually white.
        int noUvBatches = 0;
        int noUvTris = 0;
        for (const assets::RoomBatch& b : room.Batches()) {
            if (!b.mesh.HasUvs() && b.mesh.TriangleCount() > 0) {
                ++noUvBatches;
                noUvTris += b.mesh.TriangleCount();
                if (noUvBatches <= 6) {
                    std::printf("  no UVs: batch %s mat %d, %d tris\n",
                                b.sourceGlb.c_str(), b.materialIndex,
                                b.mesh.TriangleCount());
                }
            }
        }
        int noUvProps = 0;
        for (const assets::RoomProp& pr : room.Props()) {
            if (!pr.mesh.HasUvs() && pr.mesh.TriangleCount() > 0) ++noUvProps;
        }
        std::printf("  no UVs: %d batches (%d tris), %d props\n",
                    noUvBatches, noUvTris, noUvProps);
        if (!room.Textures().Consistent()) {
            std::printf("FATAL: texture pool is internally inconsistent\n");
            return 1;
        }
    }

    if (surfaces) {
        // A prop is lit where it is seen, not where it waits. The shell
        // compartment rests flipped face-down inside the table and turns over
        // to show the shells; the dealer waits at x = 60 until he walks in.
        // Each reveal animation is played to its end on the runtime tree, and
        // every prop whose node it moved takes that pose as its bake pose.
        // "pickup waiver": the release form and the signature machine rest out
        // of every light and are carried into view by it (lit only at rest
        // they draw as black slabs). The briefcase machine is the same: "move
        // to table" rolls it in, and the lid's "open" shows the money; the
        // Double or Nothing machine's "show" raises its yes/no keys. A reveal
        // names its player when the animation name is not unique ("open" also
        // swings the defib blades).
        struct Reveal {
            const char* player;   // a suffix of the player's path, or nullptr: every player
            const char* name;
            float at = -1.0f;     // seconds into it; -1: played to its end
            // A node-path prefix: only props under it take THIS pose, captured
            // right after the seek (one animation shows different props at
            // different times). nullptr: every prop it moves, at the end of all
            // reveals. A later matching entry wins.
            const char* only = nullptr;
            // > 0: the animation is sampled at this many evenly spaced times
            // and its `only` props are lit with the MEAN of those poses'
            // light (RoomProp::meanPoses); `at` is ignored.
            int samples = 0;
            // With `only`: the captured pose is ADDED to the poses those props
            // are lit in (RoomProp::extraPoses) instead of replacing their
            // reveal pose, for a prop seen in more than two poses.
            bool extra = false;
            // With `only`: the captured pose is the ONLY one those props are lit
            // in, and they are never rescaled. For items seen only in the
            // player's hands: lit as the brighter of their held and hidden
            // pose, they read far too bright.
            bool sole = false;
        };
        static const Reveal kReveals[] = {
            // Before "pickup waiver", which must leave the machine's tree in its
            // own end pose for the reveals played to their end. The signed form
            // waits where "put away waiver" throws it, out of the room: it is
            // seen on the machine just before the throw. The machine shuts its
            // lid before it leaves, and that face is seen too.
            {"animator_pickup waiver", "put away waiver", 0.7f, "signature machine2/release form signed separate"},
            {"animator_pickup waiver", "put away waiver", 0.65f, "signature machine2/signature machine main parent/", 0,
             true},
            {nullptr, "show shells"}, {nullptr, "show items"}, {nullptr, "show briefcase"},
            {nullptr, "dealer return to table"}, {nullptr, "pickup waiver"},
            {"briefcase machine animator", "move to table"},
            {"briefcase hinge/animator_lid", "open"},
            // The yes/no machine rises out of the table to ask.
            {"animator_double nothing machine", "show"},
            // The menu's shell waterfalls fall five units on a loop; at rest
            // (the top) the 400-energy lamps barely reach them. Each shell is
            // lit with the mean over its fall (8 poses): with the linear-light
            // fog (assets/scene_fog.h) frame 700 reads 6.5 against the halfway
            // pose's 7.3 (Godot 5.7).
            {"shell waterfall2/AnimationPlayer", "main action", -1.0f, "shell waterfall2/", 8},
            {"shell waterfall4/AnimationPlayer", "main action", -1.0f, "shell waterfall4/", 8},
            // The gun rests on its side, so the side the player sees while
            // holding it would be baked face-down in the table's shadow and
            // draw black after a shot (Godot lights it warm). Held up after
            // "player shoot dealer" fires (1.5 s on) is where it is seen.
            {"anim_shotgun main parent", "player shoot dealer", 1.5f},
            // "player use cigarettes" holds the pack up to the camera at
            // 0.3-0.8 s, then lights the cigarette in the mouth with the zippo
            // at 1.63-2.52 s.
            {"animator_player items", "player use cigarettes", 0.5f,
             "player item interaction parent/cigarette box pivot parent/"},
            {"animator_player items", "player use cigarettes", 2.0f,
             "player item interaction parent/cigarette box pivot parent/cigarette box_cigarette unparented"},
            {"animator_player items", "player use cigarettes", 2.0f, "player item interaction parent/zippo lighter parent2/"},
            // The Double or Nothing items wait under the table and are held up
            // to the camera when used; lit where they wait they draw black.
            // The phone is seen shut as it rises and open; the medicine box
            // shut, then open with its blister pack out.
            {"animator_player items", "player use adrenaline", 2.0f, "player item interaction parent/adrenaline", 0, false, true},
            {"animator_player items", "player use burner phone", 0.4f, "player item interaction parent/burner phone"},
            {"animator_player items", "player use burner phone", 1.2f, "player item interaction parent/burner phone", 0,
             true},
            {"animator_player items", "player use expired pills", 0.45f,
             "player item interaction parent/expired medicine parent"},
            {"animator_player items", "player use expired pills", 1.5f,
             "player item interaction parent/expired medicine parent", 0, true},
            {"animator_player items", "player use inverter", 1.2f, "player item interaction parent/inverter parent"},
            // The magnifying glass and the beer can are held up the same way.
            {"animator_player items", "player use magnifier", 1.3f,
             "player item interaction parent/magnifying glass parent", 0, false, true},
            {"animator_player items", "player use beer", 2.0f, "player item interaction parent/beer can pivot parent", 0, false, true},
            // The defibrillator rises out of the health counter on its wire
            // cutters when the last batch begins: "initial setup" lifts it four
            // units, "main setup" swings the blades in. It waits inside the
            // counter, where no light reaches.
            {"animator_defib cutter", "initial setup"},
            {"animator_defib cutter", "main setup"},
        };
        godot::BindAnimationLibrary(runtime, anims);
        // The shell templates were posed in the compartment at ITS rest pose.
        const auto* shellCompartment =
            runtime.get_node_as<godot::Node3D>("tabletop parent/main tabletop/compartment_shells");
        const core::Mat4 compartmentRest =
            shellCompartment ? shellCompartment->get_global_transform() : core::Mat4{};
        std::vector<core::Mat4> restPose(room.Props().size());
        std::vector<int> claimedBy(room.Props().size(), -1);   // kReveals index of an `only` capture
        std::vector<core::Mat4> claimed(room.Props().size());
        std::vector<const godot::Node3D*> propNode(room.Props().size(), nullptr);
        for (std::size_t pi = 0; pi < room.Props().size(); ++pi) {
            const assets::RoomProp& pr = room.Props()[pi];
            if (pr.IsTemplate()) continue;
            propNode[pi] = runtime.get_node_as<godot::Node3D>(pr.nodePath);
            if (propNode[pi]) restPose[pi] = propNode[pi]->get_global_transform();
        }
        for (godot::Node* n : runtime.nodes_in_tree_order()) {
            auto* player = godot::cast_to<godot::AnimationPlayer>(n);
            if (!player) continue;
            const std::string playerPath = player->get_path();
            for (const Reveal& reveal : kReveals) {
                const char* name = reveal.name;
                if (reveal.player) {
                    const std::string suffix = reveal.player;
                    if (playerPath.size() < suffix.size() ||
                        playerPath.compare(playerPath.size() - suffix.size(), suffix.size(), suffix) != 0) {
                        continue;
                    }
                }
                const assets::Animation* anim = player->get_animation(name);
                if (!anim) continue;
                player->play(name);
                if (reveal.samples > 0 && reveal.only) {
                    const std::string prefix = reveal.only;
                    for (int k = 0; k < reveal.samples; ++k) {
                        player->seek(anim->length * (k + 0.5) / reveal.samples, true);
                        for (std::size_t pi = 0; pi < room.Props().size(); ++pi) {
                            if (!propNode[pi] || room.Props()[pi].nodePath.rfind(prefix, 0) != 0) continue;
                            room.Props()[pi].meanPoses.push_back(propNode[pi]->get_global_transform());
                        }
                    }
                    player->seek(0.0, true);   // back to rest: nothing else takes these poses
                    std::printf("bake pose: %s \"%s\" sampled %d times for props under %s\n",
                                player->get_path().c_str(), name, reveal.samples, reveal.only);
                    continue;
                }
                const double at = reveal.at >= 0.0f ? reveal.at : anim->length;
                player->seek(at, true);
                int taken = 0;
                if (reveal.only) {
                    const std::string prefix = reveal.only;
                    for (std::size_t pi = 0; pi < room.Props().size(); ++pi) {
                        if (!propNode[pi] || room.Props()[pi].nodePath.rfind(prefix, 0) != 0) continue;
                        if (reveal.extra) {
                            room.Props()[pi].extraPoses.push_back(propNode[pi]->get_global_transform());
                        } else {
                            claimed[pi] = propNode[pi]->get_global_transform();
                            claimedBy[pi] = static_cast<int>(&reveal - kReveals);
                        }
                        ++taken;
                    }
                }
                std::printf("bake pose: %s \"%s\" played to %.2f s%s\n", player->get_path().c_str(), name,
                            static_cast<double>(at),
                            reveal.only ? (" for " + std::to_string(taken) + " props under " + reveal.only +
                                           (reveal.extra ? " (an extra pose)" : ""))
                                              .c_str()
                                        : "");
            }
        }
        int moved = 0;
        for (std::size_t pi = 0; pi < room.Props().size(); ++pi) {
            if (!propNode[pi]) continue;
            const core::Mat4 seen = claimedBy[pi] >= 0 ? claimed[pi] : propNode[pi]->get_global_transform();
            float change = 0.0f;
            for (int k = 0; k < 16; ++k) change = std::max(change, std::fabs(seen.m[k] - restPose[pi].m[k]));
            if (change < 1e-4f) continue;
            // A reveal moves some things INTO view (the compartments rise) and
            // others OUT of it (their felt lids drop away), and one part can do
            // both: the shell compartment shows its felt at rest and its slotted
            // top once turned over. So the prop keeps BOTH poses and each texel
            // is lit in whichever lights it more (BakeSurfaces). It shadows the
            // room from where it rests.
            assets::RoomProp& pr = room.Props()[pi];
            const bool sole = claimedBy[pi] >= 0 && kReveals[claimedBy[pi]].sole;
            pr.altWorld = pr.bakeWorld;   // its rest pose
            pr.hasAltWorld = !sole;
            pr.castWorld = pr.bakeWorld;
            pr.hasCastWorld = true;
            pr.bakeWorld = seen;
            ++moved;
        }
        // The shells ride with the compartment: apply its reveal to them too.
        if (shellCompartment) {
            const core::Mat4 delta =
                core::Mul(shellCompartment->get_global_transform(), core::InverseAffine(compartmentRest));
            for (assets::RoomProp& pr : room.Props()) {
                if (pr.templateKey.rfind("instances/shell.tscn", 0) != 0) continue;
                pr.bakeWorld = core::Mul(delta, pr.bakeWorld);
                ++moved;
            }
        }
        std::printf("bake pose: %d props lit where their reveal animation leaves them\n", moved);

        // The lights burn at their TABLE values, not their load values. The
        // walk into the backroom ends by switching `lght_backroom main2` off
        // (4.785 -> 0 over "camera enter backroom"), and nothing turns it on
        // again, so every round is played without it. At its load energy of
        // 18.36 it washed the health counter's front pale where Godot's is dark
        // brown. Played after the poses above are taken, so only the lights
        // take the table state from it.
        for (godot::Node* n : runtime.nodes_in_tree_order()) {
            auto* player = godot::cast_to<godot::AnimationPlayer>(n);
            const assets::Animation* anim = player ? player->get_animation("camera enter backroom") : nullptr;
            if (!anim) continue;
            player->play("camera enter backroom");
            player->seek(anim->length, true);
            std::printf("bake lights: %s \"camera enter backroom\" played to its end\n", player->get_path().c_str());
        }

        // Albedo at native size for the surface bake; the 128 px pool above is
        // only what --legacy-albedo draws.
        assets::Room native = room;
        assets::TexturePool nativePool;
        tools::BakeRoomTextures(opts.modelDir, gExtracted, native, nativePool, 8192);
        tools::SurfaceBakeInput in;
        bool fogScene = false;   // the scene's Environment has fog (assets/scene_fog.h)
        float sceneFogDensity = 0.0f;
        in.lights = RuntimeLights(runtime);
        if (!isMain) {
            // This scene's profile: its camera is the eye, its Environment the
            // grade, and an ambient COLOUR (source 2) lights every surface:
            // srgb_to_linear(colour) * energy (render_scene_data_rd.cpp:144-149).
            if (const godot::Camera3D* cam = runtime.get_current_camera()) {
                const core::Mat4 w = cam->get_global_transform();
                in.eye[0] = w.Tx();
                in.eye[1] = w.Ty();
                in.eye[2] = w.Tz();
            }
            for (godot::Node* n : runtime.nodes_in_tree_order()) {
                const auto* env = godot::cast_to<godot::WorldEnvironment>(n);
                if (!env) continue;
                in.grade.brightness = env->get_adjustment_brightness();
                in.grade.contrast = env->get_adjustment_contrast();
                in.grade.saturation = env->get_adjustment_saturation();
                fogScene = env->get_fog_enabled();
                sceneFogDensity = fogScene ? env->get_fog_density() : 0.0f;
                if (env->get_fog_enabled() && assets::FogIsBaked(sceneName)) {
                    const godot::Color f = env->get_fog_light_color();
                    const float fe = env->get_fog_light_energy();
                    in.fogDensity = env->get_fog_density();
                    in.fog[0] = assets::light::SrgbToLinear(f.r) * fe;
                    in.fog[1] = assets::light::SrgbToLinear(f.g) * fe;
                    in.fog[2] = assets::light::SrgbToLinear(f.b) * fe;
                    std::printf("scene profile: fog baked, density %.4f\n", static_cast<double>(in.fogDensity));
                }
                if (env->get_ambient_light_source() == 2) {
                    const godot::Color c = env->get_ambient_light_color();
                    const float e = env->get_ambient_light_energy();
                    in.ambient[0] = assets::light::SrgbToLinear(c.r) * e;
                    in.ambient[1] = assets::light::SrgbToLinear(c.g) * e;
                    in.ambient[2] = assets::light::SrgbToLinear(c.b) * e;
                }
                break;
            }
            std::printf("scene profile: eye (%.2f %.2f %.2f), grade b %.2f c %.2f s %.2f, ambient (%.3f %.3f %.3f)\n",
                        static_cast<double>(in.eye[0]), static_cast<double>(in.eye[1]),
                        static_cast<double>(in.eye[2]), static_cast<double>(in.grade.brightness),
                        static_cast<double>(in.grade.contrast), static_cast<double>(in.grade.saturation),
                        static_cast<double>(in.ambient[0]), static_cast<double>(in.ambient[1]),
                        static_cast<double>(in.ambient[2]));
        }
        // The car is seen only in the ending, which sets brightness 5.68,
        // contrast 1.3, saturation .26 (EndingManager.gd:120-122): baked at
        // main's rest it posterizes to black and no runtime rescale brings
        // that back. Its props bake at the ending's grade on pages of their
        // own (main's other grade fields: its defaults).
        if (isMain) {
            assets::light::Grade ending = in.grade;
            ending.brightness = 5.68f;
            ending.contrast = 1.3f;
            ending.saturation = 0.26f;
            in.propGrades = {ending};
            // Seen from where "camera loop car" (EndingManager.gd:127) starts:
            // its texel tier and view vector, not the table's.
            std::array<float, 3> eye = {-162.0f, 52.0f, -89.6f};   // measured at runtime, if the play fails
            const char* eyeFrom = "the fallback (no \"camera loop car\")";
            for (godot::Node* n : runtime.nodes_in_tree_order()) {
                auto* player = godot::cast_to<godot::AnimationPlayer>(n);
                if (!player || !player->get_animation("camera loop car")) continue;
                player->play("camera loop car");
                player->seek(0.0, true);
                if (const auto* cam = runtime.get_node_as<godot::Node3D>("Camera")) {
                    const core::Mat4 w = cam->get_global_transform();
                    eye = {w.Tx(), w.Ty(), w.Tz()};
                    eyeFrom = "\"camera loop car\" at 0 s";
                }
                break;
            }
            in.propGradeEyes = {eye};
            std::printf("scene profile: the car and the highway bake at the ending's grade b %.2f c %.2f, "
                        "seen from (%.2f %.2f %.2f), %s\n",
                        static_cast<double>(ending.brightness), static_cast<double>(ending.contrast),
                        static_cast<double>(eye[0]), static_cast<double>(eye[1]), static_cast<double>(eye[2]), eyeFrom);
        }
        in.albedo = &nativePool;
        for (const assets::RoomBatch& b : native.Batches()) in.batchTexture.push_back(b.texture);
        // The normal maps, read as the GPU samples them: red and green are x and
        // y (export_runtime_scene.gd writes what the swizzle gives). A key that
        // does not load is fatal, as a variant's albedo is: a missing map would
        // quietly relight the surface.
        assets::TexturePool normalPool;
        in.normal = &normalPool;
        bool normalMissing = false;
        auto normalFor = [&](const godot::SurfaceMaterial* m) {
            if (!m || m->normal_texture.empty()) return -1;
            const int idx = tools::AddInlineTexture(gExtracted, m->normal_texture, normalPool, 8192);
            if (idx < 0) {
                std::printf("FATAL: normal map %s did not load\n", m->normal_texture.c_str());
                normalMissing = true;
                return idx;
            }
            // Its mip 1, when export_runtime_scene.gd wrote one (RA_AS_RG maps).
            if (static_cast<std::size_t>(idx) >= in.normalMip1.size()) {
                in.normalMip1.resize(static_cast<std::size_t>(idx) + 1, -1);
                std::string key = m->normal_texture;
                key.insert(key.size() - 4, "_mip1");
                in.normalMip1[static_cast<std::size_t>(idx)] =
                    tools::AddInlineTexture(gExtracted, key, normalPool, 8192);
            }
            return idx;
        };
        int missing = 0;
        for (const assets::SurfaceSource& src : room.Sources()) {
            const auto* g = runtime.get_node_as<godot::GeometryInstance3D>(src.nodePath);
            const godot::SurfaceMaterial* m = g ? ActiveMaterial(*g, src.surface) : nullptr;
            if (!m) ++missing;
            in.sourceMaterial.push_back(m ? ToSurfaceMaterial(*m) : assets::SurfaceMaterial{});
            in.sourceNormal.push_back(normalFor(m));
            in.sourceLayers.push_back(g ? g->get_layers() : 1u);
            in.sourceCastsShadow.push_back(CastsIntoBake(g, false) ? 1 : 0);
        }
        std::map<SwapKey, std::vector<godot::SurfaceMaterial>> swaps;
        CollectMaterialSwaps(runtime.get_root(), swaps);
        CollectTextureSwaps(runtime.get_root(), swaps);
        for (const std::unique_ptr<godot::Node>& inst : keepInstances) CollectMaterialSwaps(inst.get(), swaps);
        int variantProps = 0;
        int tintedVariants = 0;
        int sceneTextured = 0;
        std::set<SwapKey> usedSwaps;
        for (std::size_t pi = 0; pi < room.Props().size(); ++pi) {
            const assets::RoomProp& pr = room.Props()[pi];
            in.propTexture.push_back(native.Props()[pi].texture);
            const godot::GeometryInstance3D* g = nullptr;
            if (auto t = templateMesh.find(pi); t != templateMesh.end()) g = t->second;
            else g = runtime.get_node_as<godot::GeometryInstance3D>(pr.nodePath);
            const godot::SurfaceMaterial* m = g ? ActiveMaterial(*g, pr.surface) : nullptr;
            if (!m) ++missing;
            in.propMaterial.push_back(m ? ToSurfaceMaterial(*m) : assets::SurfaceMaterial{});
            // A texture the scene put on this surface replaces the model's.
            if (m && assets::IsSceneAuthoredTexture(m->albedo_texture)) {
                const int scene = tools::AddInlineTexture(gExtracted, m->albedo_texture, nativePool, 8192);
                if (scene < 0) {
                    std::printf("FATAL: scene texture %s did not load\n", m->albedo_texture.c_str());
                    return 1;
                }
                if (scene != in.propTexture.back()) ++sceneTextured;
                in.propTexture.back() = scene;
            }
            in.propNormal.push_back(normalFor(m));
            in.propAlpha.push_back(m && m->transparency != 0 ? 1 : 0);
            // Layers 0 at load means a script shows it later on layer 1 (the
            // dealer's crushed head, SwapDealerMesh): light it as it will be
            // drawn, since lit with layers 0 it bakes black. It casts nothing
            // at rest, and neither does a prop an animation moves: its shadow
            // would stay on the table after it is picked up.
            const uint32_t loadLayers = g ? g->get_layers() : 1u;
            in.propLayers.push_back(loadLayers != 0 ? loadLayers : 1u);
            const bool moves = !pr.templateKey.empty() || animated.Moves(pr.nodePath);
            in.propCastsShadow.push_back(CastsIntoBake(g, moves) ? 1 : 0);
            in.propDetail.push_back(static_cast<uint8_t>(DetailLevel(pr.nodePath)));
            in.propGrade.push_back(!in.propGrades.empty() && InEndingScope(pr.nodePath) ? 0 : -1);
            in.propVariants.emplace_back();
            const auto swap = g ? swaps.find({g, pr.surface}) : swaps.end();
            if (swap == swaps.end()) continue;
            usedSwaps.insert(swap->first);
            for (const godot::SurfaceMaterial& sm : swap->second) {
                tools::PropVariantInput v;
                v.material = ToSurfaceMaterial(sm);
                v.normalTexture = normalFor(&sm);
                v.texture = sm.albedo_texture.empty()
                                ? -1
                                : tools::AddInlineTexture(gExtracted, sm.albedo_texture, nativePool, 8192);
                if (!sm.albedo_texture.empty() && v.texture < 0) {
                    std::printf("FATAL: material variant texture %s did not load\n", sm.albedo_texture.c_str());
                    return 1;
                }
                v.albedo[0] = sm.albedo.r;
                v.albedo[1] = sm.albedo.g;
                v.albedo[2] = sm.albedo.b;
                v.albedo[3] = sm.albedo.a;
                v.emission[0] = sm.emission.r;
                v.emission[1] = sm.emission.g;
                v.emission[2] = sm.emission.b;
                v.textureKey = assets::TextureKeyHash(sm.albedo_texture);
                // The default live and blank colours keep their red and blue
                // through the grade (the colour-blind pair is white and grey).
                for (const godot::Color& c :
                     {scripts::GlobalVariables::default_color_live, scripts::GlobalVariables::default_color_blank}) {
                    if (sm.albedo.r == c.r && sm.albedo.g == c.g && sm.albedo.b == c.b) {
                        v.tinted = true;
                        v.tint[0] = c.r;
                        v.tint[1] = c.g;
                        v.tint[2] = c.b;
                        ++tintedVariants;
                    }
                }
                in.propVariants.back().push_back(v);
            }
            ++variantProps;
        }
        if (normalMissing) return 1;
        std::printf("surfaces: %d prop surfaces carry material variants\n", variantProps);
        std::printf("surfaces: %d variants tinted with a shell colour\n", tintedVariants);
        std::printf("surfaces: %d props take the texture the scene gave them\n", sceneTextured);
        int detailProps = 0;
        for (uint8_t d : in.propDetail) detailProps += d;
        std::printf("surfaces: %d detail props (screens at their texture's density)\n", detailProps);
        for (const auto& [key, mats] : swaps) {
            // A swap target merged into a static batch cannot change on screen.
            if (!usedSwaps.count(key)) {
                std::printf("WARNING: %s surface %d swaps materials but is not a prop\n",
                            key.first->get_path().c_str(), key.second);
            }
        }
        int mip1s = 0;
        for (int v : in.normalMip1) mip1s += v >= 0 ? 1 : 0;
        std::printf("surfaces: %lu normal map images, %d with a mip 1\n",
                    static_cast<unsigned long>(normalPool.Textures().size()), mip1s);
        std::printf("surfaces: %lu lights lit at rest, %d surfaces without a runtime material\n",
                    static_cast<unsigned long>(in.lights.size()), missing);
        if (!probeLight.empty()) {
            const auto* g = runtime.get_node_as<godot::GeometryInstance3D>(probeLight);
            const uint32_t layers = g ? g->get_layers() : 0xFFFFFFFFu;
            std::printf("probe-light: layers 0x%08lX\n", static_cast<unsigned long>(layers));
            return ProbeLight(room, in.lights, probeLight, layers, probeEye) ? 0 : 1;
        }
        in.planOnly = surfacesPlan;
        tools::SurfaceBakeResult result;
        const bool baked = tools::BakeSurfaces(room, in, result);
        if (surfacesPlan) return baked ? 0 : 1;
        std::printf("surfaces: %lu pages, %.2f MB encoded; "
                    "%lu batches, %lu props (%d split), %d unplaced faces\n",
                    static_cast<unsigned long>(result.pages.size()),
                    static_cast<double>(result.encodedBytes) / 1048576.0,
                    static_cast<unsigned long>(result.batches.size()),
                    static_cast<unsigned long>(result.props.size()), result.propSplits,
                    result.unplacedFaces);
        for (std::size_t pg = 0; pg < result.pages.size() && !pagesDir.empty(); ++pg) {
            char name[512];
            std::snprintf(name, sizeof name, "%s/%spage_%02lu%s.ppm", pagesDir.c_str(),
                          isMain ? "" : (sceneName + "_").c_str(),
                          static_cast<unsigned long>(pg), result.pages[pg].alpha ? "_alpha" : "");
            if (!tools::WritePagePpm(result.pages[pg], name)) {
                std::printf("WARNING: could not write %s (does %s exist?)\n", name, pagesDir.c_str());
            }
        }
        if (!baked) {
            std::printf("FATAL: surface bake: %s\n", result.error.c_str());
            return 1;
        }

        // The room the device loads (format v8): the pages are the texture
        // pool, and every batch and prop samples its page through the vertex
        // UV slot. The 128 px albedo above is dropped: every surface's colour
        // is baked.
        assets::TexturePool pages;
        for (std::size_t pg = 0; pg < result.pages.size(); ++pg) {
            const tools::SurfacePage& p = result.pages[pg];
            const int index = pages.AddEncoded(
                "page", static_cast<int>(pg), p.encoded.data(), p.encoded.size(), p.size, p.size,
                p.alpha ? assets::TextureFormat::Dxt5 : assets::TextureFormat::Dxt1);
            if (index != static_cast<int>(pg)) {
                std::printf("FATAL: page %lu did not enter the pool\n", static_cast<unsigned long>(pg));
                return 1;
            }
        }
        for (assets::RoomBatch& rb : result.batches) {
            rb.texture = rb.page;
            rb.mesh.uvs = rb.mesh.uv2;
            rb.triSource.clear();
        }
        // Every prop remembers where its texels were lit, and the static light
        // is probed over the part of T0 the props live in (their pivots, 1.5
        // units around, 1 u apart; 0.5 u came to 6.58 MB over T0's props), so
        // a moved prop is scaled by the ratio. Layer 1: every table prop's
        // layer; the backroom lamps' cull masks leave it out, as they leave
        // the props' own texels.
        constexpr float kProbeStep = 1.0f;
        constexpr float kProbeMargin = 1.5f;
        // How far toward its rest an offstage prop's probes reach: "dealer
        // return to table" starts him at local x 25.96 on his 20.69 -> 60.26
        // throw, 13.3% of the way out.
        constexpr float kOffstageWalkBack = 0.2f;
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (assets::RoomProp& rp : result.props) {
            rp.texture = rp.page;
            rp.mesh.uvs = rp.mesh.uv2;
            for (assets::PropVariant& v : rp.variants) v.texture = v.page;
            // An offstage prop (assets/offstage.h: the dealer, thrown out of the
            // room when shot) is seen lit only at the table, its reveal pose:
            // its pivot is there, and the probes cover it and the start of its
            // walk back, so beyond them the renderer draws it dark.
            if (isMain && rp.hasAltWorld && rp.meanPoses.empty() && assets::InOffstageScope(rp.nodePath)) {
                rp.hasBakePivot = true;
                for (int a = 0; a < 3; ++a) rp.bakePivot[a] = rp.bakeWorld.m[12 + a];
                float back[3];
                for (int a = 0; a < 3; ++a)
                    back[a] = rp.bakePivot[a] + kOffstageWalkBack * (rp.altWorld.m[12 + a] - rp.bakePivot[a]);
                for (int a = 0; a < 3; ++a) {
                    lo[a] = std::min({lo[a], rp.bakePivot[a] - kProbeMargin, back[a] - kProbeMargin});
                    hi[a] = std::max({hi[a], rp.bakePivot[a] + kProbeMargin, back[a] + kProbeMargin});
                }
                std::printf("probes: offstage %s, table (%.1f %.1f %.1f), walk back to (%.1f %.1f %.1f)\n",
                            rp.nodePath.c_str(), static_cast<double>(rp.bakePivot[0]),
                            static_cast<double>(rp.bakePivot[1]), static_cast<double>(rp.bakePivot[2]),
                            static_cast<double>(back[0]), static_cast<double>(back[1]), static_cast<double>(back[2]));
                continue;
            }
            // A two-pose prop's texels already hold the brighter of its rest and
            // reveal light, so no single pivot describes them: never rescaled.
            // Rescaled against the reveal pivot, the compartment lids read
            // yellow and white at rest, the ratio pinned at its clamp.
            if (rp.hasAltWorld || !rp.meanPoses.empty() || !rp.extraPoses.empty()) continue;   // texels hold no one pose
            // A `sole` reveal's props keep their held light: lit dark near the
            // camera, a pivot there pins the ratio at its clamp anywhere else.
            if (rp.hasCastWorld) continue;
            rp.hasBakePivot = true;
            for (int a = 0; a < 3; ++a) rp.bakePivot[a] = rp.bakeWorld.m[12 + a];
            float d2 = 0.0f;
            for (int a = 0; a < 3; ++a) d2 += (rp.bakePivot[a] - assets::kHomeSocket[a]) * (rp.bakePivot[a] - assets::kHomeSocket[a]);
            if (assets::Tier(std::sqrt(d2)) != 0) continue;
            for (int a = 0; a < 3; ++a) {
                lo[a] = std::min(lo[a], rp.bakePivot[a] - kProbeMargin);
                hi[a] = std::max(hi[a], rp.bakePivot[a] + kProbeMargin);
            }
        }
        if (isMain && lo[0] <= hi[0]) {
            const assets::ProbeGrid grid = tools::BakeProbes(room, in, lo, hi, kProbeStep, 1u);
            std::printf("probes: %d x %d x %d points from (%.1f %.1f %.1f), %.2f MB\n", grid.dims[0], grid.dims[1],
                        grid.dims[2], static_cast<double>(lo[0]), static_cast<double>(lo[1]),
                        static_cast<double>(lo[2]),
                        static_cast<double>(grid.rgb.size() * sizeof(float)) / 1048576.0);
            room.SetProbes(grid);
        }
        // The animated lights, one per vertex channel, each where it burns: the
        // muzzle flash fires 2.5 s into ShotgunShooting's "dealer" branch
        // (gd:103-113), after "player shoot dealer" (2.205 s) has left the gun
        // at its end pose; the zippo burns 1.63-2.52 s into "player use
        // cigarettes"; the two masks hang from `light parent` and never move.
        // The restroom's pill flicker is not one of them. Main's profile only.
        if (isMain) {
            struct DynSpec { const char* light; const char* player; const char* anim; float at; };
            // In assets::kDynLightPaths' channel order.
            static const DynSpec kDyn[4] = {
                {assets::kDynLightPaths[0].data(),
                 "tabletop parent/shotgun main parent/anim_shotgun main parent", "player shoot dealer", -1.0f},
                {assets::kDynLightPaths[1].data(), nullptr, nullptr, 0.0f},
                {assets::kDynLightPaths[2].data(), nullptr, nullptr, 0.0f},
                {assets::kDynLightPaths[3].data(),
                 "player item interaction parent/animator_player items", "player use cigarettes", 2.0f},
            };
            std::vector<assets::BakedLight> dyn;
            for (const DynSpec& d : kDyn) {
                if (d.player) {
                    auto* player = runtime.get_node_as<godot::AnimationPlayer>(d.player);
                    const assets::Animation* anim = player ? player->get_animation(d.anim) : nullptr;
                    if (!anim) {
                        std::printf("FATAL: dyn light pose %s \"%s\" not found\n", d.player, d.anim);
                        return 1;
                    }
                    player->play(d.anim);
                    player->seek(d.at < 0.0f ? anim->length : d.at, true);
                }
                const auto* l = runtime.get_node_as<godot::OmniLight3D>(d.light);
                if (!l) {
                    std::printf("FATAL: dyn light %s not found\n", d.light);
                    return 1;
                }
                assets::BakedLight b;
                const core::Mat4 w = l->get_global_transform();
                b.position[0] = w.Tx();
                b.position[1] = w.Ty();
                b.position[2] = w.Tz();
                b.range = l->get_omni_range();
                b.attenuation = l->get_omni_attenuation();
                b.shadow = l->has_shadow();
                b.size = l->get_light_size();
                b.cullMask = l->get_cull_mask();
                dyn.push_back(b);
                std::printf("dyn light %lu: %s at (%.2f %.2f %.2f)\n", static_cast<unsigned long>(dyn.size() - 1),
                            d.light, static_cast<double>(b.position[0]), static_cast<double>(b.position[1]),
                            static_cast<double>(b.position[2]));
            }
            std::vector<float> casters;
            for (const assets::RoomBatch& b : room.Batches()) {
                for (std::size_t t = 0; t < b.mesh.indices.size() / 3; ++t) {
                    if (t < b.triSource.size() &&
                        !in.sourceCastsShadow[static_cast<std::size_t>(b.triSource[t])]) {
                        continue;
                    }
                    for (int k = 0; k < 3; ++k) {
                        const uint16_t i = b.mesh.indices[t * 3 + k];
                        casters.insert(casters.end(), &b.mesh.positions[i * 3], &b.mesh.positions[i * 3] + 3);
                    }
                }
            }
            assets::LightBvh dynBvh;
            dynBvh.Build(casters);
            // Layers: a merged batch takes its first source's (the room is
            // layer 1 throughout); a prop takes its node's.
            for (assets::RoomBatch& rb : result.batches) {
                tools::BakeDynLayers(dyn, dynBvh, core::Mat4{}, 1u, rb.mesh);
            }
            for (assets::RoomProp& rp : result.props) {
                const auto* g = rp.IsTemplate() ? nullptr : runtime.get_node_as<godot::GeometryInstance3D>(rp.nodePath);
                tools::BakeDynLayers(dyn, dynBvh, rp.bakeWorld, g ? g->get_layers() : 1u, rp.mesh);
            }
        }
        // Fog scenes (assets/scene_fog.h): geometry fine enough for the
        // runtime FOG program's per-vertex fog, and every vertex's surface
        // luminance in a_col.a. Main has no fog; its a_col holds the animated
        // lights. Merged geometry whose fog is baked keeps its triangles.
        if (fogScene) {
            const float maxEdge = assets::FogMaxEdge(sceneFogDensity);
            int tessellated = 0, added = 0, capped = 0;
            const auto tessellate = [&](assets::Mesh& mesh, std::vector<tools::VertexSurface>& surfaces,
                                        const core::Mat4& world, const std::string& name) {
                const tools::FogTessellation r = tools::TessellateForFog(mesh, &surfaces, world, maxEdge);
                if (r.added > 0) {
                    ++tessellated;
                    added += r.added;
                }
                if (r.capped) {   // per-vertex fog will smear across its long edges
                    ++capped;
                    std::printf("fog: WARNING %s stopped at %d vertices, longest edge %.1f units\n", name.c_str(),
                                mesh.VertexCount(), static_cast<double>(r.longestEdge));
                }
            };
            if (!assets::FogIsBaked(sceneName)) {
                for (std::size_t b = 0; b < result.batches.size(); ++b) {
                    tessellate(result.batches[b].mesh, result.batchSurfaces[b], core::Mat4{},
                               "batch " + std::to_string(b));
                }
            }
            for (std::size_t p = 0; p < result.props.size(); ++p) {
                tessellate(result.props[p].mesh, result.propSurfaces[p], result.props[p].bakeWorld,
                           result.props[p].nodePath);
            }
            std::printf("fog: %d meshes tessellated to edges <= %.1f units (+%d triangles), %d capped\n",
                        tessellated, static_cast<double>(maxEdge), added, capped);
            for (std::size_t b = 0; b < result.batches.size(); ++b) {
                tools::BakeFogLuminance(in.lights, core::Mat4{}, 1u, in.ambient, &result.batchSurfaces[b],
                                        result.batches[b].mesh);
            }
            for (std::size_t p = 0; p < result.props.size(); ++p) {
                assets::RoomProp& rp = result.props[p];
                const auto* g = rp.IsTemplate() ? nullptr : runtime.get_node_as<godot::GeometryInstance3D>(rp.nodePath);
                const uint32_t layers = g && g->get_layers() != 0 ? g->get_layers() : 1u;
                // A prop lit with the mean of its poses is fogged under it too.
                if (rp.meanPoses.empty()) {
                    tools::BakeFogLuminance(in.lights, rp.bakeWorld, layers, in.ambient, &result.propSurfaces[p],
                                            rp.mesh);
                } else {
                    tools::BakeFogLuminance(in.lights, rp.meanPoses, layers, in.ambient, &result.propSurfaces[p],
                                            rp.mesh);
                }
            }
            std::printf("fog: surface luminance baked into %lu batches and %lu props\n",
                        static_cast<unsigned long>(result.batches.size()),
                        static_cast<unsigned long>(result.props.size()));
        }
        room.AdoptBatches(std::move(result.batches));
        room.AdoptProps(std::move(result.props));
        room.Textures() = std::move(pages);
    }

    if (!assets::WriteRoomFile(out.c_str(), room)) {
        std::printf("FATAL: could not write %s (does the directory exist?)\n",
                    out.c_str());
        return 1;
    }
    std::printf("wrote %s\n", out.c_str());

    // Read it straight back. A bake that cannot be loaded is worse than no
    // bake, and finding that out on the device costs a hardware round trip.
    assets::Room check;
    const assets::RoomFileError e = assets::ReadRoomFile(out.c_str(), check);
    if (e != assets::RoomFileError::None) {
        std::printf("FATAL: wrote a file that will not load: %s\n",
                    assets::RoomFileErrorText(e));
        return 1;
    }
    if (check.Stats().triangles != s.triangles) {
        std::printf("FATAL: round trip changed the triangle count (%d -> %d)\n",
                    s.triangles, check.Stats().triangles);
        return 1;
    }
    if (static_cast<int>(check.Props().size()) != s.props) {
        std::printf("FATAL: round trip lost props (%d -> %lu)\n",
                    s.props, static_cast<unsigned long>(check.Props().size()));
        return 1;
    }
    // Textures are checked too: a pool that wrote correctly and read back
    // empty would pass a geometry-only round trip and show up as a white room
    // on the device.
    if (check.Textures().Size() != room.Textures().Size()) {
        std::printf("FATAL: round trip changed the texture count (%lu -> %lu)\n",
                    static_cast<unsigned long>(room.Textures().Size()),
                    static_cast<unsigned long>(check.Textures().Size()));
        return 1;
    }
    if (check.Textures().ByteSize() != room.Textures().ByteSize()) {
        std::printf("FATAL: round trip changed the pixel pool (%lu -> %lu bytes)\n",
                    static_cast<unsigned long>(room.Textures().ByteSize()),
                    static_cast<unsigned long>(check.Textures().ByteSize()));
        return 1;
    }
    {
        int rebound = 0;
        for (const assets::RoomBatch& b : check.Batches()) {
            if (b.texture >= 0) ++rebound;
        }
        std::printf("verified: %d/%lu batches still find their texture\n",
                    rebound, static_cast<unsigned long>(check.Batches().size()));
    }
    std::printf("verified: reloads to %d batches / %lu props / %d triangles\n",
                check.Stats().batches,
                static_cast<unsigned long>(check.Props().size()),
                check.Stats().triangles);
    return 0;
}
