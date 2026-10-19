// bsht_parity <scenario.json> <reference.json> <runtime_scene.json>
//
// Runs one scenario against the godot:: runtime and diffs the result with the
// dump real Godot produced for the same scenario. Exit 0 equal, 1 different,
// 77 skipped (no reference yet: run tools/godot/make_references.sh), 2 misuse.
#include "assets/animation.h"
#include "core/json_lite.h"
#include "godot/animation.h"
#include "godot/animation_player.h"
#include "godot/audio_server.h"
#include "game/save_store.h"
#include "godot/rng.h"
#include "godot/runtime_scene_loader.h"
#include "godot/control.h"
#include "godot/input.h"
#include "godot/input_map.h"
#include "godot/packed_scene.h"
#include "godot/physics.h"
#include "godot/translation.h"
#include "parity/parity_dump.h"
#include "parity/probes.h"
#include "app/scenario_runner.h"
#include "scripts/BlinkerBranch.h"
#include "scripts/ButtonClass.h"
#include "scripts/HeavenManager.h"
#include "scripts/DonUnlockManager.h"
#include "scripts/EndingManager.h"
#include "scripts/BriefcaseMachine.h"
#include "scripts/CameraManager.h"
#include "scripts/CursorManager.h"
#include "scripts/MenuManager.h"
#include "scripts/OptionsManager.h"
#include "scripts/SaveFileManager.h"
#include "scripts/HealthCounter.h"
#include "scripts/InteractionBranch.h"
#include "scripts/InteractionManager.h"
#include "scripts/IntroManager.h"
#include "scripts/SignatureManager.h"
#include "scripts/ItemManager.h"
#include "scripts/MouseRaycast.h"
#include "scripts/ObjectShaker.h"
#include "scripts/PickupIndicator.h"
#include "scripts/CrtManager.h"
#include "scripts/CrtIcon.h"
#include "scripts/PartitionBranch.h"
#include "scripts/UserExit.h"
#include "scripts/LeaderboardManager.h"
#include "scripts/RoundManager.h"
#include "scripts/ShellSpawner.h"
#include "scripts/TextShaker.h"
#include "scripts/bind_support.h"
#include "scripts/intro_skip.h"
#include "scripts/item_scenes.h"
#include "scripts/resources.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr int kSkip = 77;

std::string ReadAll(const char* path) {
    std::string text;
    if (std::FILE* f = std::fopen(path, "rb")) {
        char buf[65536];
        std::size_t n = 0;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
        std::fclose(f);
    }
    return text;
}

void CaptureNodes(const godot::SceneTree& tree, parity::Dump& out) {
    for (godot::Node* n : tree.nodes_in_tree_order()) {
        const godot::Node3D* n3 = godot::cast_to<godot::Node3D>(n);
        if (!n3) continue;
        parity::NodeState s;
        s.path = n3->get_path();
        const core::Mat4& g = n3->get_global_transform();
        const int src[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
        for (int i = 0; i < 12; ++i) s.g[i] = g.m[src[i]];
        s.v = n3->is_visible();
        s.vt = n3->is_visible_in_tree();
        if (const auto* vi = godot::cast_to<godot::VisualInstance3D>(n)) {
            s.hasLayers = true;
            s.l = vi->get_layers();
        }
        if (const auto* gi = godot::cast_to<godot::GeometryInstance3D>(n)) {
            s.hasTransparency = true;
            s.t = gi->get_transparency();
        }
        // Mirrors dump_state.gd: a MeshInstance3D's surface-0 active material,
        // BaseMaterial3D only (the export marks anything else transparency -1).
        if (const auto* mi = godot::cast_to<godot::MeshInstance3D>(n); mi && mi->surface_count() > 0) {
            const godot::SurfaceMaterial& m = mi->has_material_override()
                                                  ? mi->get_material_override()
                                                  : mi->get_surface_material(0);
            if (m.transparency >= 0) {
                s.hasAlbedo = true;
                s.a[0] = m.albedo.r;
                s.a[1] = m.albedo.g;
                s.a[2] = m.albedo.b;
                s.a[3] = m.albedo.a;
            }
        }
        out.nodes.push_back(std::move(s));
    }
}

struct RngOp {
    std::string name;
    double a = 0.0;
    double b = 0.0;
};

// Each op is a small array: ["name"] or ["name", a, b].
std::vector<RngOp> ReadOps(const char* ob, const char* oe) {
    using namespace core::json;
    std::vector<RngOp> ops;
    for (const char* p = ob; p < oe; ++p) {
        if (*p != '[') continue;
        const char* q = p + 1;
        while (q < oe && *q != ']') ++q;
        std::vector<std::string> names;
        ReadStringArray(p + 1, q, names);
        const char* afterName = p + 1;
        while (afterName < q && *afterName != ',') ++afterName;
        std::vector<double> nums;
        ReadNumberArray(afterName, q, nums);
        RngOp op;
        op.name = names.empty() ? std::string() : names[0];
        if (nums.size() > 0) op.a = nums[0];
        if (nums.size() > 1) op.b = nums[1];
        ops.push_back(op);
        p = q;
    }
    return ops;
}

void RunRng(const std::string& scenario, parity::Dump& out) {
    using namespace core::json;
    const char* b = scenario.data();
    const char* e = b + scenario.size();
    godot::seed(static_cast<uint64_t>(ReadNumberTopLevel(b, e, "seed", 0.0)));
    const int repeat = ReadIntTopLevel(b, e, "repeat", 0);
    const char* ob = nullptr;
    const char* oe = nullptr;
    if (!FindArrayTopLevel(b, e, "ops", &ob, &oe)) return;
    const std::vector<RngOp> ops = ReadOps(ob, oe);
    for (int i = 0; i < repeat; ++i) {
        for (const RngOp& op : ops) {
            if (op.name == "randi_range") {
                out.rng.push_back(static_cast<double>(godot::randi_range(
                    static_cast<int64_t>(op.a), static_cast<int64_t>(op.b))));
            } else if (op.name == "randf_range") {
                out.rng.push_back(godot::randf_range(op.a, op.b));
            } else if (op.name == "randf") {
                out.rng.push_back(godot::randf());
            } else if (op.name == "randi") {
                out.rng.push_back(static_cast<double>(godot::randi()));
            }
        }
    }
}

std::string DirOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// Mirrors _numbers() in tools/godot/dump_state.gd.
bool Numbers(const assets::Value& v, std::vector<double>& out) {
    int n = 0;
    switch (v.kind) {
    case assets::ValueKind::Bool:
    case assets::ValueKind::Int:
    case assets::ValueKind::Float: n = 1; break;
    case assets::ValueKind::Vec2:  n = 2; break;
    case assets::ValueKind::Vec3:  n = 3; break;
    case assets::ValueKind::Quat:
    case assets::ValueKind::Color: n = 4; break;
    default: return false;
    }
    for (int i = 0; i < n; ++i) out.push_back(v.f[i]);
    return true;
}

// Mirrors _sample_track() in tools/godot/dump_state.gd.
bool SampleTrack(const assets::Animation& anim, int track, double time, std::vector<double>& out) {
    switch (anim.tracks[static_cast<std::size_t>(track)].type) {
    case assets::TrackType::Value: {
        assets::Value v;
        return godot::value_track_interpolate(anim, track, time, v) && Numbers(v, out);
    }
    case assets::TrackType::Position3D:
    case assets::TrackType::Scale3D: {
        godot::Vector3 p;
        const bool ok = anim.tracks[static_cast<std::size_t>(track)].type == assets::TrackType::Position3D
                            ? godot::position_track_interpolate(anim, track, time, p)
                            : godot::scale_track_interpolate(anim, track, time, p);
        if (!ok) return false;
        out = {p.x, p.y, p.z};
        return true;
    }
    case assets::TrackType::Rotation3D: {
        godot::Quaternion q;
        if (!godot::rotation_track_interpolate(anim, track, time, q)) return false;
        out = {q.x, q.y, q.z, q.w};
        return true;
    }
    default:
        return false;
    }
}

// False when animations.json is missing beside the runtime scene.
bool RunAnimSample(const std::string& scenario, const char* runtimeScenePath, parity::Dump& out) {
    using namespace core::json;
    assets::AnimationLibrary lib;
    if (!lib.LoadFromFile((DirOf(runtimeScenePath) + "/animations.json").c_str())) return false;
    const char* b = scenario.data();
    const char* e = b + scenario.size();
    std::vector<double> fractions;
    const char* fb = nullptr;
    const char* fe = nullptr;
    if (FindArrayTopLevel(b, e, "fractions", &fb, &fe)) ReadNumberArray(fb, fe, fractions);
    const double pastEnd = ReadNumberTopLevel(b, e, "past_end", 0.02);

    for (const assets::AnimationPlayerData& player : lib.Players()) {
        for (const assets::Animation& anim : player.animations) {
            std::vector<double> times;
            for (double f : fractions) times.push_back(static_cast<double>(anim.length) * f);
            times.push_back(static_cast<double>(anim.length) + pastEnd);
            for (int t = 0; t < static_cast<int>(anim.tracks.size()); ++t) {
                for (std::size_t ti = 0; ti < times.size(); ++ti) {
                    parity::Sample s;
                    if (!SampleTrack(anim, t, times[ti], s.v)) continue;
                    s.id = player.path + "|" + anim.name + "|" + std::to_string(t) + "|" +
                           std::to_string(ti);
                    out.samples.push_back(std::move(s));
                }
            }
        }
    }
    return true;
}

// Mirrors _watch_numbers() in tools/godot/dump_state.gd.
std::vector<double> WatchNumbers(const godot::Node3D& n) {
    const core::Mat4& g = n.get_global_transform();
    std::vector<double> out;
    for (int i : {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14}) out.push_back(g.m[i]);
    out.push_back(n.is_visible_in_tree() ? 1.0 : 0.0);
    if (const auto* mi = godot::cast_to<godot::MeshInstance3D>(&n); mi && mi->surface_count() > 0) {
        const godot::SurfaceMaterial& m =
            mi->has_material_override() ? mi->get_material_override() : mi->get_surface_material(0);
        if (m.transparency >= 0) out.push_back(m.albedo.a);
    }
    return out;
}

std::vector<std::string> ReadStrings(const char* b, const char* e, const char* key) {
    std::vector<std::string> out;
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (core::json::FindArrayTopLevel(b, e, key, &ab, &ae)) core::json::ReadStringArray(ab, ae, out);
    return out;
}

// Mirrors _step_main_shoot() in tools/godot/dump_state.gd: the loaded tree sits
// where Godot's does on start_frame, before that frame's nodes process.
// False when the runtime scene or animations.json is missing.
bool RunMainShoot(const std::string& scenario, const char* runtimeScenePath, parity::Dump& out) {
    using namespace core::json;
    godot::SceneTree tree;
    tree.set_particle_rng(false);   // headless references: a dummy renderer
    // dump_state.gd seed(0)s before instantiating main.tscn, pinning the draws
    // _ready makes (blinker intervals and the like). The tree's constructor
    // randomizes the generator as Godot's does, so the harness must too.
    godot::seed(0);
    assets::AnimationLibrary lib;
    if (godot::LoadRuntimeScene(runtimeScenePath, tree) != godot::RuntimeSceneError::None ||
        !lib.LoadFromFile((DirOf(runtimeScenePath) + "/animations.json").c_str())) {
        return false;
    }
    godot::BindAnimationLibrary(tree, lib);

    const char* b = scenario.data();
    const char* e = b + scenario.size();
    const int start = ReadIntTopLevel(b, e, "start_frame", 3);
    const int frames = ReadIntTopLevel(b, e, "frames", 0);
    const int every = std::max(1, ReadIntTopLevel(b, e, "every", 1));
    const std::vector<std::string> watch = ReadStrings(b, e, "watch");
    const std::vector<std::string> audioWatch = ReadStrings(b, e, "audio_watch");
    std::vector<std::string> audioSeen;

    for (int f = start; f <= start + frames; ++f) {
        for (const std::string& path : audioWatch) {
            godot::Node* node = tree.get_node(path);
            const godot::AudioPlayback* a = node ? node->as_audio_playback() : nullptr;
            if (!a || !a->is_playing()) continue;
            if (std::find(audioSeen.begin(), audioSeen.end(), path) != audioSeen.end()) continue;
            audioSeen.push_back(path);
            out.events.push_back("f" + std::to_string(f) + ":audio:" + path);
        }
        if ((f - start) % every == 0) {
            for (const std::string& path : watch) {
                parity::Sample s;
                s.id = "f" + std::to_string(f) + "|" + path;
                if (const auto* n = tree.get_node_as<godot::Node3D>(path)) s.v = WatchNumbers(*n);
                out.samples.push_back(std::move(s));
            }
        }
        if (f == start + frames) break;
        if (f == start) {
            if (auto* p = tree.get_node_as<godot::AnimationPlayer>(
                    ReadStringTopLevel(b, e, "player"))) {
                p->play(ReadStringTopLevel(b, e, "animation"));
            }
        }
        tree.process_frame(1.0 / 60.0);
    }
    return true;
}

// Mirrors _ceremony_samples() in tools/godot/dump_state.gd.
void CeremonySamples(const godot::SceneTree& tree, int f, std::vector<parity::Sample>& out) {
    const std::string at = "f" + std::to_string(f) + "|";
    parity::Sample cam;
    cam.id = at + "cam";
    if (const godot::Camera3D* c = tree.get_node_as<godot::Camera3D>("Camera")) {
        const core::Mat4& g = c->get_global_transform();
        cam.v = {g.m[12], g.m[13], g.m[14], c->get_fov()};
    }
    out.push_back(std::move(cam));

    parity::Sample dlg;
    dlg.id = at + "dialogue";
    const auto* lbl = tree.get_node_as<godot::Label>("Camera/dialogue UI/main dialogue/dialogue text");
    const auto* bd = tree.get_node_as<godot::ColorRect>("Camera/dialogue UI/main dialogue/dialogue backdrop");
    if (lbl && bd) {
        dlg.v = {lbl->is_visible() ? 1.0 : 0.0, static_cast<double>(lbl->get_visible_characters()),
                 static_cast<double>(lbl->get_total_character_count()), bd->is_visible() ? 1.0 : 0.0};
    }
    out.push_back(std::move(dlg));

    parity::Sample shells;
    shells.id = at + "shells";
    if (godot::Node* comp = tree.get_node("tabletop parent/main tabletop/compartment_shells")) {
        for (int i = 0; i < comp->get_child_count(); ++i) {
            const auto* m = godot::cast_to<godot::MeshInstance3D>(comp->get_child(i));
            if (!m) continue;
            const core::Mat4& g = m->get_global_transform();
            const godot::Color c = m->get_active_surface_albedo(1);
            shells.v.insert(shells.v.end(), {g.m[12], g.m[13], g.m[14], c.r, c.g, c.b});
        }
    }
    out.push_back(std::move(shells));

    parity::Sample counter;
    counter.id = at + "counter";
    auto* hc = godot::script_cast<scripts::HealthCounter>(tree.get_node("standalone managers/health counter"));
    auto* rm = godot::script_cast<scripts::RoundManager>(tree.get_node("standalone managers/round manager"));
    if (hc && rm) {
        for (const auto* s : hc->symbolArray_player) counter.v.push_back(s->is_visible() ? 1.0 : 0.0);
        for (const auto* s : hc->symbolArray_enemy) counter.v.push_back(s->is_visible() ? 1.0 : 0.0);
        for (const auto* u : hc->uiArray) counter.v.push_back(u->is_visible() ? 1.0 : 0.0);
        counter.v.push_back(rm->roundIndicatorParent->is_visible() ? 1.0 : 0.0);
        counter.v.push_back(rm->roundIndicator->is_visible() ? 1.0 : 0.0);
        counter.v.push_back(rm->health_player);
        counter.v.push_back(rm->health_opponent);
    }
    out.push_back(std::move(counter));

    // Mirrors the "indicator" sample: global basis columns then origin of the
    // round indicator parent and the round indicator.
    parity::Sample indicator;
    indicator.id = at + "indicator";
    if (rm) {
        for (const godot::Node3D* n : {static_cast<godot::Node3D*>(rm->roundIndicatorParent),
                                       static_cast<godot::Node3D*>(rm->roundIndicator)}) {
            if (!n) continue;
            const core::Mat4& g = n->get_global_transform();
            indicator.v.insert(indicator.v.end(), {g.m[0], g.m[1], g.m[2], g.m[4], g.m[5], g.m[6],
                                                   g.m[8], g.m[9], g.m[10], g.m[12], g.m[13], g.m[14]});
        }
    }
    out.push_back(std::move(indicator));

    parity::Sample seq;
    seq.id = at + "sequence";
    if (auto* ss = godot::script_cast<scripts::ShellSpawner>(tree.get_node("standalone managers/shell spawner"))) {
        for (const std::string& s : ss->sequenceArray) seq.v.push_back(s == "live" ? 1.0 : 0.0);
    }
    out.push_back(std::move(seq));
}

// Mirrors _step_ceremony() in tools/godot/dump_state.gd, around process_frame
// as RunMainShoot does. False when an export is missing beside argv[3].
bool RunCeremony(const std::string& scenario, const char* runtimeScenePath, parity::Dump& out) {
    using namespace core::json;
    const std::string dir = DirOf(runtimeScenePath);
    assets::AnimationLibrary lib;
    godot::SceneTree tree;
    tree.set_particle_rng(false);   // headless references: a dummy renderer
    // dump_state.gd seed(0)s before instantiating main.tscn, pinning the draws
    // _ready makes (blinker intervals and the like). The tree's constructor
    // randomizes the generator as Godot's does, so the harness must too.
    godot::seed(0);
    if (!godot::load_translations((dir + "/dialogue_en.json").c_str())) return false;
    scripts::RegisterScripts();
    if (!godot::PackedScene::load("res://instances/shell.tscn", (dir + "/runtime_shell.json").c_str())) return false;
    if (godot::LoadRuntimeScene(runtimeScenePath, tree) != godot::RuntimeSceneError::None ||
        !lib.LoadFromFile((dir + "/animations.json").c_str())) {
        return false;
    }
    godot::BindAnimationLibrary(tree, lib);
    const scripts::BindResult bound = scripts::BindScripts(tree.get_root());
    for (const std::string& e : bound.errors) std::printf("bind: %s\n", e.c_str());
    tree.ready_scripts();
    auto* rm = godot::script_cast<scripts::RoundManager>(tree.get_node("standalone managers/round manager"));
    auto* cm = godot::script_cast<scripts::CameraManager>(tree.get_node("standalone managers/camera manager"));
    if (!rm || !cm || !rm->playerData) return false;

    const char* b = scenario.data();
    const char* e = b + scenario.size();
    const int start = ReadIntTopLevel(b, e, "start_frame", 3);
    const int frames = ReadIntTopLevel(b, e, "frames", 0);
    const int every = std::max(1, ReadIntTopLevel(b, e, "every", 1));
    const std::vector<std::string> players = ReadStrings(b, e, "players");
    std::vector<std::string> last(players.size());

    // Diagnostic (quiet_rng) for the ceremony path too, so the rng probe
    // measures the ceremony's own draws instead of 112 blinker loops.
    if (ReadBoolTopLevel(b, e, "quiet_rng", false)) {
        for (godot::Node* n : tree.nodes_in_tree_order()) {
            if (auto* blinker = godot::script_cast<scripts::BlinkerBranch>(n)) blinker->looping = false;
            if (auto* shaker = godot::script_cast<scripts::TextShaker>(n)) shaker->looping = false;
        }
    }

    for (int f = start;; ++f) {
        for (std::size_t i = 0; i < players.size(); ++i) {
            const auto* ap = tree.get_node_as<godot::AnimationPlayer>(players[i]);
            const std::string cur = ap ? ap->get_current_animation() : std::string();
            if (f > start && cur != last[i]) {
                out.events.push_back("f" + std::to_string(f) + ":anim:" + players[i] + ":" + cur);
            }
            last[i] = cur;
        }
        if ((f - start) % every == 0) CeremonySamples(tree, f, out.samples);
        if (f >= start + frames) break;
        if (f == start) {
            godot::seed(static_cast<uint64_t>(ReadNumberTopLevel(b, e, "seed", 0.0)));
            rm->playerData->hasSignedWaiver = true;
            for (const scripts::CameraSocket* s : cm->socketArray) {
                if (s && s->socketName == "home") {
                    cm->cam->set_position(s->pos);
                    cm->cam->set_rotation_degrees(s->rot);
                    cm->cam->set_fov(static_cast<float>(s->fov));
                }
            }
            scripts::SkipIntro(tree);   // dump_state.gd frees the intro manager here
            tree.spawn(rm->MainBatchSetup(true));
        }
        tree.process_frame(1.0 / 60.0);
    }
    return true;
}

// grab_shotgun_b1r1 / grab_shotgun_touch_b1r1: tools/godot/dump_state.gd
// _grab_samples is the twin; change both. Every field is what an interaction
// script decided, and most of them are names.
void GrabSamples(godot::SceneTree& tree, int f, std::vector<parity::Sample>& out) {
    const std::string id = "f" + std::to_string(f) + "|";
    // The camera itself: the shot lerps it to "enemy" and back to "home".
    parity::Sample camxf;
    camxf.id = id + "camxf";
    if (const godot::Camera3D* c = tree.get_node_as<godot::Camera3D>("Camera")) {
        const core::Mat4& g = c->get_global_transform();
        const int src[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
        for (int i = 0; i < 12; ++i) camxf.v.push_back(g.m[src[i]]);
        camxf.v.push_back(c->get_fov());
    }
    out.push_back(std::move(camxf));
    const auto* ray = godot::script_cast<scripts::MouseRaycast>(tree.get_node("Camera"));
    const auto* interaction =
        godot::script_cast<scripts::InteractionManager>(tree.get_node("standalone managers/interaction manager"));
    const auto* cursor =
        godot::script_cast<scripts::CursorManager>(tree.get_node("standalone managers/cursor manager"));
    const godot::Control* focus = tree.gui_get_focus_owner();

    parity::Sample focusSample;
    focusSample.id = id + "focus";
    focusSample.s = focus ? focus->get_path() : std::string();
    out.push_back(std::move(focusSample));

    parity::Sample raySample;
    raySample.id = id + "ray";
    if (ray) {
        raySample.v = {ray->mouse.x, ray->mouse.y, ray->controller_overriding ? 1.0 : 0.0};
        if (ray->result.has_value() && ray->result->collider) raySample.s = ray->result->collider->get_path();
    }
    out.push_back(std::move(raySample));

    parity::Sample branchSample;
    branchSample.id = id + "branch";
    if (interaction && interaction->activeInteractionBranch) {
        branchSample.s = interaction->activeInteractionBranch->interactionAlias;
    }
    branchSample.v = {cursor && cursor->controller_active ? 1.0 : 0.0,
                      cursor && cursor->cursor_visible ? 1.0 : 0.0};
    out.push_back(std::move(branchSample));

    parity::Sample gun;
    gun.id = id + "gun";
    if (const auto* n = tree.get_node_as<godot::Node3D>("tabletop parent/shotgun main parent/shotgun parent new")) {
        const godot::Vector3 p = n->get_position();
        gun.v = {p.x, p.y, p.z};
    }
    out.push_back(std::move(gun));

    parity::Sample choice;
    choice.id = id + "choice";
    const auto* parent = tree.get_node_as<godot::Control>("Camera/dialogue UI/bracket ui/shooting choice");
    const auto* dealer = tree.get_node_as<godot::Label3D>("ui parent_shooting decision/text dealer");
    const auto* you = tree.get_node_as<godot::Label3D>("ui parent_shooting decision/text you");
    const auto* dealerBody =
        tree.get_node_as<godot::CollisionObject3D>("ui parent_shooting decision/text dealer/StaticBody3D");
    const auto* youBody =
        tree.get_node_as<godot::CollisionObject3D>("ui parent_shooting decision/text you/StaticBody3D");
    choice.v = {parent && parent->is_visible() ? 1.0 : 0.0,
                dealer ? dealer->get_transparency() : 0.0,
                you ? you->get_transparency() : 0.0,
                dealer ? dealer->get_modulate().r : 0.0,
                you ? you->get_modulate().r : 0.0,
                dealerBody ? static_cast<double>(dealerBody->collision_layer) : 0.0,
                youBody ? static_cast<double>(youBody->collision_layer) : 0.0};
    out.push_back(std::move(choice));

    parity::Sample anim;
    anim.id = id + "anim";
    if (const auto* ap =
            tree.get_node_as<godot::AnimationPlayer>("tabletop parent/shotgun main parent/anim_shotgun main parent")) {
        anim.s = ap->get_current_animation();
    }
    out.push_back(std::move(anim));

    parity::Sample desc;
    desc.id = id + "desc";
    if (const auto* label = tree.get_node_as<godot::Label>("Camera/dialogue UI/item description/item name")) {
        desc.s = label->get_text();
    }
    out.push_back(std::move(desc));

    // The two halves of the gun's x, separately: PickupIndicator lerps it and
    // ObjectShaker adds a random offset on top. Comparing only the sum cannot
    // say which of them differs.
    parity::Sample slide;
    slide.id = id + "slide";
    if (const auto* indicator = godot::script_cast<scripts::PickupIndicator>(
            tree.get_node("tabletop parent/shotgun main parent/shotgun parent new/pickup indicator branch"))) {
        slide.v = {indicator->current, indicator->next, indicator->elapsed, indicator->moving ? 1.0 : 0.0};
    }
    out.push_back(std::move(slide));

    parity::Sample shake;
    shake.id = id + "shake";
    if (const auto* shaker = godot::script_cast<scripts::ObjectShaker>(
            tree.get_node("tabletop parent/shotgun main parent/shotgun parent new/obj shaker branch"))) {
        shake.v = {shaker->originalPos.x, shaker->shaking ? 1.0 : 0.0};
    }
    out.push_back(std::move(shake));
}

// items_*: tools/godot/dump_state.gd _item_samples is the twin; change both.
// Items are identified by what they are (InteractionBranch.itemName), never by
// node name: an instanced item is named @Node3D@N, and the counter N differs
// between the engines. Which items were dealt is an RNG-parity check; where
// they landed is a transform check (and settles the handcuff height).
void ItemSamples(godot::SceneTree& tree, int f, std::vector<parity::Sample>& out) {
    const std::string id = "f" + std::to_string(f) + "|";
    parity::Sample camxf;
    camxf.id = id + "camxf";
    if (const godot::Camera3D* c = tree.get_node_as<godot::Camera3D>("Camera")) {
        const core::Mat4& g = c->get_global_transform();
        const int src[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
        for (int i = 0; i < 12; ++i) camxf.v.push_back(g.m[src[i]]);
        camxf.v.push_back(c->get_fov());
    }
    out.push_back(std::move(camxf));

    const auto* im = godot::script_cast<scripts::ItemManager>(tree.get_node("standalone managers/item manager"));
    parity::Sample counts;
    counts.id = id + "items";
    if (im) {
        counts.v = {im->interaction_intake && im->interaction_intake->interactionAllowed ? 1.0 : 0.0,
                    static_cast<double>(im->numberOfItemsGrabbed), static_cast<double>(im->numberOfOccupiedGrids),
                    static_cast<double>(im->numberOfItemsGrabbed_enemy)};
    }
    out.push_back(std::move(counts));

    // What an item use changes: the chamber, both healths, the damage, the
    // cuffs and the saw, and what the dealer says.
    const auto* rm = godot::script_cast<scripts::RoundManager>(tree.get_node("standalone managers/round manager"));
    parity::Sample state;
    state.id = id + "state";
    if (rm && rm->shellSpawner) {
        for (const std::string& shell : rm->shellSpawner->sequenceArray) state.v.push_back(shell == "live" ? 1.0 : 0.0);
        state.v.push_back(-1.0);
        state.v.insert(state.v.end(), {static_cast<double>(rm->health_player), static_cast<double>(rm->health_opponent),
                                       static_cast<double>(rm->currentShotgunDamage), rm->dealerCuffed ? 1.0 : 0.0,
                                       rm->playerCuffed ? 1.0 : 0.0, rm->barrelSawedOff ? 1.0 : 0.0});
    }
    out.push_back(std::move(state));
    parity::Sample dia;
    dia.id = id + "dialogue";
    if (const auto* l = tree.get_node_as<godot::Label>("Camera/dialogue UI/main dialogue/dialogue text")) {
        dia.s = l->get_text();
    }
    out.push_back(std::move(dia));

    // Where the pointer ray comes from and what it hits: the item a tap picks
    // depends on it (mouse, or the focused control when a controller
    // overrides). Same fields as GrabSamples' ray and branch.
    const auto* ray = godot::script_cast<scripts::MouseRaycast>(tree.get_node("Camera"));
    const auto* interaction =
        godot::script_cast<scripts::InteractionManager>(tree.get_node("standalone managers/interaction manager"));
    const auto* cursor =
        godot::script_cast<scripts::CursorManager>(tree.get_node("standalone managers/cursor manager"));
    parity::Sample raySample;
    raySample.id = id + "ray";
    if (ray) {
        raySample.v = {ray->mouse.x, ray->mouse.y, ray->controller_overriding ? 1.0 : 0.0};
        if (ray->result.has_value() && ray->result->collider) raySample.s = ray->result->collider->get_path();
    }
    out.push_back(std::move(raySample));
    parity::Sample branchSample;
    branchSample.id = id + "branch";
    if (interaction && interaction->activeInteractionBranch) {
        branchSample.s = interaction->activeInteractionBranch->interactionAlias;
    }
    branchSample.v = {cursor && cursor->controller_active ? 1.0 : 0.0, cursor && cursor->cursor_visible ? 1.0 : 0.0};
    out.push_back(std::move(branchSample));

    godot::Node* parent = im ? im->itemSpawnParent : nullptr;
    int k = 0;
    for (int i = 0; parent && i < parent->get_child_count(); ++i) {
        auto* item = godot::cast_to<godot::Node3D>(parent->get_child(i));
        if (!item || item->get_child_count() < 2) continue;
        const auto* branch = godot::script_cast<scripts::InteractionBranch>(item->get_child(1));
        if (!branch || branch->itemName.empty()) continue;
        parity::Sample s;
        s.id = id + "item" + std::to_string(k++);
        s.s = branch->itemName;
        const core::Mat4& g = item->get_global_transform();
        // Position, then the basis columns: a mis-rotated item draws and
        // collides wrong while its position still matches.
        s.v = {g.m[12], g.m[13], g.m[14], g.m[0], g.m[1], g.m[2], g.m[4], g.m[5], g.m[6], g.m[8], g.m[9], g.m[10]};
        out.push_back(std::move(s));
    }
}

// intro_*: tools/godot/dump_state.gd _intro_samples is the twin; change both.
void IntroSamples(godot::SceneTree& tree, int f, std::vector<parity::Sample>& out) {
    const std::string id = "f" + std::to_string(f) + "|";
    auto* im = godot::script_cast<scripts::IntroManager>(tree.get_node("standalone managers/intro manager"));
    const auto* cam = tree.get_node_as<godot::Camera3D>("Camera");
    const auto* blocker = tree.get_node_as<godot::Control>("Camera/dialogue UI/viewblocker parent/viewblcoker");
    if (!im || !cam || !blocker) return;
    parity::Sample camxf{id + "camxf", {}, ""};
    const core::Mat4& g = cam->get_global_transform();
    const int src[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
    for (int i = 0; i < 12; ++i) camxf.v.push_back(g.m[src[i]]);
    camxf.v.push_back(cam->get_fov());
    out.push_back(std::move(camxf));
    out.push_back({id + "anim", {}, im->animator_camera->get_current_animation()});
    out.push_back({id + "flags",
                   {im->intbranch_bathroomdoor->interactionAllowed ? 1.0 : 0.0,
                    im->intbranch_backroomdoor->interactionAllowed ? 1.0 : 0.0, im->counting ? 1.0 : 0.0,
                    blocker->is_visible() ? 1.0 : 0.0, im->smokerdude_revival->is_visible() ? 1.0 : 0.0,
                    im->parent_pills->is_visible() ? 1.0 : 0.0},
                   ""});
    const godot::Control* focus = tree.gui_get_focus_owner();
    out.push_back({id + "focus", {}, focus ? focus->get_path() : std::string()});
    const auto* bath = tree.get_node_as<godot::Node3D>("intro parent/bathroom door");
    const auto* back = tree.get_node_as<godot::Node3D>("intro parent/backroom door");
    if (bath && back) {
        const godot::Vector3 a = bath->get_rotation();
        const godot::Vector3 b = back->get_rotation();
        out.push_back({id + "doors", {a.x, a.y, a.z, b.x, b.y, b.z}, ""});
    }
}

// waiver_*: tools/godot/dump_state.gd _waiver_samples is the twin; change
// both. The release form is freed after Enter, so its branch is looked up by
// the path taken on the first sample, never read through
// sm->intrbranch_waiver after.
void WaiverSamples(godot::SceneTree& tree, int f, std::vector<parity::Sample>& out) {
    auto* sm = godot::script_cast<scripts::Signature>(tree.get_node("standalone managers/signature manager"));
    auto* rm = godot::script_cast<scripts::RoundManager>(tree.get_node("standalone managers/round manager"));
    if (!sm || !rm) return;
    static std::string branchPath;
    if (f <= 0 || branchPath.empty()) branchPath = sm->intrbranch_waiver->get_node_owner()->get_path();
    const auto* branch = godot::script_cast<scripts::InteractionBranch>(tree.get_node(branchPath));
    std::string letters;
    for (const godot::Label3D* l : sm->letterArray) letters += l->get_text();
    std::string signedName;
    for (const godot::Label3D* l : sm->letterArray_signature_joined) signedName += l->get_text();
    out.push_back({"f" + std::to_string(f) + "|waiver",
                   {static_cast<double>(sm->markerIndex), rm->playerData->hasSignedWaiver ? 1.0 : 0.0,
                    sm->marker->is_visible() ? 1.0 : 0.0, branch && branch->interactionAllowed ? 1.0 : 0.0,
                    sm->parent_signatureMachineMainParent->is_visible() ? 1.0 : 0.0,
                    sm->marker->get_modulate().a},
                   letters + "|" + signedName});
}

// ending_*: tools/godot/dump_state.gd _ending_samples is the twin; change both.
void EndingSamples(godot::SceneTree& tree, int f, const game::SaveStore& store, std::vector<parity::Sample>& out) {
    const std::string id = "f" + std::to_string(f) + "|";
    auto* brief = godot::script_cast<scripts::BriefcaseMachine>(tree.get_node("standalone managers/briefcase machine"));
    const auto* cam = tree.get_node_as<godot::Camera3D>("Camera");
    const auto* blocker = tree.get_node_as<godot::Control>("Camera/dialogue UI/viewblocker parent/viewblcoker");
    if (!brief || !brief->ending || !cam || !blocker) return;
    scripts::EndingManager* e = brief->ending;
    parity::Sample camxf{id + "camxf", {}, ""};
    const core::Mat4& g = cam->get_global_transform();
    const int src[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
    for (int i = 0; i < 12; ++i) camxf.v.push_back(g.m[src[i]]);
    camxf.v.push_back(cam->get_fov());
    out.push_back(std::move(camxf));
    out.push_back({id + "anim", {},
                   brief->anim_machine->get_current_animation() + "|" + brief->anim_lid->get_current_animation() +
                       "|" + e->animator_pan->get_current_animation() + "|" +
                       e->animator_cam->get_current_animation() + "|" +
                       e->animator_viewblocker->get_current_animation() + "|" +
                       e->animator_anykey->get_current_animation()});
    out.push_back({id + "flags",
                   {brief->parent_eyes->is_visible() ? 1.0 : 0.0, brief->parent_machine->is_visible() ? 1.0 : 0.0,
                    brief->intbranch_L->interactionAllowed ? 1.0 : 0.0,
                    brief->intbranch_R->interactionAllowed ? 1.0 : 0.0,
                    brief->intbranch_lid->interactionAllowed ? 1.0 : 0.0,
                    brief->btnParent_briefcase->is_visible() ? 1.0 : 0.0, blocker->is_visible() ? 1.0 : 0.0,
                    e->waitingForInput ? 1.0 : 0.0, e->blockout[0]->is_visible() ? 1.0 : 0.0,
                    store.Exists(scripts::Unlocker::savepath) ? 1.0 : 0.0,
                    store.Exists(scripts::SaveFileManager::savePath_stats) ? 1.0 : 0.0},
                   ""});
    std::string texts = e->label_congrats->get_text();
    for (const godot::Label* l : e->label_array) texts += "|" + l->get_text();
    texts += "|" + e->unlocker->ui->get_text();
    out.push_back({id + "text",
                   {static_cast<double>(e->label_congrats->get_visible_characters()),
                    static_cast<double>(e->unlocker->ui->get_visible_characters()),
                    e->label_congrats->is_visible() ? 1.0 : 0.0, e->label_array[5]->is_visible() ? 1.0 : 0.0,
                    e->pp->get_adjustment_brightness(), e->pp->get_adjustment_contrast(),
                    e->pp->get_adjustment_saturation()},
                   texts});
    const godot::Control* focus = tree.gui_get_focus_owner();
    out.push_back({id + "focus", {}, focus ? focus->get_path() : std::string()});
    auto* rm = godot::script_cast<scripts::RoundManager>(tree.get_node("standalone managers/round manager"));
    if (rm && rm->playerData) {
        out.push_back({id + "player",
                       {rm->playerData->playerEnteringFromDeath ? 1.0 : 0.0,
                        rm->playerData->enteringFromTrueDeath ? 1.0 : 0.0},
                       ""});
    }
}

// dump_state.gd _crt_samples: the restroom CRT boots, cycles and exits.
void CrtSamples(godot::SceneTree& tree, int f, std::vector<parity::Sample>& out) {
    auto* crt = godot::script_cast<scripts::CRT>(tree.get_node("standalone managers/crt manager"));
    if (!crt) return;
    std::string icons;
    for (const scripts::CrtIcon* icon : crt->iconbranches) {
        icons += icon->instance->get_active_surface_texture(0) == icon->texture_active ? "1" : "0";
    }
    const std::string id = "f" + std::to_string(f) + "|";
    out.push_back({id + "crt",
                   {static_cast<double>(crt->window_index), crt->viewing ? 1.0 : 0.0, crt->has_exited ? 1.0 : 0.0,
                    crt->screenparent_stats->is_visible() ? 1.0 : 0.0,
                    crt->screenparent_leaderboard->is_visible() ? 1.0 : 0.0,
                    crt->array_bootup[0]->is_visible() ? 1.0 : 0.0, crt->array_bootuplogo[0]->is_visible() ? 1.0 : 0.0,
                    crt->array_stats[0]->is_visible() ? 1.0 : 0.0, crt->branch_window->interactionAllowed ? 1.0 : 0.0,
                    crt->exit->exitAllowed ? 1.0 : 0.0, crt->bathroom_broken->is_visible() ? 1.0 : 0.0},
                   icons + "|" + (crt->array_partbranch[0]->p->get_text().rfind("  ", 0) == 0 ? "blank" : "digits") +
                       "|" + crt->anim_iconfade->get_current_animation()});
}

// dump_state.gd _don_samples: the score lerp, the yes/no machine, the restart.
void DonSamples(godot::SceneTree& tree, int f, std::vector<parity::Sample>& out) {
    auto* rm = godot::script_cast<scripts::RoundManager>(tree.get_node("standalone managers/round manager"));
    if (!rm || !rm->playerData) return;
    const std::string id = "f" + std::to_string(f) + "|";
    out.push_back({id + "don",
                   {rm->ui_score->is_visible() ? 1.0 : 0.0, rm->ui_doubleornothing->is_visible() ? 1.0 : 0.0,
                    rm->intbranch_yes->interactionAllowed ? 1.0 : 0.0,
                    rm->intbranch_no->interactionAllowed ? 1.0 : 0.0,
                    rm->btnParent_doubleor->is_visible() ? 1.0 : 0.0,
                    static_cast<double>(rm->playerData->currentBatchIndex), rm->endless ? 1.0 : 0.0,
                    rm->doubling ? 1.0 : 0.0, static_cast<double>(rm->endscore),
                    static_cast<double>(rm->double_or_nothing_rounds_beat), static_cast<double>(rm->mainBatchIndex)},
                   rm->ui_score->get_text() + "|" + rm->anim_doubleor->get_current_animation() + "|" +
                       rm->anim_yes->get_current_animation() + "|" + rm->anim_no->get_current_animation() + "|" +
                       rm->camera->activeSocket});
}

bool RunGrab(const std::string& scenario, const char* runtimeScenePath, parity::Dump& out) {
    using namespace core::json;
    const std::string dir = DirOf(runtimeScenePath);
    assets::AnimationLibrary lib;
    godot::SceneTree tree;
    tree.set_particle_rng(false);   // headless references: a dummy renderer
    // dump_state.gd seed(0)s before instantiating main.tscn, pinning the draws
    // _ready makes (blinker intervals and the like). The tree's constructor
    // randomizes the generator as Godot's does, so the harness must too.
    godot::seed(0);
    if (!godot::load_translations((dir + "/dialogue_en.json").c_str())) return false;
    if (!godot::InputMap::get_singleton().load((dir + "/input_map.json").c_str())) return false;
    scripts::RegisterScripts();
    if (!godot::PackedScene::load("res://instances/shell.tscn", (dir + "/runtime_shell.json").c_str())) return false;
    if (godot::LoadRuntimeScene(runtimeScenePath, tree) != godot::RuntimeSceneError::None ||
        !lib.LoadFromFile((dir + "/animations.json").c_str())) {
        return false;
    }
    // As GameApp::BindScripts: the item scenes, before binding, by the res://
    // path each ItemResource records.
    if (godot::Node* itemManager = tree.get_node("standalone managers/item manager")) {
        for (const std::string& res : scripts::ItemScenePaths(*itemManager)) {
            const std::string file = scripts::ItemSceneExportFile(res);
            if (!godot::PackedScene::load(res, (dir + "/" + file).c_str())) return false;
        }
    }
    godot::BindAnimationLibrary(tree, lib);
    const scripts::BindResult bound = scripts::BindScripts(tree.get_root());
    for (const std::string& e : bound.errors) std::printf("bind: %s\n", e.c_str());
    // ending_*: as the reference starts from an empty private user://
    // (make_menu_references.sh), a fresh save directory, emptied first; the
    // unlock and the lifetime stats are written there.
    const std::string scenarioNameEarly =
        core::json::ReadStringTopLevel(scenario.data(), scenario.data() + scenario.size(), "name");
    const bool endingScenario = scenarioNameEarly.rfind("ending_", 0) == 0;
    game::SaveStore endingStore("parity_saves_" + scenarioNameEarly);
    // "pills": the Double or Nothing unlock (dump_state.gd _begin_intro writes
    // it to the private user:// before main.tscn loads).
    const bool pillsScenario =
        core::json::ReadBoolTopLevel(scenario.data(), scenario.data() + scenario.size(), "pills", false);
    if (pillsScenario && !endingScenario) {
        endingStore.Clear(scripts::SaveFileManager::savePath_stats);
        endingStore.Save(scripts::Unlocker::savepath, game::SaveMap{{"total_amount_selected", int64_t{0}}});
        for (godot::Node* n : tree.nodes_in_tree_order()) {
            if (auto* u = godot::script_cast<scripts::Unlocker>(n)) u->store = &endingStore;
            if (auto* board = godot::script_cast<scripts::Board>(n)) board->store = &endingStore;
        }
    }
    if (endingScenario) {
        endingStore.Clear(scripts::Unlocker::savepath);
        endingStore.Clear(scripts::SaveFileManager::savePath_stats);
        endingStore.Clear(scripts::SaveFileManager::savePath);
        for (godot::Node* n : tree.nodes_in_tree_order()) {
            if (auto* u = godot::script_cast<scripts::Unlocker>(n)) u->store = &endingStore;
            if (auto* em = godot::script_cast<scripts::EndingManager>(n)) em->store = &endingStore;
            if (auto* sv = godot::script_cast<scripts::SaveFileManager>(n)) sv->store = &endingStore;
        }
    }
    tree.ready_scripts();
    auto* rm = godot::script_cast<scripts::RoundManager>(tree.get_node("standalone managers/round manager"));
    auto* cm = godot::script_cast<scripts::CameraManager>(tree.get_node("standalone managers/camera manager"));
    if (!rm || !cm || !rm->playerData) return false;

    // As GameApp does: parsed events reach the tree through push_input.
    godot::Input& input = godot::Input::get_singleton();
    input.reset();
    input.set_dispatch([&tree](const godot::InputEventRef& e) { tree.push_input(e); });

    // The scenario's frames are driven by app::ScenarioRunner, the same driver
    // the desktop exe's --scenario mode renders from.
    app::ScenarioRunner runner;
    if (!runner.Parse(scenario)) return false;
    const std::string& scenarioName = runner.name();
    const bool itemScenario = scenarioName.rfind("items_", 0) == 0 || scenarioName.rfind("use_", 0) == 0 ||
                              scenarioName.rfind("dealer_", 0) == 0;
    const bool introScenario = scenarioName.rfind("intro_", 0) == 0;
    const bool waiverScenario = scenarioName.rfind("waiver_", 0) == 0;
    auto* dealerRm = godot::script_cast<scripts::RoundManager>(tree.get_node("standalone managers/round manager"));
    bool dealerSeen = false;
    // The ceremony can take a while to reach the player's turn; the gate ends it.
    for (int guard = 0; guard < 60 * 240; ++guard) {   // round-2 dealer_ runs are long
        if (runner.BeginFrame(tree, input) == app::ScenarioRunner::Step::Running) {
            if (introScenario) {
                // dump_state.gd _step_intro: the frame the dealer starts back
                // to the table (RoundManager.BeginMainGame + 0.5 s).
                if (!dealerSeen && dealerRm && dealerRm->animator_dealer &&
                    dealerRm->animator_dealer->get_current_animation() == "dealer return to table") {
                    dealerSeen = true;
                    out.events.push_back("f" + std::to_string(runner.frame()) + ":dealer_return");
                }
                IntroSamples(tree, runner.frame(), out.samples);
                if (scenarioName.rfind("intro_crt", 0) == 0) CrtSamples(tree, runner.frame(), out.samples);
            } else if (endingScenario) {
                EndingSamples(tree, runner.frame(), endingStore, out.samples);
                if (scenarioName.rfind("ending_don", 0) == 0) DonSamples(tree, runner.frame(), out.samples);
            } else if (waiverScenario) {
                WaiverSamples(tree, runner.frame(), out.samples);
            } else if (itemScenario) {
                ItemSamples(tree, runner.frame(), out.samples);
            } else {
                GrabSamples(tree, runner.frame(), out.samples);
            }
        }
        if (!runner.EndFrame(input)) return true;
        // Not flushed here: what this frame parsed belongs to the next one.
        tree.process_frame(1.0 / 60.0);
        // ending_*: the unlock's change_scene_to_file ends the run, as the swap
        // ends dump_state.gd's (the frame just processed is frame() - 1).
        if (endingScenario) {
            if (const std::string next = tree.take_scene_change(); !next.empty()) {
                out.events.push_back("f" + std::to_string(runner.frame() - 1) + ":scene:" + next);
                return true;
            }
        }
    }
    return false;
}


// menu_*: scenes/menu.tscn from frame 0. tools/godot/dump_state.gd
// _step_menu / _menu_samples is the twin; change both.
void MenuSamples(godot::SceneTree& tree, int f, const game::SaveStore& store, std::vector<parity::Sample>& out) {
    const std::string id = "f" + std::to_string(f) + "|";
    auto* mm = godot::script_cast<scripts::MenuManager>(tree.get_node("standalone managers/menu manager"));
    auto* om = godot::script_cast<scripts::OptionsManager>(tree.get_node("standalone managers/options manager"));
    if (!mm || !om) return;
    const godot::Control* focus = tree.gui_get_focus_owner();
    out.push_back({id + "focus", {}, focus ? focus->get_path() : std::string()});

    parity::Sample screen{id + "screen", {}, mm->currentScreen};
    for (const godot::Control* scr : mm->screens) screen.v.push_back(scr && scr->is_visible() ? 1.0 : 0.0);
    screen.v.push_back(mm->title && mm->title->is_visible() ? 1.0 : 0.0);
    screen.v.push_back(mm->mouseblocker && mm->mouseblocker->is_visible() ? 1.0 : 0.0);
    out.push_back(std::move(screen));

    parity::Sample anim{id + "anim", {}, ""};
    anim.s = (mm->animator_intro ? mm->animator_intro->get_current_animation() : std::string()) + "|" +
             (mm->anim_creds ? mm->anim_creds->get_current_animation() : std::string());
    anim.v = {!mm->waterfalls.empty() && mm->waterfalls[0] && mm->waterfalls[0]->is_playing() ? 1.0 : 0.0};
    out.push_back(std::move(anim));

    out.push_back({id + "volume",
                   {om->setting_volume, godot::AudioServer::get_singleton().get_bus_volume_db(0),
                    store.Exists(scripts::OptionsManager::savePath) ? 1.0 : 0.0},
                   om->ui_volume ? om->ui_volume->get_text() : std::string()});
    out.push_back({id + "cursor",
                   {mm->cursor && mm->cursor->controller_active ? 1.0 : 0.0,
                    mm->cursor && mm->cursor->cursor_visible ? 1.0 : 0.0},
                   ""});
}

bool RunMenu(const std::string& scenario, const char* runtimeScenePath, parity::Dump& out) {
    const std::string dir = DirOf(runtimeScenePath);
    assets::AnimationLibrary lib;
    godot::SceneTree tree;
    tree.set_particle_rng(false);   // menu.tscn has no particle systems
    godot::seed(0);                 // dump_state.gd _begin_menu, before the load
    if (!godot::load_translations((dir + "/dialogue_en.json").c_str())) return false;
    if (!godot::InputMap::get_singleton().load((dir + "/input_map.json").c_str())) return false;
    godot::AudioServer::get_singleton().reset();
    scripts::RegisterScripts();
    if (godot::LoadRuntimeScene((dir + "/runtime_menu.json").c_str(), tree) != godot::RuntimeSceneError::None ||
        !lib.LoadFromFile((dir + "/animations_menu.json").c_str())) {
        return false;
    }
    // As the reference starts from an empty user:// (make_menu_references.sh):
    // a fresh save directory beside the working directory, emptied first.
    game::SaveStore store("parity_saves_" + out.scenario);
    store.Clear(scripts::OptionsManager::savePath);
    store.Clear(scripts::SaveFileManager::savePath);
    godot::BindAnimationLibrary(tree, lib);
    const scripts::BindResult bound = scripts::BindScripts(tree.get_root());
    for (const std::string& e : bound.errors) std::printf("bind: %s\n", e.c_str());
    for (godot::Node* n : tree.nodes_in_tree_order()) {
        if (auto* save = godot::script_cast<scripts::SaveFileManager>(n)) save->store = &store;
        if (auto* options = godot::script_cast<scripts::OptionsManager>(n)) options->store = &store;
    }
    tree.ready_scripts();

    godot::Input& input = godot::Input::get_singleton();
    input.reset();
    input.set_dispatch([&tree](const godot::InputEventRef& e) { tree.push_input(e); });
    app::ScenarioRunner runner;
    if (!runner.Parse(scenario) || runner.scene() != "menu") return false;
    for (int guard = 0; guard < 60 * 60; ++guard) {
        if (runner.BeginFrame(tree, input) == app::ScenarioRunner::Step::Running) {
            MenuSamples(tree, runner.frame(), store, out.samples);
        }
        if (!runner.EndFrame(input)) return true;
        tree.process_frame(1.0 / 60.0);
        // The frame just processed is Godot's frame() - 1 (samples come
        // before nodes process there); its change_scene_to_file ends the run,
        // as the swap ends dump_state.gd's.
        if (const std::string next = tree.take_scene_change(); !next.empty()) {
            out.events.push_back("f" + std::to_string(runner.frame() - 1) + ":scene:" + next);
            return true;
        }
    }
    return false;
}
// heaven_*: scenes/heaven.tscn from frame 0. tools/godot/dump_state.gd
// _step_heaven / _heaven_samples is the twin; change both.
void HeavenSamples(godot::SceneTree& tree, int f, std::vector<parity::Sample>& out) {
    const std::string id = "f" + std::to_string(f) + "|";
    auto* h = godot::script_cast<scripts::Heaven>(tree.get_node("standalone managers/heaven manager"));
    const auto* cam = tree.get_node_as<godot::Camera3D>("Camera");
    auto* im = godot::script_cast<scripts::InteractionManager>(tree.get_node("standalone managers/interaction manager"));
    if (!h || !cam || !im) return;
    parity::Sample camxf{id + "camxf", {}, ""};
    const core::Mat4& g = cam->get_global_transform();
    const int src[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
    for (int i = 0; i < 12; ++i) camxf.v.push_back(g.m[src[i]]);
    camxf.v.push_back(cam->get_fov());
    out.push_back(std::move(camxf));
    out.push_back({id + "anim", {}, h->animator->get_current_animation()});
    out.push_back({id + "flags",
                   {h->viewblocker->is_visible() ? 1.0 : 0.0, h->btn_door->is_visible() ? 1.0 : 0.0,
                    h->intbranch_heavendoor->interactionAllowed ? 1.0 : 0.0,
                    h->buttonArray[0]->isActive ? 1.0 : 0.0, h->buttonArray[1]->isActive ? 1.0 : 0.0,
                    im->checking ? 1.0 : 0.0, h->cursor->cursor_visible ? 1.0 : 0.0,
                    h->cursor->controller_active ? 1.0 : 0.0},
                   ""});
    const godot::Control* focus = tree.gui_get_focus_owner();
    out.push_back({id + "focus", {}, focus ? focus->get_path() : std::string()});
}

bool RunHeaven(const std::string& scenario, const char* runtimeScenePath, parity::Dump& out) {
    const std::string dir = DirOf(runtimeScenePath);
    assets::AnimationLibrary lib;
    godot::SceneTree tree;
    tree.set_particle_rng(false);   // heaven.tscn draws no particle system
    godot::seed(0);                 // dump_state.gd _begin_heaven, before the load
    if (!godot::load_translations((dir + "/dialogue_en.json").c_str())) return false;
    if (!godot::InputMap::get_singleton().load((dir + "/input_map.json").c_str())) return false;
    scripts::RegisterScripts();
    if (godot::LoadRuntimeScene((dir + "/runtime_heaven.json").c_str(), tree) != godot::RuntimeSceneError::None ||
        !lib.LoadFromFile((dir + "/animations_heaven.json").c_str())) {
        return false;
    }
    godot::BindAnimationLibrary(tree, lib);
    const scripts::BindResult bound = scripts::BindScripts(tree.get_root());
    for (const std::string& e : bound.errors) std::printf("bind: %s\n", e.c_str());
    tree.ready_scripts();

    godot::Input& input = godot::Input::get_singleton();
    input.reset();
    input.set_dispatch([&tree](const godot::InputEventRef& e) { tree.push_input(e); });
    app::ScenarioRunner runner;
    if (!runner.Parse(scenario) || runner.scene() != "heaven") return false;
    for (int guard = 0; guard < 60 * 90; ++guard) {
        if (runner.BeginFrame(tree, input) == app::ScenarioRunner::Step::Running) {
            HeavenSamples(tree, runner.frame(), out.samples);
        }
        if (!runner.EndFrame(input)) return true;
        tree.process_frame(1.0 / 60.0);
        if (std::string next = tree.take_scene_change(); !next.empty()) {
            // death.tscn's TempDeathScreen._ready changes straight on to main
            // (isDeathScreen), so Godot's current_scene after the swap is main.
            if (next == "res://scenes/death.tscn") next = "res://scenes/main.tscn";
            out.events.push_back("f" + std::to_string(runner.frame() - 1) + ":scene:" + next);
            return true;
        }
        if (tree.take_quit()) {
            out.events.push_back("f" + std::to_string(runner.frame() - 1) + ":quit");
            return true;
        }
    }
    return false;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) {
        std::fprintf(stderr,
                     "usage: bsht_parity <scenario.json> <reference.json> <runtime_scene.json> [actual_out.json]\n");
        return 2;
    }
    const std::string scenario = ReadAll(argv[1]);
    const std::string name = core::json::ReadStringTopLevel(
        scenario.data(), scenario.data() + scenario.size(), "name");

    parity::Dump reference;
    if (!parity::ReadDump(argv[2], reference)) {
        std::printf("SKIP %s: no reference at %s (run tools/godot/make_references.sh)\n",
                    name.c_str(), argv[2]);
        return kSkip;
    }

    parity::Dump actual;
    actual.scenario = name;
    if (name == "rng") {
        RunRng(scenario, actual);
    } else if (name == "anim_sample") {
        if (!RunAnimSample(scenario, argv[3], actual)) {
            std::printf("SKIP anim_sample: animations.json missing beside %s\n", argv[3]);
            return kSkip;
        }
    } else if (name == "anim_main_shoot") {
        if (!RunMainShoot(scenario, argv[3], actual)) {
            std::printf("SKIP anim_main_shoot: runtime scene or animations.json missing\n");
            return kSkip;
        }
    } else if (name == "ceremony_b1r1") {
        if (!RunCeremony(scenario, argv[3], actual)) {
            std::printf("SKIP ceremony_b1r1: runtime scenes, animations or dialogue missing, or scripts unbound\n");
            return kSkip;
        }
    } else if (name.rfind("grab_shotgun", 0) == 0 || name.rfind("shoot_", 0) == 0 ||
               name.rfind("items_", 0) == 0 || name.rfind("use_", 0) == 0 || name.rfind("dealer_", 0) == 0 ||
               name.rfind("intro_", 0) == 0 || name.rfind("waiver_", 0) == 0 || name.rfind("ending_", 0) == 0) {
        if (!RunGrab(scenario, argv[3], actual)) {
            std::printf("SKIP %s: runtime scenes, animations, dialogue or input map missing\n", name.c_str());
            return kSkip;
        }
    } else if (name.rfind("heaven_", 0) == 0) {
        if (!RunHeaven(scenario, argv[3], actual)) {
            std::printf("SKIP %s: runtime_heaven.json, animations_heaven.json, dialogue or input map missing\n",
                        name.c_str());
            return kSkip;
        }
    } else if (name.rfind("menu_", 0) == 0) {
        if (!RunMenu(scenario, argv[3], actual)) {
            std::printf("SKIP %s: runtime_menu.json, animations_menu.json, dialogue or input map missing\n",
                        name.c_str());
            return kSkip;
        }
    } else if (name == "anim_play_semantics") {
        actual.events = parity::RunAnimPlaySemanticsProbe(scenario);
    } else if (name == "frame_order") {
        actual.events = parity::RunFrameOrderProbe(core::json::ReadIntTopLevel(
            scenario.data(), scenario.data() + scenario.size(), "frames", 0));
    } else if (name == "load_main") {
        godot::SceneTree tree;
        tree.set_particle_rng(false);   // headless references: a dummy renderer
        godot::seed(0);                 // as dump_state.gd:125, before the load
        const godot::RuntimeSceneError e = godot::LoadRuntimeScene(argv[3], tree);
        if (e == godot::RuntimeSceneError::FileNotFound) {
            std::printf("SKIP load_main: %s missing (run export_runtime_scene.gd)\n", argv[3]);
            return kSkip;
        }
        if (e != godot::RuntimeSceneError::None) {
            std::printf("FAIL load_main: %s\n", godot::RuntimeSceneErrorText(e));
            return 1;
        }
        CaptureNodes(tree, actual);
    } else {
        std::fprintf(stderr, "unknown scenario '%s'\n", name.c_str());
        return 2;
    }

    if (argc == 5 && !parity::WriteDump(argv[4], actual)) {
        std::fprintf(stderr, "cannot write %s\n", argv[4]);
        return 2;
    }
    const std::vector<std::string> diffs = parity::Compare(reference, actual, 25);
    if (diffs.empty()) {
        std::printf("PASS %s\n", name.c_str());
        return 0;
    }
    std::printf("FAIL %s: %lu difference(s) shown (capped at 25)\n", name.c_str(),
                static_cast<unsigned long>(diffs.size()));
    for (const std::string& d : diffs) std::printf("  %s\n", d.c_str());
    return 1;
}
