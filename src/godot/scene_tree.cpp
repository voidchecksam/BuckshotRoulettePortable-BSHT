#include "godot/scene_tree.h"

#include "godot/control.h"
#include "godot/camera_projection.h"
#include "godot/input.h"
#include "godot/particles.h"
#include "core/frustum.h"

#include <algorithm>
#include <unordered_set>

namespace godot {

Node* SceneTree::add_node(std::unique_ptr<Node> node, Node* parent) {
    Node* n = node.get();
    n->tree_ = this;
    n->parent_ = parent;
    if (!parent) {
        n->path_ = ".";
        root_ = n;
    } else {
        parent->children_.push_back(n);
        n->path_ = parent == root_ ? n->name_ : parent->path_ + "/" + n->name_;
    }
    by_path_[n->path_] = n;
    order_.push_back(n);
    owned_.push_back(std::move(node));
    return n;
}

namespace {

void CollectPreorder(Node* n, std::vector<Node*>& out) {
    out.push_back(n);
    for (int i = 0; i < n->get_child_count(); ++i) CollectPreorder(n->get_child(i), out);
}

bool IsAncestor(const Node* ancestor, const Node* n) {
    for (const Node* p = n ? n->get_parent() : nullptr; p; p = p->get_parent()) {
        if (p == ancestor) return true;
    }
    return false;
}

}  // namespace

Node* SceneTree::adopt(Node* parent, std::unique_ptr<Node> child) {
    Node* c = child.get();
    for (const Node* sibling : parent->children_) {
        if (sibling->name_ == c->name_) {
            ++generated_names_;
            c->name_ = "@" + c->class_ + "@" + std::to_string(generated_names_);
            break;
        }
    }
    c->parent_ = parent;
    parent->children_.push_back(c);

    std::vector<Node*> subtree;
    CollectPreorder(c, subtree);
    for (Node* n : subtree) {
        n->tree_ = this;
        n->path_ = n->parent_ == root_ ? n->name_ : n->parent_->path_ + "/" + n->name_;
        by_path_[n->path_] = n;
        if (auto* mesh = cast_to<MeshInstance3D>(n); mesh && !mesh->get_template_key().empty()) {
            template_instances_.push_back(mesh);
        }
    }

    std::vector<std::unique_ptr<Node>> descendants = std::move(c->detached_);
    owned_.push_back(std::move(child));
    for (auto& d : descendants) owned_.push_back(std::move(d));

    if (processing_) {
        pending_order_.emplace_back(parent, std::move(subtree));
    } else {
        insert_in_order(parent, subtree);
    }

    seq::ScopedCurrent current(&scheduler_);
    ready_recursive(c);
    return c;
}

void SceneTree::detach(Node* child) {
    Node* parent = child->parent_;
    auto& siblings = parent->children_;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
    child->parent_ = nullptr;

    std::vector<Node*> subtree;
    CollectPreorder(child, subtree);
    for (Node* n : subtree) {
        const auto it = by_path_.find(n->path_);
        if (it != by_path_.end() && it->second == n) by_path_.erase(it);
        // Leaving the tree drops focus and hover, as freeing does.
        if (static_cast<Node*>(focus_owner_) == n) focus_owner_ = nullptr;
        if (static_cast<Node*>(mouse_focus_) == n) mouse_focus_ = nullptr;
        if (static_cast<Node*>(mouse_over_) == n) mouse_over_ = nullptr;
    }
    if (processing_) {
        pending_detach_.insert(pending_detach_.end(), subtree.begin(), subtree.end());
    } else {
        remove_from_order(subtree);
    }
}

Node* SceneTree::reattach(Node* parent, Node* child) {
    for (const Node* sibling : parent->children_) {
        if (sibling->name_ == child->name_) {
            ++generated_names_;
            child->name_ = "@" + child->class_ + "@" + std::to_string(generated_names_);
            break;
        }
    }
    child->parent_ = parent;
    parent->children_.push_back(child);

    std::vector<Node*> subtree;
    CollectPreorder(child, subtree);
    for (Node* n : subtree) {
        n->path_ = n->parent_ == root_ ? n->name_ : n->parent_->path_ + "/" + n->name_;
        by_path_[n->path_] = n;
    }
    // A new parent means a new global transform for the whole subtree.
    if (auto* n3d = cast_to<Node3D>(child)) n3d->mark_global_dirty();

    if (processing_) {
        pending_order_.emplace_back(parent, std::move(subtree));
    } else {
        insert_in_order(parent, subtree);
    }
    return child;
}

void SceneTree::remove_from_order(const std::vector<Node*>& subtree) {
    const std::unordered_set<Node*> leaving(subtree.begin(), subtree.end());
    order_.erase(std::remove_if(order_.begin(), order_.end(),
                                [&leaving](const Node* n) { return leaving.count(const_cast<Node*>(n)) != 0; }),
                 order_.end());
}

void SceneTree::insert_in_order(Node* parent, const std::vector<Node*>& subtree) {
    auto at = std::find(order_.begin(), order_.end(), parent);
    if (at == order_.end()) {
        order_.insert(order_.end(), subtree.begin(), subtree.end());
        return;
    }
    ++at;
    while (at != order_.end() && IsAncestor(parent, *at)) ++at;
    order_.insert(at, subtree.begin(), subtree.end());
}

void SceneTree::flush_delete_queue() {
    if (delete_queue_.empty()) return;
    std::vector<Node*> queue;
    queue.swap(delete_queue_);

    std::unordered_set<Node*> doomed;
    for (Node* n : queue) {
        if (doomed.count(n)) continue;   // inside a subtree already freed
        if (Node* p = n->parent_) {
            auto& siblings = p->children_;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), n), siblings.end());
        }
        std::vector<Node*> subtree;
        CollectPreorder(n, subtree);
        doomed.insert(subtree.begin(), subtree.end());
    }
    ++free_generation_;
    for (Node* n : doomed) {
        // NOTIFICATION_PREDELETE: a freed player stops, its sounds fade.
        if (AudioPlayback* a = n->as_audio_playback()) a->stop();
        scheduler_.CancelOwner(n);   // its awaiting methods never resume
        const auto it = by_path_.find(n->path_);
        if (it != by_path_.end() && it->second == n) by_path_.erase(it);
        if (static_cast<Node*>(focus_owner_) == n) focus_owner_ = nullptr;
        if (static_cast<Node*>(mouse_focus_) == n) mouse_focus_ = nullptr;
        if (static_cast<Node*>(mouse_over_) == n) mouse_over_ = nullptr;
    }
    const auto gone = [&doomed](const Node* n) { return doomed.count(const_cast<Node*>(n)) != 0; };
    order_.erase(std::remove_if(order_.begin(), order_.end(), gone), order_.end());
    template_instances_.erase(
        std::remove_if(template_instances_.begin(), template_instances_.end(),
                       [&](const MeshInstance3D* m) { return gone(m); }),
        template_instances_.end());
    owned_.erase(std::remove_if(owned_.begin(), owned_.end(),
                                [&](const std::unique_ptr<Node>& u) { return gone(u.get()); }),
                 owned_.end());
}

Node* SceneTree::get_node(std::string_view path) const {
    const auto it = by_path_.find(std::string(path));
    return it == by_path_.end() ? nullptr : it->second;
}

// Cached for the frame (and dropped when nodes are freed): every live 3D
// sound asks for it each frame, and the search walks the whole tree.
Camera3D* SceneTree::get_current_camera() const {
    if (camera_frame_ == frame_ && camera_generation_ == free_generation_ && camera_cache_ &&
        camera_cache_->is_current()) {
        return camera_cache_;
    }
    camera_cache_ = nullptr;
    for (Node* n : order_) {
        if (Camera3D* c = cast_to<Camera3D>(n); c && c->is_current()) {
            camera_cache_ = c;
            break;
        }
    }
    camera_frame_ = frame_;
    camera_generation_ = free_generation_;
    return camera_cache_;
}

// Node::_notification(NOTIFICATION_READY), node.cpp:179 at 4.1.4-stable: a
// script defining _process has processing switched on before its _ready runs.
void SceneTree::ready_script(Node* n) {
    Script* s = n->script_.get();
    if (!s || s->readied_) return;
    s->readied_ = true;
    if (s->has_process()) n->set_process(true);
    s->_ready();
}

// _propagate_ready (node.cpp:217): children first.
void SceneTree::ready_recursive(Node* n) {
    for (Node* c : n->children_) ready_recursive(c);
    n->_ready();
    ready_script(n);
}

void SceneTree::ready_scripts_recursive(Node* n) {
    for (Node* c : n->children_) ready_scripts_recursive(c);
    ready_script(n);
}

void SceneTree::ready() {
    if (!root_) return;
    seq::ScopedCurrent current(&scheduler_);
    ready_recursive(root_);
}

void SceneTree::ready_scripts() {
    if (!root_) return;
    seq::ScopedCurrent current(&scheduler_);
    ready_scripts_recursive(root_);
}

void SceneTree::spawn(seq::Task&& task, const Node* owner) {
    seq::ScopedCurrent current(&scheduler_);
    scheduler_.Spawn(std::move(task), owner);
}

void SceneTree::call_deferred(Node* node, const std::string& method,
                              const std::vector<assets::Value>& args) {
    if (node) deferred_.push_back({node, method, args});
}

// MessageQueue::flush. Calls queued while flushing run in the same flush.
void SceneTree::flush_deferred() {
    for (std::size_t i = 0; i < deferred_.size(); ++i) {
        // Moved out before the call: a callee that queues more may reallocate.
        DeferredCall call = std::move(deferred_[i]);
        if (call.node->call(call.method, call.args)) {
            ++stats_.methods_called;
            continue;
        }
        ++stats_.methods_unhandled;
        auto& names = stats_.unhandled_methods;
        if (std::find(names.begin(), names.end(), call.method) == names.end()) {
            names.push_back(call.method);
        }
    }
    deferred_.clear();
}

void SceneTree::physics_sync() {
    space_.clear();
    for (Node* n : order_) {
        auto* cs = cast_to<CollisionShape3D>(n);
        if (!cs || cs->disabled || !cs->shape) continue;
        auto* body = cast_to<CollisionObject3D>(cs->get_parent());
        if (!body) continue;
        int index = 0;
        for (int i = 0; i < body->get_child_count(); ++i) {
            if (body->get_child(i) == cs) break;
            if (cast_to<CollisionShape3D>(body->get_child(i))) ++index;
        }
        PhysicsDirectSpaceState3D::Entry e;
        e.body = body;
        e.shape = cs->shape.get();
        e.xform = cs->get_global_transform();
        e.inverse = core::InverseAffine(e.xform);
        e.layer = body->collision_layer;
        e.shape_index = index;
        space_.add(e);
    }
}

// scene_tree.cpp SceneTree::physics_process (4.1.4-stable): the step Main::
// iteration runs before process. It ends with MessageQueue::flush() and
// _flush_delete_queue(), so what a timer freed last frame is gone before this
// frame's process, and before anything sampled at MainLoop::process.
void SceneTree::physics_frame() {
    if (physics_ran_) return;
    physics_ran_ = true;
    seq::ScopedCurrent current(&scheduler_);
    flush_deferred();
    flush_delete_queue();
    // After the frees: a freed body leaves Godot's physics server at once, so
    // this frame's queries must never see it (the space holds raw pointers).
    physics_sync();
}

void SceneTree::process_frame(double raw_delta) {
    physics_frame();   // no-op when the caller already ran it this frame
    physics_ran_ = false;
    ++frame_;
    // Sampled once: a script changing time_scale inside _process affects the
    // NEXT frame, as main.cpp computes process_step * time_scale beforehand.
    const double delta = raw_delta * time_scale_;
    process_delta_ = delta;
    raw_delta_ = raw_delta;
    raw_time_ += raw_delta;
    seq::ScopedCurrent current(&scheduler_);
    {
        core::ProfScope prof(profiler_, core::ProfSection::Deferred);
        flush_deferred();   // scene_tree.cpp:504
    }

    // scene_tree.cpp:916: the list of processing nodes is copied first.
    const std::size_t count = order_.size();
    in_list_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        in_list_[i] = (order_[i]->process_ || order_[i]->process_internal_) ? 1u : 0u;
    }
    processing_ = true;
    {   // the node pass alone; the steps after it time themselves
        core::ProfScope profTree(profiler_, core::ProfSection::Tree);
        for (std::size_t i = 0; i < count; ++i) {
            Node* n = order_[i];
            if (!in_list_[i] || n->removed_in_frame_ == frame_) continue;
            if (n->process_internal_) {
                // An AnimationPlayer's whole frame (tracks, METHOD keys queued,
                // audio) is timed on its own; the rest of the pass is scripts.
                core::ProfScope profAnim(profiler_ && (n->class_bits() & kClassAnimationPlayer) ? profiler_ : nullptr,
                                         core::ProfSection::Animation);
                n->_internal_process(delta);
            }
            if (n->process_) {
                n->_process(delta);
                // The script's _process is the node's _process in Godot.
                if (n->script_) n->script_->_process(delta);
            }
        }
    }
    processing_ = false;
    // Leaving first, then joining: a node moved during the pass lands once,
    // under its new parent.
    remove_from_order(pending_detach_);
    pending_detach_.clear();
    for (auto& pending : pending_order_) insert_in_order(pending.first, pending.second);
    pending_order_.clear();

    {
        core::ProfScope prof(profiler_, core::ProfSection::Deferred);
        flush_deferred();   // scene_tree.cpp:511
        flush_delete_queue();   // scene_tree.cpp:516, before process_timers
    }
    {
        // The scheduler's own scale stays 1: the tree already scaled the delta.
        core::ProfScope prof(profiler_, core::ProfSection::Coroutines);
        scheduler_.Tick(delta);
    }
    Input::get_singleton().end_process_frame();   // main.cpp:3467
    core::ProfScope prof(profiler_, core::ProfSection::ParticleSim);
    render_step(delta);
}

// rendering_server_default.cpp:83-91: update_particles() first (the systems
// the previous frame's cull queued), then the viewports, whose cull
// (renderer_scene_cull.cpp:2780-2796) queues every visible system that is not
// `!emitting && inactive` for the next frame. The update list is each node's
// `dirty` flag walked in tree order: a freed node simply leaves it, and the
// order cannot matter, since each draw only advances the stream.
void SceneTree::render_step(double delta) {
    if (!particle_rng_) return;
    const bool zeroTimeScale = time_scale_ <= 0.0;   // particles_storage.cpp:1473
    for (Node* n : order_) {
        if (auto* p = cast_to<GPUParticles3D>(n); p && p->dirty) p->update(delta, zeroTimeScale);
    }
    const Camera3D* cam = get_current_camera();
    if (!cam) return;
    const float aspect = kViewportSize.x / kViewportSize.y;
    const core::Mat4 viewProj = core::Mul(core::Perspective(cam->get_fov(), aspect, cam->get_near(), cam->get_far()),
                                          core::InverseAffine(cam->get_camera_transform()));
    core::Frustum frustum;
    frustum.FromViewProj(viewProj);
    for (Node* n : order_) {
        auto* p = cast_to<GPUParticles3D>(n);
        if (!p || p->dirty || !p->wants_process()) continue;
        if (!p->is_visible_in_tree() || (p->get_layers() & cam->get_cull_mask()) == 0u) continue;
        const Vector3& pos = p->get_visibility_aabb_position();
        const Vector3& size = p->get_visibility_aabb_size();
        const float lo[3] = {pos.x, pos.y, pos.z};
        const float hi[3] = {pos.x + size.x, pos.y + size.y, pos.z + size.z};
        float wlo[3];
        float whi[3];
        core::TransformAabb(p->get_global_transform(), lo, hi, wlo, whi);
        if (frustum.IntersectsAabb(wlo, whi)) p->dirty = true;   // particles_request_process
    }
}

namespace {

// scene_tree.cpp _call_input_pause: the group is copied first, then walked from
// the last node in tree order to the first, stopping once the input is handled.
template <class HasFn, class CallFn>
void CallInputGroup(const std::vector<Node*>& order, const SceneTree& tree, HasFn has, CallFn call) {
    std::vector<Node*> group;
    for (Node* n : order) {
        if (n->get_script() && has(*n->get_script())) group.push_back(n);
    }
    for (auto it = group.rbegin(); it != group.rend(); ++it) {
        if (tree.is_input_handled()) break;
        call(*(*it)->get_script());
    }
}

}  // namespace

void SceneTree::push_input(const InputEventRef& event) {
    if (!event) return;
    input_handled_ = false;
    seq::ScopedCurrent current(&scheduler_);
    CallInputGroup(order_, *this, [](const Script& s) { return s.has_input(); },
                   [&](Script& s) { s._input(event); });
    if (!input_handled_) gui_input_event(event);
    CallInputGroup(order_, *this, [](const Script& s) { return s.has_unhandled_input(); },
                   [&](Script& s) { s._unhandled_input(event); });
    // viewport.cpp _push_unhandled_input_internal: then the key group, for an
    // InputEventKey nobody handled.
    if (!input_handled_ && event->type == InputEventType::Key) {
        CallInputGroup(order_, *this, [](const Script& s) { return s.has_unhandled_key_input(); },
                       [&](Script& s) { s._unhandled_key_input(event); });
    }
}

}  // namespace godot
