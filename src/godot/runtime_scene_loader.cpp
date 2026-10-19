#include "godot/runtime_scene_loader.h"

#include "godot/particles.h"

#include "godot/material.h"
#include "godot/physics.h"
#include "godot/skeleton.h"

#include <cstdio>
#include <cstring>

#include "core/json_lite.h"
#include "godot/animation_player.h"
#include "godot/control.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace godot {
namespace {

using namespace core::json;

// The most derived C++ type for an exported node. Classes the runtime does not
// model (Skeleton3D, ...) become the nearest base their exported fields prove:
// transparency -> GeometryInstance3D, layers -> VisualInstance3D, a transform ->
// Node3D, otherwise Node. get_class() still reports the real name.
std::unique_ptr<Node> MakeNode(const std::string& cls, std::string name, bool hasXform,
                               bool hasLayers, bool hasTransparency) {
    if (cls == "Camera3D")        return std::make_unique<Camera3D>(std::move(name));
    if (cls == "MeshInstance3D")  return std::make_unique<MeshInstance3D>(std::move(name));
    if (cls == "Label3D")         return std::make_unique<Label3D>(std::move(name));
    if (cls == "OmniLight3D")     return std::make_unique<OmniLight3D>(std::move(name));
    if (cls == "Skeleton3D")      return std::make_unique<Skeleton3D>(std::move(name));
    if (cls == "WorldEnvironment") return std::make_unique<WorldEnvironment>(std::move(name));
    if (cls == "GPUParticles3D")  return std::make_unique<GPUParticles3D>(std::move(name));
    if (cls == "AnimationPlayer") return std::make_unique<AnimationPlayer>(std::move(name));
    // AudioStreamPlayer3D is a Node3D in Godot; the other two are not spatial
    // here (the port does not model Node2D).
    if (cls == "AudioStreamPlayer3D") return std::make_unique<AudioStreamPlayer3D>(std::move(name));
    if (cls == "AudioStreamPlayer" || cls == "AudioStreamPlayer2D") {
        auto player = std::make_unique<AudioStreamPlayer>(std::move(name));
        if (cls == "AudioStreamPlayer2D") player->set_space(AudioSpace::Flat2D);
        return player;
    }
    if (cls == "StaticBody3D")    return std::make_unique<StaticBody3D>(std::move(name));
    if (cls == "CollisionShape3D") return std::make_unique<CollisionShape3D>(std::move(name));
    if (cls == "Label")           return std::make_unique<Label>(std::move(name));
    if (cls == "ColorRect")       return std::make_unique<ColorRect>(std::move(name));
    if (cls == "TextureRect")     return std::make_unique<TextureRect>(std::move(name));
    if (cls == "Button")          return std::make_unique<Button>(std::move(name));
    if (cls == "Control")         return std::make_unique<Control>(std::move(name));
    if (hasTransparency)          return std::make_unique<GeometryInstance3D>(std::move(name));
    if (hasLayers)                return std::make_unique<VisualInstance3D>(std::move(name));
    if (hasXform)                 return std::make_unique<Node3D>(std::move(name));
    return std::make_unique<Node>(std::move(name));
}

bool ReadFloats(const char* b, const char* e, const char* key, std::size_t n, float* out) {
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (!FindArrayTopLevel(b, e, key, &ab, &ae)) return false;
    std::vector<double> v;
    ReadNumberArray(ab, ae, v);
    if (v.size() != n) return false;
    for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<float>(v[i]);
    return true;
}

std::string NameFromPath(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// {"particles":{"emitting","one_shot","lifetime","speed_scale","fixed_fps",
// "preprocess","aabb":[px,py,pz,sx,sy,sz]}}: what decides when the renderer
// draws the global RNG (godot/particles.h).
void ReadParticles(const char* b, const char* e, GPUParticles3D& p) {
    const char* ob = nullptr;
    const char* oe = nullptr;
    if (!FindKeyTopLevel(b, e, "particles") || !FindObject(b, e, "particles", &ob, &oe)) return;
    p.set_emitting(ReadBoolTopLevel(ob, oe, "emitting", true));
    p.set_one_shot(ReadBoolTopLevel(ob, oe, "one_shot", false));
    p.set_lifetime(ReadNumberTopLevel(ob, oe, "lifetime", 1.0));
    p.set_speed_scale(ReadNumberTopLevel(ob, oe, "speed_scale", 1.0));
    p.set_fixed_fps(ReadIntTopLevel(ob, oe, "fixed_fps", 30));
    p.set_pre_process_time(ReadNumberTopLevel(ob, oe, "preprocess", 0.0));
    float box[6] = {-4.0f, -4.0f, -4.0f, 8.0f, 8.0f, 8.0f};
    ReadFloats(ob, oe, "aabb", 6, box);
    p.set_visibility_aabb({box[0], box[1], box[2]}, {box[3], box[4], box[5]});
    // The look the CPU billboards draw.
    const char* lb = nullptr;
    const char* le = nullptr;
    if (!FindKeyTopLevel(ob, oe, "look") || !FindObject(ob, oe, "look", &lb, &le)) return;
    GPUParticles3D::Look look;
    look.amount = ReadIntTopLevel(lb, le, "amount", 0);
    look.explosiveness = static_cast<float>(ReadNumberTopLevel(lb, le, "explosiveness", 0.0));
    look.randomness = static_cast<float>(ReadNumberTopLevel(lb, le, "randomness", 0.0));
    look.local_coords = ReadBoolTopLevel(lb, le, "local_coords", false);
    const char* pb = nullptr;
    const char* pe = nullptr;
    if (FindKeyTopLevel(lb, le, "process") && FindObject(lb, le, "process", &pb, &pe)) {
        ReadFloats(pb, pe, "direction", 3, look.direction);
        look.spread = static_cast<float>(ReadNumberTopLevel(pb, pe, "spread", 45.0));
        ReadFloats(pb, pe, "velocity", 2, look.velocity);
        ReadFloats(pb, pe, "gravity", 3, look.gravity);
        ReadFloats(pb, pe, "scale", 2, look.scale);
        ReadFloats(pb, pe, "scale_curve", 9, look.scale_curve);
    }
    const char* db = nullptr;
    const char* de = nullptr;
    if (FindKeyTopLevel(lb, le, "draw") && FindObject(lb, le, "draw", &db, &de)) {
        ReadFloats(db, de, "size", 2, look.size);
        ReadFloats(db, de, "albedo", 4, look.albedo);
        look.texture = ReadStringTopLevel(db, de, "texture");
        look.unshaded = ReadBoolTopLevel(db, de, "unshaded", false);
    }
    p.set_look(look);
}

// {"albedo":[r,g,b,a],"transparency":n}; the same colour is the load-time one.
SurfaceMaterial ReadMaterial(const char* b, const char* e) {
    SurfaceMaterial m;
    float c[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    ReadFloats(b, e, "albedo", 4, c);
    m.albedo = {c[0], c[1], c[2], c[3]};
    m.albedo_at_load = m.albedo;
    m.transparency = ReadIntTopLevel(b, e, "transparency", 0);
    m.albedo_texture = ReadStringTopLevelDecoded(b, e, "albedo_texture");
    // The premultiplied emission: a scene-authored emissive surface (the health
    // counter's bolts) carries its own, as the materials scripts hold do
    // (bind_support).
    float em[3] = {0.0f, 0.0f, 0.0f};
    ReadFloats(b, e, "emission", 3, em);
    m.emission = {em[0], em[1], em[2], 1.0f};
    m.roughness = static_cast<float>(ReadNumberTopLevel(b, e, "roughness", 1.0));
    m.metallic = static_cast<float>(ReadNumberTopLevel(b, e, "metallic", 0.0));
    float ec[3] = {0.0f, 0.0f, 0.0f};
    ReadFloats(b, e, "emission_color", 3, ec);
    m.emission_color = {ec[0], ec[1], ec[2], 1.0f};
    m.emission_energy = static_cast<float>(ReadNumberTopLevel(b, e, "emission_energy", 0.0));
    m.shading_mode = ReadIntTopLevel(b, e, "shading_mode", 1);
    m.blend_mode = ReadIntTopLevel(b, e, "blend_mode", 0);
    m.diffuse_mode = ReadIntTopLevel(b, e, "diffuse_mode", 0);
    m.normal_texture = ReadStringTopLevelDecoded(b, e, "normal_texture");
    m.normal_scale = static_cast<float>(ReadNumberTopLevel(b, e, "normal_scale", 1.0));
    return m;
}

void ReadGeometry(const char* b, const char* e, GeometryInstance3D& gi) {
    gi.set_transparency(static_cast<float>(ReadNumberTopLevel(b, e, "transparency", 0.0)));
    gi.set_cast_shadow(ReadIntTopLevel(b, e, "cast_shadow", 1));
    const char* mb = nullptr;
    const char* me = nullptr;
    if (FindArrayTopLevel(b, e, "materials", &mb, &me)) {
        std::vector<Span> mats;
        SplitObjects(mb, me, mats);
        for (const Span& m : mats) gi.add_surface_material(ReadMaterial(m.first, m.second));
    }
    const char* ob = nullptr;
    const char* oe = nullptr;
    if (FindKeyTopLevel(b, e, "material_override") &&
        FindObject(b, e, "material_override", &ob, &oe)) {
        gi.set_material_override(ReadMaterial(ob, oe));
        // A .tres override is a shared resource: a script holding the same
        // path (SegmentManager.mat) must reach the very object drawn here.
        const std::string res = ReadStringTopLevelDecoded(b, e, "material_override_res");
        if (!res.empty()) gi.bind_material_override(MaterialLibrary::get(res, ReadMaterial(ob, oe)));
    }
}

void ReadPlayer(const char* b, const char* e, AnimationPlayer& p) {
    const char* ob = nullptr;
    const char* oe = nullptr;
    if (!FindKeyTopLevel(b, e, "player") || !FindObject(b, e, "player", &ob, &oe)) return;
    p.set_initial_state(ReadStringTopLevelDecoded(ob, oe, "assigned"),
                        ReadBoolTopLevel(ob, oe, "playing", false),
                        ReadNumberTopLevel(ob, oe, "position", 0.0),
                        static_cast<float>(ReadNumberTopLevel(ob, oe, "speed_scale", 1.0)),
                        ReadStringTopLevelDecoded(ob, oe, "autoplay"));
}

// AudioPlayback, not AudioStreamPlayer: AudioStreamPlayer3D is its own class
// (a Node3D), and its speakers need their streams too (the health counter's
// beep and its health-loss noise among them).
void ReadAudio(const char* b, const char* e, AudioPlayback& a) {
    const char* ob = nullptr;
    const char* oe = nullptr;
    if (!FindKeyTopLevel(b, e, "audio") || !FindObject(b, e, "audio", &ob, &oe)) return;
    a.set_stream(ReadStringTopLevelDecoded(ob, oe, "stream"));
    a.set_volume_db(static_cast<float>(ReadNumberTopLevel(ob, oe, "volume_db", 0.0)));
    a.set_pitch_scale(static_cast<float>(ReadNumberTopLevel(ob, oe, "pitch_scale", 1.0)));
    a.set_playing(ReadBoolTopLevel(ob, oe, "playing", false));
    a.set_max_polyphony(ReadIntTopLevel(ob, oe, "max_polyphony", 1));
    if (FindKeyTopLevel(ob, oe, "bus")) a.set_bus(ReadStringTopLevelDecoded(ob, oe, "bus"));
    // The spatial mix (godot/audio_spatial.h). Godot's defaults when absent.
    auto num = [&](const char* key, float fallback) {
        return static_cast<float>(ReadNumberTopLevel(ob, oe, key, fallback));
    };
    if (a.space() == AudioSpace::Flat2D) {
        Spatial2D s;
        float pos[2] = {0.0f, 0.0f};
        ReadFloats(ob, oe, "global_position", 2, pos);
        s.global_position = {pos[0], pos[1]};
        s.max_distance = num("max_distance", s.max_distance);
        s.attenuation = num("attenuation", s.attenuation);
        s.panning_strength = num("panning_strength", s.panning_strength);
        a.set_spatial_2d(s);
    } else if (a.space() == AudioSpace::Positional3D) {
        Spatial3D s;
        s.unit_size = num("unit_size", s.unit_size);
        s.max_db = num("max_db", s.max_db);
        s.max_distance = num("max_distance", s.max_distance);
        s.attenuation_model = ReadIntTopLevel(ob, oe, "attenuation_model", s.attenuation_model);
        s.panning_strength = num("panning_strength", s.panning_strength);
        s.attenuation_filter_db = num("attenuation_filter_db", s.attenuation_filter_db);
        s.attenuation_filter_cutoff_hz = num("attenuation_filter_cutoff_hz", s.attenuation_filter_cutoff_hz);
        a.set_spatial_3d(s);
    }
}

void ReadControl(const char* b, const char* e, Control& c) {
    c.set_visible(ReadBoolTopLevel(b, e, "visible", true));
    float scale[2] = {1.0f, 1.0f};
    ReadFloats(b, e, "scale", 2, scale);
    c.set_scale({scale[0], scale[1]});
    float mod[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    ReadFloats(b, e, "modulate", 4, mod);
    c.set_modulate({mod[0], mod[1], mod[2], mod[3]});
    float self_mod[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    ReadFloats(b, e, "self_modulate", 4, self_mod);
    c.set_self_modulate({self_mod[0], self_mod[1], self_mod[2], self_mod[3]});
    c.set_rotation(static_cast<float>(ReadNumberTopLevel(b, e, "control_rotation", 0.0)));
    if (auto* label = cast_to<Label>(&c)) {
        // Text first: set_visible_characters derives the ratio from it.
        label->set_text(ReadStringTopLevelDecoded(b, e, "text"));
        label->set_visible_characters(ReadIntTopLevel(b, e, "visible_characters", -1));
    }
    if (auto* button = cast_to<Button>(&c)) button->set_text(ReadStringTopLevelDecoded(b, e, "text"));

    // GUI state (export format 4). Absent keys keep Godot's defaults, so an
    // older export still loads; the focus and pointer paths then have nothing
    // to use.
    if (FindKeyTopLevel(b, e, "focus_mode")) c.set_focus_mode(ReadIntTopLevel(b, e, "focus_mode", c.get_focus_mode()));
    if (FindKeyTopLevel(b, e, "mouse_filter")) {
        c.set_mouse_filter(ReadIntTopLevel(b, e, "mouse_filter", c.get_mouse_filter()));
    }
    const char* nb = nullptr;
    const char* ne = nullptr;
    if (FindArrayTopLevel(b, e, "focus_neighbor", &nb, &ne)) {
        std::vector<std::string> sides;
        ReadStringArray(nb, ne, sides);
        for (int side = 0; side < 4 && side < static_cast<int>(sides.size()); ++side) {
            c.set_focus_neighbor(side, sides[static_cast<std::size_t>(side)]);
        }
    }
    float gx[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    if (ReadFloats(b, e, "global_xform", 6, gx)) {
        Transform2D t;
        t.xx = gx[0]; t.xy = gx[1]; t.yx = gx[2]; t.yy = gx[3]; t.ox = gx[4]; t.oy = gx[5];
        c.set_global_transform(t);
    }
    float size[2] = {0.0f, 0.0f};
    if (ReadFloats(b, e, "size", 2, size)) c.set_size({size[0], size[1]});
    float pos[2] = {0.0f, 0.0f};
    if (ReadFloats(b, e, "position", 2, pos)) c.set_position_at_load({pos[0], pos[1]});
}

// The script file and the raw exports object, for the generated bindings.
void ReadScript(const char* b, const char* e, Node& node) {
    node.set_script_file(ReadStringTopLevelDecoded(b, e, "script"));
    const char* ob = nullptr;
    const char* oe = nullptr;
    if (FindKeyTopLevel(b, e, "exports") && FindObject(b, e, "exports", &ob, &oe)) {
        node.set_exports_json(std::string(ob, oe));
    }
}

// PORT (physics.h): box and concave are the only shapes main.tscn uses; any
// other type fails the load loudly instead of leaving a hole in the space.
// Concave triangles live in the export's <name>_faces.bin; `offset` and
// `count` are counted in vertices.
bool ReadCollisionShape(const char* b, const char* e, CollisionShape3D& cs, const std::vector<float>* faces) {
    cs.disabled = ReadBoolTopLevel(b, e, "disabled", false);
    const char* sb = nullptr;
    const char* se = nullptr;
    if (!FindKeyTopLevel(b, e, "shape") || !FindObject(b, e, "shape", &sb, &se)) return true;
    const std::string type = ReadStringTopLevelDecoded(sb, se, "type");
    if (type == "box") {
        float size[3] = {0.0f, 0.0f, 0.0f};
        if (!ReadFloats(sb, se, "size", 3, size)) return false;
        auto box = std::make_shared<BoxShape3D>();
        box->half_extents = {size[0] * 0.5f, size[1] * 0.5f, size[2] * 0.5f};
        cs.shape = box;
        return true;
    }
    if (type == "cylinder") {
        // Scalars, not arrays, so ReadFloats would reject them. An absent value
        // takes CylinderShape3D's own default, as Godot does.
        auto cylinder = std::make_shared<CylinderShape3D>();
        cylinder->radius = static_cast<float>(ReadNumberTopLevel(sb, se, "radius", cylinder->radius));
        cylinder->height = static_cast<float>(ReadNumberTopLevel(sb, se, "height", cylinder->height));
        cs.shape = cylinder;
        return true;
    }
    if (type == "concave") {
        auto shape = std::make_shared<ConcavePolygonShape3D>();
        shape->backface_collision = ReadBoolTopLevel(sb, se, "backface_collision", false);
        const int offset = ReadIntTopLevel(sb, se, "offset", -1);
        const int count = ReadIntTopLevel(sb, se, "count", -1);
        if (!faces) {
            std::fprintf(stderr, "runtime scene: %s has a concave shape but no collision faces were loaded\n",
                         cs.get_name().c_str());
            cs.shape = shape;
            return true;
        }
        if (offset < 0 || count < 0 || static_cast<std::size_t>(offset + count) * 3u > faces->size()) return false;
        std::vector<Vector3> soup(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            const std::size_t k = static_cast<std::size_t>(offset + i) * 3u;
            soup[static_cast<std::size_t>(i)] = {(*faces)[k], (*faces)[k + 1], (*faces)[k + 2]};
        }
        shape->set_faces(soup);
        cs.shape = shape;
        return true;
    }
    std::fprintf(stderr, "runtime scene: unsupported collision shape '%s' on %s\n", type.c_str(),
                 cs.get_name().c_str());
    return false;
}

// Applies every exported field this node's C++ type owns.
bool ReadState(const char* b, const char* e, Node& node, const std::vector<float>* faces) {
    if (Node3D* n3 = cast_to<Node3D>(&node)) {
        core::Mat4 local;
        float rot[3] = {0.0f, 0.0f, 0.0f};
        float scl[3] = {1.0f, 1.0f, 1.0f};
        if (!ReadFloats(b, e, "xform", 16, local.m) || !ReadFloats(b, e, "rotation", 3, rot) ||
            !ReadFloats(b, e, "scale", 3, scl)) {
            return false;
        }
        n3->set_rest(local, {rot[0], rot[1], rot[2]}, {scl[0], scl[1], scl[2]});
        n3->set_visible(ReadBoolTopLevel(b, e, "visible", true));
    }
    if (auto* vi = cast_to<VisualInstance3D>(&node)) {
        vi->set_layers(static_cast<uint32_t>(ReadNumberTopLevel(b, e, "layers", 1.0)));
    }
    if (auto* gi = cast_to<GeometryInstance3D>(&node)) ReadGeometry(b, e, *gi);
    if (auto* cam = cast_to<Camera3D>(&node)) {
        cam->set_current(ReadBoolTopLevel(b, e, "current", false));
        cam->set_fov(static_cast<float>(ReadNumberTopLevel(b, e, "fov", 75.0)));
        cam->set_cull_mask(static_cast<uint32_t>(ReadNumberTopLevel(b, e, "cull_mask", 1048575.0)));
        cam->set_near(static_cast<float>(ReadNumberTopLevel(b, e, "near", 0.05)));
        cam->set_far(static_cast<float>(ReadNumberTopLevel(b, e, "far", 4000.0)));
    }
    // Bones and skins.
    if (auto* sk = cast_to<Skeleton3D>(&node)) {
        const char* ab = nullptr;
        const char* ae = nullptr;
        if (FindArrayTopLevel(b, e, "bones", &ab, &ae)) {
            std::vector<Span> bones;
            SplitObjects(ab, ae, bones);
            for (const Span& bone : bones) {
                core::Mat4 rest, pose;
                ReadFloats(bone.first, bone.second, "rest", 16, rest.m);
                ReadFloats(bone.first, bone.second, "pose", 16, pose.m);
                sk->add_bone(ReadStringTopLevel(bone.first, bone.second, "name"),
                             ReadIntTopLevel(bone.first, bone.second, "parent", -1), rest, pose);
            }
        }
    }
    if (auto* mi = cast_to<MeshInstance3D>(&node)) {
        const char* sb = nullptr;
        const char* se = nullptr;
        if (FindKeyTopLevel(b, e, "skin") && FindObject(b, e, "skin", &sb, &se)) {
            std::vector<MeshInstance3D::SkinBind> binds;
            const char* ab = nullptr;
            const char* ae = nullptr;
            if (FindArrayTopLevel(sb, se, "binds", &ab, &ae)) {
                std::vector<Span> spans;
                SplitObjects(ab, ae, spans);
                for (const Span& s : spans) {
                    MeshInstance3D::SkinBind bind{};
                    bind.name = ReadStringTopLevel(s.first, s.second, "name");
                    ReadFloats(s.first, s.second, "pose", 16, bind.pose);
                    binds.push_back(std::move(bind));
                }
            }
            mi->set_skin(ReadStringTopLevel(sb, se, "skeleton"), std::move(binds));
        }
    }
    if (auto* env = cast_to<WorldEnvironment>(&node)) {
        const char* eb = nullptr;
        const char* ee = nullptr;
        if (FindKeyTopLevel(b, e, "environment") && FindObject(b, e, "environment", &eb, &ee)) {
            env->set_adjustment_brightness(static_cast<float>(ReadNumberTopLevel(eb, ee, "adjustment_brightness", 1.0)));
            env->set_adjustment_contrast(static_cast<float>(ReadNumberTopLevel(eb, ee, "adjustment_contrast", 1.0)));
            env->set_adjustment_saturation(static_cast<float>(ReadNumberTopLevel(eb, ee, "adjustment_saturation", 1.0)));
            env->set_ambient_light_source(ReadIntTopLevel(eb, ee, "ambient_light_source", 0));
            env->set_ambient_light_energy(static_cast<float>(ReadNumberTopLevel(eb, ee, "ambient_light_energy", 1.0)));
            env->set_fog_enabled(ReadBoolTopLevel(eb, ee, "fog_enabled", false));
            env->set_fog_light_energy(static_cast<float>(ReadNumberTopLevel(eb, ee, "fog_light_energy", 1.0)));
            env->set_fog_density(static_cast<float>(ReadNumberTopLevel(eb, ee, "fog_density", 0.01)));
            env->set_fog_sky_affect(static_cast<float>(ReadNumberTopLevel(eb, ee, "fog_sky_affect", 1.0)));
            env->set_background_mode(ReadIntTopLevel(eb, ee, "background_mode", 0));
            env->set_background_energy_multiplier(
                static_cast<float>(ReadNumberTopLevel(eb, ee, "background_energy_multiplier", 1.0)));
            const auto readColour = [eb, ee](const char* key, Color* out) {
                const char* cb = nullptr;
                const char* ce = nullptr;
                if (!FindArrayTopLevel(eb, ee, key, &cb, &ce)) return;
                std::vector<double> c;
                ReadNumberArray(cb, ce, c);
                if (c.size() >= 3) {
                    *out = {static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2]), 1.0f};
                }
            };
            Color colour = env->get_ambient_light_color();
            readColour("ambient_light_color", &colour);
            env->set_ambient_light_color(colour);
            colour = env->get_fog_light_color();
            readColour("fog_light_color", &colour);
            env->set_fog_light_color(colour);
            colour = env->get_background_color();
            readColour("background_color", &colour);
            env->set_background_color(colour);
        }
    }
    if (auto* light = cast_to<OmniLight3D>(&node)) {
        light->set_light_energy(static_cast<float>(ReadNumberTopLevel(b, e, "light_energy", 1.0)));
        light->set_omni_range(static_cast<float>(ReadNumberTopLevel(b, e, "omni_range", 5.0)));
        const char* lb = nullptr;
        const char* le = nullptr;
        if (FindKeyTopLevel(b, e, "light") && FindObject(b, e, "light", &lb, &le)) {
            float col[3] = {1.0f, 1.0f, 1.0f};
            ReadFloats(lb, le, "color", 3, col);
            light->set_light_color({col[0], col[1], col[2], 1.0f});
            light->set_omni_attenuation(static_cast<float>(ReadNumberTopLevel(lb, le, "omni_attenuation", 1.0)));
            light->set_shadow(ReadBoolTopLevel(lb, le, "shadow_enabled", false));
            light->set_light_size(static_cast<float>(ReadNumberTopLevel(lb, le, "light_size", 0.0)));
            light->set_light_specular(static_cast<float>(ReadNumberTopLevel(lb, le, "light_specular", 0.5)));
            light->set_negative(ReadBoolTopLevel(lb, le, "light_negative", false));
            light->set_cull_mask(static_cast<uint32_t>(ReadNumberTopLevel(lb, le, "cull_mask", 4294967295.0)));
        }
    }
    if (auto* particles = cast_to<GPUParticles3D>(&node)) ReadParticles(b, e, *particles);
    if (auto* player = cast_to<AnimationPlayer>(&node)) ReadPlayer(b, e, *player);
    if (AudioPlayback* audio = node.as_audio_playback()) ReadAudio(b, e, *audio);
    if (auto* mesh = cast_to<MeshInstance3D>(&node)) mesh->set_mesh_name(ReadStringTopLevelDecoded(b, e, "mesh"));
    if (auto* label3d = cast_to<Label3D>(&node)) {
        label3d->set_text(ReadStringTopLevelDecoded(b, e, "text"));
        float mod[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        ReadFloats(b, e, "modulate", 4, mod);
        label3d->set_modulate({mod[0], mod[1], mod[2], mod[3]});
    }
    if (auto* control = cast_to<Control>(&node)) ReadControl(b, e, *control);
    if (auto* body = cast_to<CollisionObject3D>(&node)) {
        body->collision_layer = static_cast<uint32_t>(ReadNumberTopLevel(b, e, "collision_layer", 1.0));
        body->collision_mask = static_cast<uint32_t>(ReadNumberTopLevel(b, e, "collision_mask", 1.0));
    }
    if (auto* cs = cast_to<CollisionShape3D>(&node)) {
        if (!ReadCollisionShape(b, e, *cs, faces)) return false;
    }
    ReadScript(b, e, node);
    return true;
}

}  // namespace

SurfaceMaterial ReadMaterialJson(std::string_view object) {
    if (object.empty()) return SurfaceMaterial{};
    return ReadMaterial(object.data(), object.data() + object.size());
}

const char* RuntimeSceneErrorText(RuntimeSceneError e) {
    switch (e) {
    case RuntimeSceneError::None:         return "ok";
    case RuntimeSceneError::FileNotFound: return "runtime scene file not found";
    case RuntimeSceneError::BadFormat:    return "runtime scene is not the expected JSON shape";
    case RuntimeSceneError::BadVersion:   return "runtime scene format version mismatch -- re-run export_runtime_scene.gd";
    case RuntimeSceneError::BadParent:    return "a node names a parent that does not precede it";
    }
    return "unknown";
}

RuntimeSceneError BuildRuntimeNodes(std::string_view json, std::vector<BuiltNode>& out,
                                    std::string& rootName, const std::vector<float>* faces) {
    out.clear();
    const char* begin = json.data();
    const char* end = json.data() + json.size();
    if (ReadIntTopLevel(begin, end, "format", -1) != kRuntimeSceneFormat) {
        return FindKeyTopLevel(begin, end, "format") ? RuntimeSceneError::BadVersion
                                                     : RuntimeSceneError::BadFormat;
    }
    rootName = ReadStringTopLevelDecoded(begin, end, "root_name");
    if (rootName.empty()) rootName = "main";
    const char* nb = nullptr;
    const char* ne = nullptr;
    if (!FindArrayTopLevel(begin, end, "nodes", &nb, &ne)) return RuntimeSceneError::BadFormat;

    std::vector<Span> objects;
    SplitObjects(nb, ne, objects);
    out.reserve(objects.size());

    for (const Span& o : objects) {
        std::string path = ReadStringTopLevelDecoded(o.first, o.second, "path");
        const std::string cls = ReadStringTopLevelDecoded(o.first, o.second, "class");
        const int parent = ReadIntTopLevel(o.first, o.second, "parent", -2);
        if (path.empty() || cls.empty()) return RuntimeSceneError::BadFormat;
        if (parent < -1 || parent >= static_cast<int>(out.size())) return RuntimeSceneError::BadParent;
        if ((parent == -1) != out.empty()) return RuntimeSceneError::BadParent;

        std::unique_ptr<Node> node = MakeNode(
            cls, parent == -1 ? rootName : NameFromPath(path),
            FindKeyTopLevel(o.first, o.second, "xform") != nullptr,
            FindKeyTopLevel(o.first, o.second, "layers") != nullptr,
            FindKeyTopLevel(o.first, o.second, "transparency") != nullptr);
        node->set_class(cls);
        if (!ReadState(o.first, o.second, *node, faces)) return RuntimeSceneError::BadFormat;
        out.push_back({std::move(node), parent, std::move(path)});
    }
    return out.empty() ? RuntimeSceneError::BadFormat : RuntimeSceneError::None;
}

RuntimeSceneError LoadRuntimeSceneFromString(std::string_view json, SceneTree& tree,
                                             const std::vector<float>* faces) {
    std::vector<BuiltNode> built;
    std::string rootName;
    const RuntimeSceneError e = BuildRuntimeNodes(json, built, rootName, faces);
    if (e != RuntimeSceneError::None) return e;

    std::vector<Node*> placed;
    placed.reserve(built.size());
    for (BuiltNode& b : built) {
        Node* parentNode = b.parent >= 0 ? placed[static_cast<std::size_t>(b.parent)] : nullptr;
        placed.push_back(tree.add_node(std::move(b.node), parentNode));
        // The path the tree derives must be the path Godot exported, or the
        // parent index and the path disagree about where this node lives.
        if (b.parent != -1 && placed.back()->get_path() != b.path) return RuntimeSceneError::BadParent;
    }
    return RuntimeSceneError::None;
}

int BindAnimationLibrary(SceneTree& tree, const assets::AnimationLibrary& library) {
    int bound = 0;
    for (const assets::AnimationPlayerData& data : library.Players()) {
        if (auto* player = tree.get_node_as<AnimationPlayer>(data.path)) {
            player->set_animations(&data);
            ++bound;
        }
    }
    return bound;
}

RuntimeSceneError LoadRuntimeScene(const char* path, SceneTree& tree) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return RuntimeSceneError::FileNotFound;
    std::string text;
    char buf[65536];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);

    // Concave collision triangles sit beside the export (export format 4):
    // float32 little-endian x, y, z per vertex, the byte order of both targets.
    std::vector<float> faces;
    bool haveFaces = false;
    const std::string facesName = ReadStringTopLevelDecoded(text.data(), text.data() + text.size(), "collision_faces");
    if (!facesName.empty()) {
        std::string dir(path);
        const std::size_t slash = dir.find_last_of("/\\");
        dir = slash == std::string::npos ? std::string() : dir.substr(0, slash + 1);
        if (std::FILE* ff = std::fopen((dir + facesName).c_str(), "rb")) {
            std::vector<unsigned char> bytes;
            while ((n = std::fread(buf, 1, sizeof(buf), ff)) > 0) bytes.insert(bytes.end(), buf, buf + n);
            std::fclose(ff);
            faces.resize(bytes.size() / sizeof(float));
            if (!faces.empty()) std::memcpy(faces.data(), bytes.data(), faces.size() * sizeof(float));
            haveFaces = true;
        }
    }
    return LoadRuntimeSceneFromString(text, tree, haveFaces ? &faces : nullptr);
}

}  // namespace godot
