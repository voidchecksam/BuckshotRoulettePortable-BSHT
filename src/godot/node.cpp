#include "godot/node.h"

#include "godot/audio_server.h"
#include "core/mat4.h"

#include "core/trs.h"
#include "godot/basis.h"
#include "godot/scene_tree.h"

namespace godot {
namespace {

constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

}  // namespace

void Node3D::set_position(const Vector3& p) {
    local_.m[12] = p.x;
    local_.m[13] = p.y;
    local_.m[14] = p.z;
    mark_global_dirty();
}

void Node3D::recompose() {
    const float pos[3] = {local_.m[12], local_.m[13], local_.m[14]};
    const float rot[3] = {rotation_.x, rotation_.y, rotation_.z};
    const float scl[3] = {scale_.x, scale_.y, scale_.z};
    local_ = core::ComposeTRSRad(pos, rot, scl);
    mark_global_dirty();
}

void Node3D::update_rotation_and_scale() const {
    scale_ = basis::GetScale(local_);
    rotation_ = basis::GetEulerNormalizedYXZ(local_);
    euler_dirty_ = false;
}

Vector3 Node3D::get_rotation() const {
    if (euler_dirty_) update_rotation_and_scale();
    return rotation_;
}

Vector3 Node3D::get_scale() const {
    if (euler_dirty_) update_rotation_and_scale();
    return scale_;
}

// node_3d.cpp set_rotation: when both are dirty only the scale is refreshed,
// since the rotation is about to be replaced.
void Node3D::set_rotation(const Vector3& radians) {
    if (euler_dirty_) {
        scale_ = basis::GetScale(local_);
        euler_dirty_ = false;
    }
    rotation_ = radians;
    recompose();
}

void Node3D::set_transform(const core::Mat4& t) {
    local_ = t;
    euler_dirty_ = true;
    mark_global_dirty();
}

void Node3D::set_transform_origin(const Vector3& p) {
    core::Mat4 t = local_;
    t.m[12] = p.x;
    t.m[13] = p.y;
    t.m[14] = p.z;
    set_transform(t);
}

Vector3 Node3D::get_rotation_degrees() const {
    const Vector3 r = get_rotation();
    return {r.x * kRadToDeg, r.y * kRadToDeg, r.z * kRadToDeg};
}

void Node3D::set_rotation_degrees(const Vector3& d) {
    set_rotation({d.x * kDegToRad, d.y * kDegToRad, d.z * kDegToRad});
}

// node_3d.cpp set_scale: a dirty rotation is refreshed first, since the
// scale is about to be replaced.
void Node3D::set_scale(const Vector3& s) {
    if (euler_dirty_) {
        rotation_ = basis::GetEulerNormalizedYXZ(local_);
        euler_dirty_ = false;
    }
    scale_ = s;
    recompose();
}

void Node3D::set_rest(const core::Mat4& local, const Vector3& rotation, const Vector3& scale) {
    local_ = local;
    rotation_ = rotation;
    scale_ = scale;
    euler_dirty_ = false;
    mark_global_dirty();
}

// node_3d.cpp:304 Node3D::set_global_transform (4.1.4-stable). The port has no
// top_level, so the parent term applies whenever the parent is a Node3D --
// the same chain get_global_transform walks.
void Node3D::set_global_transform(const core::Mat4& xform) {
    const Node3D* p = parent3d();
    set_transform(p ? core::Mul(core::InverseAffine(p->get_global_transform()), xform) : xform);
}

Vector3 Node3D::get_global_position() const {
    const core::Mat4& g = get_global_transform();
    return {g.m[12], g.m[13], g.m[14]};
}

void Node3D::set_global_position(const Vector3& p) {
    core::Mat4 g = get_global_transform();
    g.m[12] = p.x;
    g.m[13] = p.y;
    g.m[14] = p.z;
    set_global_transform(g);
}

const core::Mat4& Node3D::get_global_transform() const {
    if (global_dirty_) {
        const Node3D* p = parent3d();
        global_ = p ? core::Mul(p->get_global_transform(), local_) : local_;
        global_dirty_ = false;
    }
    return global_;
}

// Invariant: if a node is dirty, every Node3D descendant is dirty too. A child's
// global is only ever computed by first computing its parent's, so a clean child
// implies a clean parent. That is what lets an already-dirty node end the walk.
void Node3D::mark_global_dirty() {
    if (global_dirty_) return;
    global_dirty_ = true;
    for (int i = 0; i < get_child_count(); ++i) {
        if (Node3D* c = cast_to<Node3D>(get_child(i))) c->mark_global_dirty();
    }
}

bool Node3D::is_visible_in_tree() const {
    for (const Node3D* n = this; n; n = n->parent3d()) {
        if (!n->visible_) return false;
    }
    return true;
}

namespace {

// Godot's active material for surface 0: material_override wins.
const SurfaceMaterial* ActiveMaterial(const GeometryInstance3D& g) {
    if (g.has_material_override()) return &g.get_material_override();
    return g.surface_count() > 0 ? &g.get_surface_material(0) : nullptr;
}

}  // namespace

float GeometryInstance3D::get_draw_alpha() const {
    const SurfaceMaterial* m = ActiveMaterial(*this);
    // Godot ignores albedo alpha on a material whose transparency is disabled.
    const float materialAlpha = (m && m->transparency > 0) ? m->albedo.a : 1.0f;
    return materialAlpha * (1.0f - transparency_);
}

int GeometryInstance3D::get_active_material_transparency() const {
    const SurfaceMaterial* m = ActiveMaterial(*this);
    return m ? m->transparency : 0;
}

int GeometryInstance3D::get_active_material_blend_mode() const {
    const SurfaceMaterial* m = ActiveMaterial(*this);
    return m ? m->blend_mode : 0;
}

void GeometryInstance3D::add_surface_material(const SurfaceMaterial& m) {
    surfaces_.push_back(m);
    surface_overrides_.emplace_back();
    has_surface_override_.push_back(0u);
}

void GeometryInstance3D::set_surface_override_material(int surface, const SurfaceMaterial& m) {
    if (surface < 0 || surface >= surface_count()) return;
    const std::size_t i = static_cast<std::size_t>(surface);
    surface_overrides_[i] = m;
    has_surface_override_[i] = 1u;
}

Color GeometryInstance3D::get_active_surface_albedo(int surface) const {
    if (has_override_) return get_material_override().albedo;
    if (surface < 0 || surface >= surface_count()) return {};
    const std::size_t i = static_cast<std::size_t>(surface);
    return has_surface_override_[i] ? surface_overrides_[i].albedo : surfaces_[i].albedo;
}

Color GeometryInstance3D::get_active_surface_emission(int surface) const {
    if (has_override_) return get_material_override().emission;
    if (surface < 0 || surface >= surface_count()) return {0.0f, 0.0f, 0.0f, 1.0f};
    const std::size_t i = static_cast<std::size_t>(surface);
    return has_surface_override_[i] ? surface_overrides_[i].emission : surfaces_[i].emission;
}

const std::string& GeometryInstance3D::get_active_surface_texture(int surface) const {
    static const std::string kNone;
    if (has_override_) return get_material_override().albedo_texture;
    if (surface < 0 || surface >= surface_count()) return kNone;
    const std::size_t i = static_cast<std::size_t>(surface);
    return has_surface_override_[i] ? surface_overrides_[i].albedo_texture : surfaces_[i].albedo_texture;
}

Color GeometryInstance3D::get_surface_tint_since_load(int surface) const {
    if (surface < 0 || surface >= surface_count()) return {};
    const Color now = get_active_surface_albedo(surface);
    const Color& was = surfaces_[static_cast<std::size_t>(surface)].albedo_at_load;
    const auto ratio = [](float n, float w) { return w > 0.0f ? n / w : n; };
    return {ratio(now.r, was.r), ratio(now.g, was.g), ratio(now.b, was.b), 1.0f};
}

Node* Node::add_child(std::unique_ptr<Node> child) {
    if (!child || !tree_) return nullptr;
    return tree_->adopt(this, std::move(child));
}

void Node::remove_child(Node* child) {
    if (!child || child->parent_ != this || !tree_) return;   // ERR_FAIL_COND in Godot
    tree_->detach(child);
}

Node* Node::add_child(Node* child) {
    if (!child || child->parent_ || !tree_) return nullptr;
    return tree_->reattach(this, child);
}

void Node::queue_free() {
    if (!tree_ || queued_) return;
    queued_ = true;
    tree_->queue_delete(this);
}

// Node::set_process / set_process_internal (node.cpp:857-877): a node that was
// processing is removed from the process list first, which during a frame skips
// it for the rest of that frame even if it is added straight back.
void Node::set_process(bool on) {
    if (process_ == on) return;
    if (tree_ && (process_ || process_internal_)) tree_->note_process_removed(*this);
    process_ = on;
}

void Node::set_process_internal(bool on) {
    if (process_internal_ == on) return;
    if (tree_ && (process_ || process_internal_)) tree_->note_process_removed(*this);
    process_internal_ = on;
}

// PORT: Godot plays through an AudioStreamPlayback on the server; the port
// hands the stream to the platform mixer through the tree's sink and only
// tracks how long it plays, so is_playing() reads true for that long.
// Hands the play to the tree's sink and keeps the playback it started (with
// its mixer voice, if it has one).
void AudioPlayback::Start(const AudioPlayRequest& request, bool polyphonic) {
    playing_ = true;
    float length = 0.0f;
    int voice = -1;
    if (SceneTree* tree = owner_->get_tree()) {
        ++tree->animation_stats().audio_plays;
        if (tree->audio_sink()) {
            const AudioPlayResult r = tree->audio_sink()(*this, request);
            length = r.length;
            voice = r.voice;
            // The sink started the voice at the player's gain and pitch as they are now.
            sentGain_ = OutputGain(*this);
            sentPitch_ = pitch_scale_;
        }
    }
    playbacks_.push_back(Playback{voice, length, polyphonic});
    RefreshRemaining();
    owner_->set_process_internal(true);
}

// is_playing lasts as long as the longest playback still sounding.
void AudioPlayback::RefreshRemaining() {
    remaining_ = 0.0f;
    for (const Playback& p : playbacks_) remaining_ = std::max(remaining_, p.remaining);
    if (playbacks_.empty()) playing_ = false;
}

// Stops playback `index` (the whole polyphonic playback if it is part of it).
void AudioPlayback::Cut(std::size_t index) {
    const bool polyphonic = playbacks_[index].polyphonic;
    const SceneTree* tree = owner_->get_tree();
    const AudioStopSink* stopSink = tree && tree->audio_stop_sink() ? &tree->audio_stop_sink() : nullptr;
    for (std::size_t i = playbacks_.size(); i-- > 0;) {
        if (i != index && !(polyphonic && playbacks_[i].polyphonic)) continue;
        if (stopSink && playbacks_[i].voice >= 0) (*stopSink)(playbacks_[i].voice);
        playbacks_.erase(playbacks_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    if (polyphonic) polyphonic_ = false;
}

// audio_stream_player.cpp play(): the oldest playback goes once max_polyphony
// are live; the animation's polyphonic playback counts as one.
void AudioPlayback::play(float from) {
    for (;;) {
        int live = 0;
        bool sawPolyphonic = false;
        for (const Playback& p : playbacks_) {
            if (!p.polyphonic) ++live;
            else if (!sawPolyphonic) { ++live; sawPolyphonic = true; }
        }
        if (live < max_polyphony_ || playbacks_.empty()) break;
        Cut(0);
    }
    polyphonic_ = false;
    AudioPlayRequest r;
    r.from = from;
    Start(r, false);
}

void AudioPlayback::play_from_animation(std::string stream, float start_offset, float end_offset) {
    // The polyphonic playback lives until stop(), a direct play() or a stream
    // change (AudioStreamPlaybackPolyphonic::mix keeps returning a full buffer
    // while active), so a key joins it even after its last sound ended.
    const bool joining = polyphonic_;
    if (!joining) {
        for (std::size_t i = playbacks_.size(); i-- > 0;) Cut(i);
    }
    stream_ = std::move(stream);   // the key's stream, without set_stream's stop()
    polyphonic_ = true;
    AudioPlayRequest r;
    r.from = start_offset;
    r.end_offset = end_offset;
    r.ramp_in = !joining;
    r.polyphonic = true;
    Start(r, true);
}

void AudioPlayback::stop() {
    ++stops_;
    while (!playbacks_.empty()) Cut(playbacks_.size() - 1);
    polyphonic_ = false;
    playing_ = false;
    remaining_ = 0.0f;
    owner_->set_process_internal(false);
}

// AudioStreamPlayer pushes a pitch change to its playbacks at once, and the
// AudioServer reads its volume (and a 3D player's pan) every mix buffer: a
// change reaches every live voice, once. An animation's sound keeps pitch 1.
void AudioPlayback::SendChanges() {
    if (playbacks_.empty()) return;
    const StereoGain g = OutputGain(*this);
    if (g.l == sentGain_.l && g.r == sentGain_.r && g.shelf == sentGain_.shelf && pitch_scale_ == sentPitch_) return;
    sentGain_ = g;
    sentPitch_ = pitch_scale_;
    const SceneTree* tree = owner_->get_tree();
    if (!tree || !tree->audio_update_sink()) return;
    for (const Playback& p : playbacks_) {
        if (p.voice >= 0) tree->audio_update_sink()(p.voice, g, p.polyphonic ? 1.0f : pitch_scale_);
    }
}

void AudioPlayback::tick(double delta) {
    SendChanges();
    // Playbacks run on the audio clock: the frame's raw delta, not the scaled one.
    const SceneTree* tree = owner_->get_tree();
    const float real = static_cast<float>(tree ? tree->get_raw_delta_time() : delta);
    for (std::size_t i = playbacks_.size(); i-- > 0;) {
        playbacks_[i].remaining -= real;
        if (playbacks_[i].remaining <= 0.0f) playbacks_.erase(playbacks_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    RefreshRemaining();
    if (playbacks_.empty()) owner_->set_process_internal(false);
}

StereoGain OutputGain(const AudioPlayback& a) {
    if (!AudioServer::get_singleton().is_bus_heard(a.get_bus())) return {0.0f, 0.0f};
    switch (a.space()) {
    case AudioSpace::Plain:
        return GainPlain(a.get_volume_db());
    case AudioSpace::Flat2D:
        // project.godot: 960x540, stretch mode "viewport": the visible rect
        // whatever the window, so the listener never moves.
        return Gain2D(a.spatial_2d(), a.get_volume_db(), {960.0f, 540.0f});
    case AudioSpace::Positional3D:
        break;
    }
    const auto* self = cast_to<Node3D>(a.owner());
    const SceneTree* tree = a.owner()->get_tree();
    const Camera3D* camera = tree ? tree->get_current_camera() : nullptr;
    if (!self || !camera) return {0.0f, 0.0f};
    const core::Mat4& world = self->get_global_transform();
    const float origin[3] = {world.Tx(), world.Ty(), world.Tz()};
    float local[3];
    core::TransformPoint(core::InverseAffine(camera->get_listener_transform()), origin, local);
    return Gain3D(a.spatial_3d(), a.get_volume_db(), {local[0], local[1], local[2]});
}

}  // namespace godot
