// The part of Godot 4.1's node model the 93 scripts touch, with Godot's names
// and units. Built from the runtime scene export (godot/runtime_scene_loader.h),
// never by hand in game code. Paths resolve to pointers once, at load.
//
// Rules implemented here, each checked against real Godot by parity_load_main:
//   - rotation is radians, Euler order YXZ; rotation_degrees is a view.
//   - data.parent is the DIRECT parent cast to Node3D. A plain Node in between
//     starts a new chain for global transform AND is_visible_in_tree.
//   - an authored basis is kept exactly until rotation or scale is written, so a
//     sheared basis is not bent by a round trip through TRS.
//   - layers: layer n is bit n-1; default 1. Camera cull_mask default 0xFFFFF.
#pragma once

#include "assets/animation.h"
#include "core/mat4.h"
#include "godot/audio_spatial.h"
#include "godot/script.h"
#include "godot/translation.h"
#include "godot/value_types.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace godot {

class SceneTree;
class AudioPlayback;
class PackedScene;

// The state of one BaseMaterial3D the renderer needs. `transparency` is Godot's
// BaseMaterial3D.transparency enum (0 disabled); -1 marks a material that is not
// a BaseMaterial3D (a ShaderMaterial), which draws as before.
// BaseMaterial3D::BLEND_MODE_ADD: the colour is added over what is behind it.
inline constexpr int kBlendModeAdd = 1;

struct SurfaceMaterial {
    Color albedo;
    Color albedo_at_load;
    int   transparency = 0;
    // BaseMaterial3D.blend_mode: 0 MIX, kBlendModeAdd ADD (the blood splatter
    // plane). Any mode but MIX draws in Godot's transparent pass.
    int   blend_mode = 0;
    // BaseMaterial3D emission * emission_energy, 0 when emission_enabled is
    // off. Godot adds it on top of the lit albedo (the bake does the same for
    // static geometry); `mat_emission red` is how the blinkers glow.
    Color emission{0.0f, 0.0f, 0.0f, 1.0f};
    // BaseMaterial3D.albedo_texture, as the key the baker reads
    // ("inline_textures/<name>.png"); empty when the material sets none. Bake
    // input only: the device draws the texture the bake baked in.
    std::string albedo_texture;

    // Bake inputs: Godot's BaseMaterial3D defaults until the export says
    // otherwise. The emission colour is sRGB (source_color) and must be
    // linearised before the energy scales it, which the premultiplied
    // `emission` above cannot give back.
    float roughness = 1.0f;
    float metallic = 0.0f;
    Color emission_color{0.0f, 0.0f, 0.0f, 1.0f};
    float emission_energy = 0.0f;
    int   shading_mode = 1;   // SHADING_MODE_PER_PIXEL
    int   diffuse_mode = 0;   // DIFFUSE_BURLEY
    // BaseMaterial3D.normal_texture (only when normal_enabled) as a baker key,
    // and normal_scale (material.cpp:1289-1291). Bake input only.
    std::string normal_texture;
    float normal_scale = 1.0f;
};

// Class bits for cast_to. One bit per level of the hierarchy, so a Label3D is
// also a GeometryInstance3D, a VisualInstance3D and a Node3D.
enum : uint32_t {
    kClassNode3D              = 1u << 0,
    kClassVisualInstance3D    = 1u << 1,
    kClassGeometryInstance3D  = 1u << 2,
    kClassMeshInstance3D      = 1u << 3,
    kClassLabel3D             = 1u << 4,
    kClassCamera3D            = 1u << 5,
    kClassOmniLight3D         = 1u << 6,
    kClassAnimationPlayer     = 1u << 7,
    kClassAudioStreamPlayer   = 1u << 8,
    kClassAudioStreamPlayer3D = 1u << 9,
    kClassControl             = 1u << 10,
    kClassLabel               = 1u << 11,
    kClassColorRect           = 1u << 12,
    kClassTextureRect         = 1u << 13,
    kClassButton              = 1u << 14,
    kClassCollisionObject3D   = 1u << 15,
    kClassStaticBody3D        = 1u << 16,
    kClassCollisionShape3D    = 1u << 17,
    kClassGPUParticles3D      = 1u << 18,
    kClassWorldEnvironment    = 1u << 19,
    kClassSkeleton3D          = 1u << 20,
};

class Node {
public:
    static constexpr uint32_t kClassBit = 0u;   // every node is a Node

    explicit Node(std::string name) : name_(std::move(name)) {}
    virtual ~Node() = default;
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    const std::string& get_name() const { return name_; }
    const std::string& get_path() const { return path_; }

    // The Godot class name as exported ("OmniLight3D"), even when the C++ type
    // is a base such as VisualInstance3D.
    const std::string& get_class() const { return class_; }
    void set_class(std::string c) { class_ = std::move(c); }

    Node* get_parent() const { return parent_; }
    int   get_child_count() const { return static_cast<int>(children_.size()); }
    // node.cpp Node::get_child: negative counts from the end; out of range is
    // ERR_FAIL_INDEX_V(..., nullptr), which scripts rely on when they probe.
    Node* get_child(int i) const {
        const int count = static_cast<int>(children_.size());
        if (i < 0) i += count;
        return (i >= 0 && i < count) ? children_[static_cast<std::size_t>(i)] : nullptr;
    }
    SceneTree* get_tree() const { return tree_; }

    // Node::add_child: the tree adopts `child` with its subtree (typically
    // PackedScene::instantiate output) and readies it. Returns the child.
    // PORT: only a parent already in a tree can adopt; the scripts never build
    // detached hierarchies by hand.
    Node* add_child(std::unique_ptr<Node> child);

    // Node::remove_child (node.cpp:1429): detaches the child and its subtree
    // without freeing it. In Godot the caller then owns it; here the tree
    // owns every node, so this only unlinks, and add_child(Node*) below
    // puts it back under any parent. ItemManager.SetupItemClear re-parents
    // items this way.
    void remove_child(Node* child);
    // add_child for a node this tree already owns and remove_child detached.
    Node* add_child(Node* child);

    // Node::queue_free: deleted with its subtree when the frame flushes its
    // delete queue (after the process pass, before timers).
    void queue_free();
    bool is_queued_for_deletion() const { return queued_; }

    virtual void _ready() {}
    // NOTIFICATION_INTERNAL_PROCESS: engine nodes (AnimationPlayer) advance here,
    // immediately before this node's _process.
    virtual void _internal_process(double /*delta*/) {}
    virtual void _process(double /*delta*/) {}

    // Godot's process flags. SceneTree calls _process / _internal_process only
    // while they are on. A node switched on mid-frame waits for the next frame,
    // and one switched off mid-frame is skipped for the rest of it: Godot
    // copies the process list when the frame begins (scene_tree.cpp:916) and
    // skips nodes removed during the pass (nodes_removed_on_group_call).
    void set_process(bool on);
    bool is_processing() const { return process_; }
    void set_process_internal(bool on);
    bool is_processing_internal() const { return process_internal_; }

    // A METHOD track or a script calling a method by name. Reaches the attached
    // script. False: no such method (counted by the caller, never silently
    // ignored).
    virtual bool call(const std::string& method, const std::vector<assets::Value>& args) {
        return script_ && script_->call(method, args);
    }

    // The attached GDScript translation, owned by the node.
    void set_script(std::unique_ptr<Script> script) {
        script_ = std::move(script);
        if (script_) script_->owner_ = this;
    }
    Script* get_script() const { return script_.get(); }

    // The export's "script" (the .gd file's base name, "" for none) and the raw
    // JSON of its "exports" object ("" for none). Generated bindings read them.
    const std::string& get_script_file() const { return script_file_; }
    void set_script_file(std::string f) { script_file_ = std::move(f); }
    const std::string& get_exports_json() const { return exports_json_; }
    void set_exports_json(std::string j) { exports_json_ = std::move(j); }

    // AudioStreamPlayer, 2D and 3D share playback but not a base class in
    // Godot (Node, Node2D, Node3D), so they are reached through this instead of
    // cast_to. nullptr for every other node.
    virtual AudioPlayback* as_audio_playback() { return nullptr; }
    const AudioPlayback* as_audio_playback() const {
        return const_cast<Node*>(this)->as_audio_playback();
    }

    uint32_t class_bits() const { return class_bits_; }

    // Object::connect / emit_signal for the argument-less signals the scripts use
    // (focus_entered, focus_exited, pressed, and script-declared ones such as
    // is_pressed). Callbacks connected during an emit are not called by it.
    void connect(const std::string& signal, std::function<void()> callback) {
        signals_.emplace_back(signal, std::move(callback));
    }
    void emit_signal(const std::string& signal) {
        std::vector<std::function<void()>> matched;
        for (const auto& [name, cb] : signals_) {
            if (name == signal) matched.push_back(cb);
        }
        for (const auto& cb : matched) cb();
    }

protected:
    Node(std::string name, uint32_t bits) : name_(std::move(name)), class_bits_(bits) {}

private:
    friend class SceneTree;
    friend class PackedScene;

    std::string name_;
    std::string path_;
    std::string class_ = "Node";
    Node* parent_ = nullptr;
    std::vector<Node*> children_;
    SceneTree* tree_ = nullptr;
    uint32_t class_bits_ = 0u;
    bool process_ = false;
    bool process_internal_ = false;
    uint64_t removed_in_frame_ = 0;   // SceneTree frame in which processing stopped
    std::unique_ptr<Script> script_;
    std::string script_file_;
    std::string exports_json_;
    // An instantiated root owns its descendants until a tree adopts them.
    std::vector<std::unique_ptr<Node>> detached_;
    bool queued_ = false;
    std::vector<std::pair<std::string, std::function<void()>>> signals_;
};

// `node.script as T`: the node's script if it is a T.
template <class T>
T* script_cast(Node* n) {
    return n ? script_cast<T>(n->get_script()) : nullptr;
}

template <class T>
T* cast_to(Node* n) {
    if (!n) return nullptr;
    if (T::kClassBit != 0u && (n->class_bits() & T::kClassBit) == 0u) return nullptr;
    return static_cast<T*>(n);
}
template <class T>
const T* cast_to(const Node* n) {
    return cast_to<T>(const_cast<Node*>(n));
}

class Node3D : public Node {
public:
    static constexpr uint32_t kClassBit = kClassNode3D;

    explicit Node3D(std::string name) : Node(std::move(name), kClassNode3D) { set_class("Node3D"); }

    Vector3 get_position() const { return {local_.m[12], local_.m[13], local_.m[14]}; }
    void    set_position(const Vector3& p);

    // node_3d.cpp get_rotation / get_scale: after a set_transform the euler
    // angles and scale are re-derived from the basis (get_euler_normalized,
    // YXZ), which can differ from the angles originally set (190 -> -170).
    Vector3 get_rotation() const;
    void    set_rotation(const Vector3& radians);
    Vector3 get_rotation_degrees() const;
    void    set_rotation_degrees(const Vector3& degrees);

    Vector3 get_scale() const;
    void    set_scale(const Vector3& s);

    const core::Mat4& get_transform() const { return local_; }
    // Node3D::set_transform: the matrix as given; euler and scale go dirty.
    void set_transform(const core::Mat4& t);
    // GDScript's `node.transform.origin = v`: get_transform, change the
    // origin, set_transform. Not set_position, which keeps the angles.
    void set_transform_origin(const Vector3& p);
    const core::Mat4& get_global_transform() const;
    // node_3d.cpp:304: local = parent_global.affine_inverse() * xform. The
    // matrix is kept exactly; rotation and scale are re-derived for scripts.
    void set_global_transform(const core::Mat4& xform);
    // node_3d.cpp: the global transform's origin; setting it keeps the basis.
    Vector3 get_global_position() const;
    void set_global_position(const Vector3& p);

    // The loader's entry point: the exact authored matrix plus the rotation and
    // scale Godot reports for it. The matrix is used as-is until a write.
    void set_rest(const core::Mat4& local, const Vector3& rotation, const Vector3& scale);

    bool is_visible() const { return visible_; }
    void set_visible(bool v) { visible_ = v; }
    bool is_visible_in_tree() const;

protected:
    Node3D(std::string name, uint32_t bits) : Node(std::move(name), kClassNode3D | bits) {}

private:
    // SceneTree re-parents (remove_child / add_child), which moves the parent
    // and so invalidates the cached global transform.
    friend class SceneTree;
    const Node3D* parent3d() const { return cast_to<Node3D>(get_parent()); }
    void recompose();
    void mark_global_dirty();

    void update_rotation_and_scale() const;   // node_3d.cpp _update_rotation_and_scale

    core::Mat4 local_;
    mutable Vector3 rotation_{};
    mutable Vector3 scale_{1.0f, 1.0f, 1.0f};
    mutable bool euler_dirty_ = false;         // DIRTY_EULER_ROTATION_AND_SCALE
    bool visible_ = true;
    mutable core::Mat4 global_;
    mutable bool global_dirty_ = true;
};

class VisualInstance3D : public Node3D {
public:
    static constexpr uint32_t kClassBit = kClassVisualInstance3D;
    explicit VisualInstance3D(std::string name)
        : Node3D(std::move(name), kClassVisualInstance3D) { set_class("VisualInstance3D"); }

    uint32_t get_layers() const { return layers_; }
    void     set_layers(uint32_t l) { layers_ = l; }
    bool     get_layer_mask_value(int layer) const { return (layers_ & (1u << (layer - 1))) != 0u; }
    void     set_layer_mask_value(int layer, bool on) {
        const uint32_t bit = 1u << (layer - 1);
        layers_ = on ? (layers_ | bit) : (layers_ & ~bit);
    }

protected:
    VisualInstance3D(std::string name, uint32_t bits)
        : Node3D(std::move(name), kClassVisualInstance3D | bits) {}

private:
    uint32_t layers_ = 1u;
};

class GeometryInstance3D : public VisualInstance3D {
public:
    static constexpr uint32_t kClassBit = kClassGeometryInstance3D;
    explicit GeometryInstance3D(std::string name)
        : VisualInstance3D(std::move(name), kClassGeometryInstance3D) { set_class("GeometryInstance3D"); }

    float get_transparency() const { return transparency_; }
    void  set_transparency(float t) { transparency_ = t; }

    // PORT: per-surface material state lives on the node. The material an
    // animation animates (the muzzle-flash cone) is unique to its node, so
    // nothing can observe the difference; materials a script holds are shared
    // through MaterialLibrary.
    int  surface_count() const { return static_cast<int>(surfaces_.size()); }
    const SurfaceMaterial& get_surface_material(int i) const {
        return surfaces_[static_cast<std::size_t>(i)];
    }
    void add_surface_material(const SurfaceMaterial& m);
    void set_surface_albedo(int i, const Color& c) {
        if (i >= 0 && i < surface_count()) surfaces_[static_cast<std::size_t>(i)].albedo = c;
    }

    // GeometryInstance3D.cast_shadow (SHADOW_CASTING_SETTING_*): 0 off, 1 on,
    // 2 double-sided, 3 shadows only. Bake input: which surfaces occlude.
    int  get_cast_shadow() const { return cast_shadow_; }
    void set_cast_shadow(int c) { cast_shadow_ = c; }
    bool has_material_override() const { return has_override_; }
    const SurfaceMaterial& get_material_override() const { return shared_override_ ? *shared_override_ : override_; }
    // A material_override that is a shared RESOURCE (godot::MaterialLibrary):
    // a script holding the same resource writes what this draws. The library
    // owns it; this only observes.
    void bind_material_override(SurfaceMaterial* shared) {
        shared_override_ = shared;
        has_override_ = shared != nullptr || has_override_;
    }
    void set_material_override(const SurfaceMaterial& m) {
        override_ = m;
        has_override_ = true;
    }
    void set_override_albedo(const Color& c) {
        (shared_override_ ? *shared_override_ : override_).albedo = c;
    }
    // material_override.albedo_texture = tex (CrtIcon), as the baker's key.
    void set_override_albedo_texture(const std::string& key) {
        (shared_override_ ? *shared_override_ : override_).albedo_texture = key;
    }

    // GeometryInstance3D's per-surface override (MeshInstance3D in Godot; kept
    // here beside the other material state). Out of range is ignored, as Godot
    // errors and does nothing.
    void set_surface_override_material(int surface, const SurfaceMaterial& m);
    // The albedo Godot draws surface `surface` with: material_override, else
    // the surface override, else the mesh's own material.
    Color get_active_surface_albedo(int surface) const;
    // That material's emission, which Godot adds on top of the lit albedo.
    // BlinkerBranch swaps in a material whose red is emission, not albedo.
    Color get_active_surface_emission(int surface) const;
    // That material's albedo_texture key ("" when none). EndlessMode and
    // CrtIcon swap only the texture; the renderer keys variants on it.
    const std::string& get_active_surface_texture(int surface) const;
    // That albedo relative to the surface's load-time albedo, for the
    // renderer's tint (the bake carries the load-time colour). Identity when
    // the surface does not exist.
    Color get_surface_tint_since_load(int surface) const;

    // The alpha Godot composites this geometry with: the active material's
    // albedo alpha when that material is alpha-transparent, times
    // (1 - transparency). 0 means nothing reaches the screen.
    float get_draw_alpha() const;

    // PORT: the active material's BaseMaterial3D.transparency (0 disabled, -1
    // not a BaseMaterial3D; 0 with no material), for the renderer's pass choice.
    int get_active_material_transparency() const;
    int get_active_material_blend_mode() const;

protected:
    GeometryInstance3D(std::string name, uint32_t bits)
        : VisualInstance3D(std::move(name), kClassGeometryInstance3D | bits) {}

private:
    float transparency_ = 0.0f;
    std::vector<SurfaceMaterial> surfaces_;
    std::vector<SurfaceMaterial> surface_overrides_;   // parallel to surfaces_
    std::vector<uint8_t> has_surface_override_;
    SurfaceMaterial override_;
    SurfaceMaterial* shared_override_ = nullptr;   // non-owning; MaterialLibrary owns
    int  cast_shadow_ = 1;
    bool has_override_ = false;
};

class MeshInstance3D : public GeometryInstance3D {
public:
    static constexpr uint32_t kClassBit = kClassMeshInstance3D;
    explicit MeshInstance3D(std::string name)
        : GeometryInstance3D(std::move(name), kClassMeshInstance3D) { set_class("MeshInstance3D"); }

    // The mesh's resource_name: the glTF mesh the room bake resolves.
    const std::string& get_mesh_name() const { return mesh_name_; }
    void set_mesh_name(std::string n) { mesh_name_ = std::move(n); }

    // Set on nodes cloned by PackedScene::instantiate: which template props
    // draw this node ("instances/shell.tscn:shell parent"). Empty otherwise.
    const std::string& get_template_key() const { return template_key_; }
    void set_template_key(std::string k) { template_key_ = std::move(k); }

    // The Skin: its skeleton (a NodePath from this node) and binds, each a
    // bone name and its bind pose (the inverse bind matrix).
    struct SkinBind {
        std::string name;
        float pose[16];
    };
    bool has_skin() const { return !skin_binds_.empty(); }
    const std::string& get_skeleton_path() const { return skeleton_path_; }
    const std::vector<SkinBind>& get_skin_binds() const { return skin_binds_; }
    void set_skin(std::string skeletonPath, std::vector<SkinBind> binds) {
        skeleton_path_ = std::move(skeletonPath);
        skin_binds_ = std::move(binds);
    }

private:
    std::string mesh_name_;
    std::string template_key_;
    std::string skeleton_path_;
    std::vector<SkinBind> skin_binds_;
};

class Label3D : public GeometryInstance3D {
public:
    static constexpr uint32_t kClassBit = kClassLabel3D;
    explicit Label3D(std::string name)
        : GeometryInstance3D(std::move(name), kClassLabel3D) { set_class("Label3D"); }

    const std::string& get_text() const { return text_; }
    void set_text(std::string t) { text_ = std::move(t); }
    // What the Label3D draws: set_text stores xl_text = atr(text)
    // (label_3d.cpp), re-translated when the locale changes.
    const std::string& get_xl_text() const {
        const unsigned generation = translation_generation();
        if (xl_generation_ != generation || xl_source_ != text_) {
            xl_text_ = tr(text_);
            xl_source_ = text_;
            xl_generation_ = generation;
        }
        return xl_text_;
    }

    // label_3d.cpp:860. Every glyph vertex takes it as its colour
    // (_generate_glyph_surfaces, label_3d.cpp:418); the outline takes
    // outline_modulate instead (label_3d.cpp:604-608).
    Color get_modulate() const { return modulate_; }
    void  set_modulate(const Color& c) { modulate_ = c; }

private:
    std::string text_;
    mutable std::string xl_text_;
    mutable std::string xl_source_;
    mutable unsigned xl_generation_ = ~0u;
    Color modulate_{1.0f, 1.0f, 1.0f, 1.0f};
};

class Camera3D : public Node3D {
public:
    static constexpr uint32_t kClassBit = kClassCamera3D;
    explicit Camera3D(std::string name) : Node3D(std::move(name), kClassCamera3D) { set_class("Camera3D"); }

    bool     is_current() const { return current_; }
    void     set_current(bool c) { current_ = c; }
    float    get_fov() const { return fov_; }
    void     set_fov(float f) { fov_ = f; }
    uint32_t get_cull_mask() const { return cull_mask_; }
    void     set_cull_mask(uint32_t m) { cull_mask_ = m; }
    // camera_3d.h defaults; project_ray_origin / project_position read them.
    float    get_near() const { return near_; }
    void     set_near(float n) { near_ = n; }
    float    get_far() const { return far_; }
    void     set_far(float f) { far_ = f; }

    // camera_3d.h: h_offset / v_offset, in metres. CameraShaker writes them
    // every frame while a shake is running. They are not a projection skew:
    // camera_3d.cpp:166-171 moves the camera transform's origin along its own
    // up and right columns and leaves the node's global transform untouched,
    // so the node still reports where it is parked.
    float h_offset = 0.0f;
    float v_offset = 0.0f;

    // Camera3D::get_camera_transform (camera_3d.cpp:166-171). Everything that
    // renders or picks through this camera uses THIS, not get_global_transform:
    // the renderer and project_ray_origin / project_position alike.
    core::Mat4 get_camera_transform() const {
        core::Mat4 tr = get_global_transform();
        Orthonormalize(tr);
        // tr.origin += basis column 1 * v_offset, then column 0 * h_offset.
        for (int i = 0; i < 3; ++i) {
            tr.m[12 + i] += tr.m[4 + i] * v_offset;
            tr.m[12 + i] += tr.m[0 + i] * h_offset;
        }
        return tr;
    }

    // The 3D audio listener (audio_stream_player_3d.cpp:417): the camera node's
    // global transform, orthonormalised; h_offset and v_offset do not move it.
    core::Mat4 get_listener_transform() const {
        core::Mat4 tr = get_global_transform();
        Orthonormalize(tr);
        return tr;
    }

private:
    // Basis::orthonormalize (basis.cpp), Gram-Schmidt over the three columns.
    static void Orthonormalize(core::Mat4& t) {
        float* x = &t.m[0];
        float* y = &t.m[4];
        float* z = &t.m[8];
        core::Normalize3(x);
        const float xy = x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
        for (int i = 0; i < 3; ++i) y[i] -= x[i] * xy;
        core::Normalize3(y);
        const float xz = x[0] * z[0] + x[1] * z[1] + x[2] * z[2];
        const float yz = y[0] * z[0] + y[1] * z[1] + y[2] * z[2];
        for (int i = 0; i < 3; ++i) z[i] -= x[i] * xz + y[i] * yz;
        core::Normalize3(z);
    }

    bool current_ = false;
    float fov_ = 75.0f;
    uint32_t cull_mask_ = 0xFFFFFu;
    float near_ = 0.05f;
    float far_ = 4000.0f;
};

// WorldEnvironment and the Environment it holds, reduced to the adjustments
// the port applies: animator_pp animates the brightness, the muzzle flash the
// contrast. Godot's defaults until the export says otherwise.
class WorldEnvironment : public Node {
public:
    static constexpr uint32_t kClassBit = kClassWorldEnvironment;
    explicit WorldEnvironment(std::string name) : Node(std::move(name), kClassWorldEnvironment) {
        set_class("WorldEnvironment");
    }
    float get_adjustment_brightness() const { return brightness_; }
    void  set_adjustment_brightness(float v) { brightness_ = v; }
    float get_adjustment_contrast() const { return contrast_; }
    void  set_adjustment_contrast(float v) { contrast_ = v; }
    float get_adjustment_saturation() const { return saturation_; }
    void  set_adjustment_saturation(float v) { saturation_ = v; }
    // Environment.ambient_light_*: source 2 is COLOR. The colour is sRGB as
    // authored; the renderer linearises it (render_scene_data_rd.cpp:146).
    int   get_ambient_light_source() const { return ambientSource_; }
    void  set_ambient_light_source(int s) { ambientSource_ = s; }
    Color get_ambient_light_color() const { return ambientColor_; }
    void  set_ambient_light_color(const Color& c) { ambientColor_ = c; }
    float get_ambient_light_energy() const { return ambientEnergy_; }
    void  set_ambient_light_energy(float e) { ambientEnergy_ = e; }
    // Environment.fog_* and the background behind it. Colours are sRGB as
    // authored; the renderer linearises them (fog: render_scene_data_rd).
    bool  get_fog_enabled() const { return fogEnabled_; }
    void  set_fog_enabled(bool on) { fogEnabled_ = on; }
    Color get_fog_light_color() const { return fogColor_; }
    void  set_fog_light_color(const Color& c) { fogColor_ = c; }
    float get_fog_light_energy() const { return fogEnergy_; }
    void  set_fog_light_energy(float e) { fogEnergy_ = e; }
    float get_fog_density() const { return fogDensity_; }
    void  set_fog_density(float d) { fogDensity_ = d; }
    float get_fog_sky_affect() const { return fogSkyAffect_; }
    void  set_fog_sky_affect(float a) { fogSkyAffect_ = a; }
    int   get_background_mode() const { return bgMode_; }
    void  set_background_mode(int m) { bgMode_ = m; }
    Color get_background_color() const { return bgColor_; }
    void  set_background_color(const Color& c) { bgColor_ = c; }
    float get_background_energy_multiplier() const { return bgEnergy_; }
    void  set_background_energy_multiplier(float e) { bgEnergy_ = e; }

private:
    bool  fogEnabled_ = false;
    Color fogColor_{0.518f, 0.553f, 0.608f, 1.0f};   // Environment's defaults
    float fogEnergy_ = 1.0f;
    float fogDensity_ = 0.01f;
    float fogSkyAffect_ = 1.0f;
    int   bgMode_ = 0;
    Color bgColor_{0.0f, 0.0f, 0.0f, 1.0f};
    float bgEnergy_ = 1.0f;
    int   ambientSource_ = 0;
    Color ambientColor_{0.0f, 0.0f, 0.0f, 1.0f};
    float ambientEnergy_ = 1.0f;
    float brightness_ = 1.0f;
    float contrast_ = 1.0f;
    float saturation_ = 1.0f;
};

// Recorded for the lighting track (L); animation tracks drive it now.
class OmniLight3D : public VisualInstance3D {
public:
    static constexpr uint32_t kClassBit = kClassOmniLight3D;
    explicit OmniLight3D(std::string name)
        : VisualInstance3D(std::move(name), kClassOmniLight3D) { set_class("OmniLight3D"); }

    float get_light_energy() const { return light_energy_; }
    void  set_light_energy(float e) { light_energy_ = e; }
    float get_omni_range() const { return omni_range_; }
    void  set_omni_range(float r) { omni_range_ = r; }

    // Bake inputs. The colour is sRGB as authored; the bake linearises it
    // (light_storage.cpp:802).
    Color get_light_color() const { return light_color_; }
    void  set_light_color(Color c) { light_color_ = c; }
    float get_omni_attenuation() const { return omni_attenuation_; }
    void  set_omni_attenuation(float a) { omni_attenuation_ = a; }
    bool  has_shadow() const { return shadow_enabled_; }
    void  set_shadow(bool on) { shadow_enabled_ = on; }
    float get_light_size() const { return light_size_; }
    void  set_light_size(float s) { light_size_ = s; }
    float get_light_specular() const { return light_specular_; }
    void  set_light_specular(float s) { light_specular_ = s; }
    bool  is_negative() const { return light_negative_; }
    void  set_negative(bool on) { light_negative_ = on; }
    // light_cull_mask: the render layers this light reaches.
    uint32_t get_cull_mask() const { return cull_mask_; }
    void     set_cull_mask(uint32_t m) { cull_mask_ = m; }

private:
    float light_energy_ = 1.0f;
    float omni_range_ = 5.0f;
    // Godot's defaults (light_3d.cpp).
    Color light_color_{1.0f, 1.0f, 1.0f, 1.0f};
    float omni_attenuation_ = 1.0f;
    bool  shadow_enabled_ = false;
    float light_size_ = 0.0f;
    float light_specular_ = 0.5f;
    bool  light_negative_ = false;
    uint32_t cull_mask_ = 0xFFFFFFFFu;
};

// Playback state shared by AudioStreamPlayer, AudioStreamPlayer2D and
// AudioStreamPlayer3D (their spatial mix is godot/audio_spatial.h).
// One play of a player's stream, on Godot's terms: where in the stream it
// starts (play(from), an audio key's start_offset), how far before the
// stream's end an audio key stops it (end_offset; 0 = plays out), and
// whether the AudioServer's start ramp applies (not for a sound an animation
// adds to a player whose polyphonic playback is already running).
struct AudioPlayRequest {
    float from = 0.0f;
    float end_offset = 0.0f;
    bool ramp_in = true;
    // An animation's sound inside the player's AudioStreamPolyphonic: it plays
    // at pitch 1 whatever the player's pitch_scale (play_stream's default; the
    // polyphonic mix drops the player's rate).
    bool polyphonic = false;
};
// How long this play will sound, in seconds (0: unknown; a loop reports
// StreamRouter::kNeverEnds), and the mixer's voice for it (-1: none, for a
// stream voice, a missing cue or no free voice).
struct AudioPlayResult {
    float length = 0.0f;
    int voice = -1;
};
// How Godot mixes the player (godot/audio_spatial.h): an AudioStreamPlayer at
// its volume, a 2D player against the screen centre, a 3D one from the camera.
enum class AudioSpace : uint8_t { Plain, Flat2D, Positional3D };

class AudioPlayback {
public:
    explicit AudioPlayback(Node* owner, AudioSpace space = AudioSpace::Plain)
        : owner_(owner), space_(space) {}
    virtual ~AudioPlayback() = default;

    Node* owner() const { return owner_; }

    // The stream's file name, e.g. "temp gunshot_live.wav", as the bank keys it.
    const std::string& get_stream() const { return stream_; }
    // AudioStreamPlayer::set_stream begins with stop() (its playbacks fade).
    void  set_stream(std::string s) {
        stop();
        stream_ = std::move(s);
    }
    float get_volume_db() const { return volume_db_; }
    void  set_volume_db(float v) { volume_db_ = v; }
    float get_pitch_scale() const { return pitch_scale_; }
    void  set_pitch_scale(float p) { pitch_scale_ = p; }
    bool  is_playing() const { return playing_; }
    // Godot's `playing = false` is a stop(): counted as one.
    void  set_playing(bool p) {
        if (!p && playing_) ++stops_;
        playing_ = p;
    }
    // How many times it was stopped (stop(), `playing = false`): a playback
    // that ran out leaves it alone, so a stream router can tell the two apart.
    uint32_t get_stop_count() const { return stops_; }
    AudioSpace space() const { return space_; }
    void  set_space(AudioSpace s) { space_ = s; }
    const Spatial2D& spatial_2d() const { return spatial2d_; }
    void  set_spatial_2d(const Spatial2D& s) { spatial2d_ = s; }
    const Spatial3D& spatial_3d() const { return spatial3d_; }
    void  set_spatial_3d(const Spatial3D& s) { spatial3d_ = s; }

    // AudioStreamPlayer::play: past max_polyphony live playbacks the oldest is
    // cut (faded); the new one starts with the AudioServer's ramp.
    void play(float from = 0.0f);
    // An animation AUDIO key (animation_player.cpp:879-912): the key's stream
    // joins the player's AudioStreamPolyphonic. The first one starts the
    // player (ramped); later ones are added at full volume and never cut.
    void play_from_animation(std::string stream, float start_offset, float end_offset);
    // Every live playback fades out.
    void stop();
    // Counts a playing stream down to its end, and forgets ended playbacks.
    void tick(double delta);
    // The AudioServer bus it plays on ("Master", or "music" with the table's
    // low-pass).
    const std::string& get_bus() const { return bus_; }
    void  set_bus(std::string b) { bus_ = std::move(b); }
    int  get_max_polyphony() const { return max_polyphony_; }
    void set_max_polyphony(int n) { max_polyphony_ = n > 0 ? n : 1; }

private:
    Node* owner_;
    std::string stream_;
    float volume_db_ = 0.0f;
    float pitch_scale_ = 1.0f;
    bool  playing_ = false;
    uint32_t stops_ = 0;   // get_stop_count
    float remaining_ = 0.0f;
    AudioSpace space_;
    Spatial2D spatial2d_;
    Spatial3D spatial3d_;
    int max_polyphony_ = 1;   // AudioStreamPlayer's default
    std::string bus_ = "Master";
    // The playbacks this player has started that may still sound: a mixer
    // voice, the seconds it has left, and whether it is part of the
    // polyphonic playback an animation started (cut together, as one).
    struct Playback {
        int voice = -1;
        float remaining = 0.0f;
        bool polyphonic = false;
    };
    std::vector<Playback> playbacks_;
    bool polyphonic_ = false;   // an animation's polyphonic playback is running
    // What the live voices were last told: a change is sent on the next tick.
    StereoGain sentGain_;
    float sentPitch_ = 1.0f;
    void SendChanges();
    void Start(const AudioPlayRequest& request, bool polyphonic);
    void RefreshRemaining();
    void Cut(std::size_t index);
};

// The left/right gain Godot's AudioServer gives `a` now: its volume_db through
// its space, a 3D player heard from the tree's current camera (none: silent).
StereoGain OutputGain(const AudioPlayback& a);

// AudioStreamPlayer (a Node) and AudioStreamPlayer2D (a Node2D, which the port
// does not model). get_class() keeps the real name.
class AudioStreamPlayer : public Node, public AudioPlayback {
public:
    static constexpr uint32_t kClassBit = kClassAudioStreamPlayer;
    explicit AudioStreamPlayer(std::string name)
        : Node(std::move(name), kClassAudioStreamPlayer), AudioPlayback(this) {
        set_class("AudioStreamPlayer");
    }
    AudioPlayback* as_audio_playback() override { return this; }
    void _internal_process(double delta) override { tick(delta); }
};

// AudioStreamPlayer3D is a Node3D in Godot: it has a transform and sits in the
// spatial chain, which parity_load_main checks.
class AudioStreamPlayer3D : public Node3D, public AudioPlayback {
public:
    static constexpr uint32_t kClassBit = kClassAudioStreamPlayer3D;
    explicit AudioStreamPlayer3D(std::string name)
        : Node3D(std::move(name), kClassAudioStreamPlayer3D), AudioPlayback(this, AudioSpace::Positional3D) {
        set_class("AudioStreamPlayer3D");
    }
    AudioPlayback* as_audio_playback() override { return this; }
    void _internal_process(double delta) override { tick(delta); }
};

}  // namespace godot
