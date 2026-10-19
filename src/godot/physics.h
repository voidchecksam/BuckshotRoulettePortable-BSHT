// Godot 4.1.4's 3D physics, reduced to what the scripts query: a ray against
// static collision shapes (MouseRaycast.get_selection).
//
//   godot_space_3d.cpp  GodotPhysicsDirectSpaceState3D::intersect_ray
//   godot_shape_3d.cpp  GodotBoxShape3D, GodotConcavePolygonShape3D (_setup,
//                       _cull_segment, intersect_segment), GodotFaceShape3D
//   aabb.cpp            AABB::intersects_segment / merge_with
//
// PORT, as main.tscn uses it: every CollisionObject3D is a StaticBody3D and
// every shape a BoxShape3D (20) or ConcavePolygonShape3D (51); a scene shape
// of any other type fails the load rather than going missing. The broadphase
// is not transcribed: Godot's cull_segment only prunes, and every surviving
// shape is tested with the same nearest-hit rule below.
#pragma once

#include "godot/node.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace godot {

struct AABB {
    Vector3 position;
    Vector3 size;

    bool intersects_segment(const Vector3& from, const Vector3& to, Vector3* r_clip = nullptr,
                            Vector3* r_normal = nullptr) const;
    void merge_with(const AABB& other);
    Vector3 get_center() const {
        return {position.x + size.x * 0.5f, position.y + size.y * 0.5f, position.z + size.z * 0.5f};
    }
    int get_longest_axis_index() const;
};

struct Shape3D {
    virtual ~Shape3D() = default;
    // In shape space.
    virtual bool intersect_segment(const Vector3& begin, const Vector3& end, Vector3& r_result, Vector3& r_normal,
                                   bool hit_back_faces) const = 0;
    virtual bool intersect_point(const Vector3& point) const = 0;
};

struct BoxShape3D : Shape3D {
    Vector3 half_extents;
    bool intersect_segment(const Vector3& begin, const Vector3& end, Vector3& r_result, Vector3& r_normal,
                           bool hit_back_faces) const override;
    bool intersect_point(const Vector3& point) const override;
};

// godot_shape_3d.cpp GodotCylinderShape3D: centred on the origin, the height
// along local Y. The beer can (instances/item_beer can.tscn) is the only one
// in the game; main.tscn uses boxes and concave meshes only.
struct CylinderShape3D : Shape3D {
    float radius = 0.5f;   // CylinderShape3D defaults (cylinder_shape_3d.cpp)
    float height = 2.0f;
    bool intersect_segment(const Vector3& begin, const Vector3& end, Vector3& r_result, Vector3& r_normal,
                           bool hit_back_faces) const override;
    bool intersect_point(const Vector3& point) const override;
};

struct ConcavePolygonShape3D : Shape3D {
    struct Face {
        Vector3 normal;
        int indices[3] = {0, 0, 0};
    };
    struct BVH {
        AABB aabb;
        int left = -1;
        int right = -1;
        int face_index = -1;
    };

    // ConcavePolygonShape3D.set_faces: three vertices per triangle.
    void set_faces(const std::vector<Vector3>& triangle_soup);

    bool intersect_segment(const Vector3& begin, const Vector3& end, Vector3& r_result, Vector3& r_normal,
                           bool hit_back_faces) const override;
    bool intersect_point(const Vector3& point) const override;

    bool backface_collision = false;
    std::vector<Face> faces;
    std::vector<Vector3> vertices;
    std::vector<BVH> bvh;
};

class CollisionObject3D : public Node3D {
public:
    static constexpr uint32_t kClassBit = kClassCollisionObject3D;
    uint32_t collision_layer = 1;
    uint32_t collision_mask = 1;

    // CollisionObject3D.set_collision_layer_value / _mask_value: layer_number
    // is 1-based (1..32), as in Godot.
    void set_collision_layer_value(int layer_number, bool value) { SetBit(collision_layer, layer_number, value); }
    bool get_collision_layer_value(int layer_number) const { return GetBit(collision_layer, layer_number); }
    void set_collision_mask_value(int layer_number, bool value) { SetBit(collision_mask, layer_number, value); }
    bool get_collision_mask_value(int layer_number) const { return GetBit(collision_mask, layer_number); }

protected:
    static void SetBit(uint32_t& bits, int layer_number, bool value) {
        if (layer_number < 1 || layer_number > 32) return;
        const uint32_t bit = 1u << (layer_number - 1);
        bits = value ? (bits | bit) : (bits & ~bit);
    }
    static bool GetBit(uint32_t bits, int layer_number) {
        return layer_number >= 1 && layer_number <= 32 && (bits & (1u << (layer_number - 1))) != 0;
    }
    CollisionObject3D(std::string name, uint32_t bits) : Node3D(std::move(name), kClassCollisionObject3D | bits) {}
};

class StaticBody3D : public CollisionObject3D {
public:
    static constexpr uint32_t kClassBit = kClassStaticBody3D;
    explicit StaticBody3D(std::string name) : CollisionObject3D(std::move(name), kClassStaticBody3D) {
        set_class("StaticBody3D");
    }
};

class CollisionShape3D : public Node3D {
public:
    static constexpr uint32_t kClassBit = kClassCollisionShape3D;
    explicit CollisionShape3D(std::string name) : Node3D(std::move(name), kClassCollisionShape3D) {
        set_class("CollisionShape3D");
    }
    bool disabled = false;
    std::shared_ptr<const Shape3D> shape;
};

// PhysicsRayQueryParameters3D, with Godot's defaults.
struct RayQuery {
    Vector3 from;
    Vector3 to;
    uint32_t collision_mask = 0xFFFFFFFFu;
    bool collide_with_bodies = true;
    bool collide_with_areas = false;
    bool hit_from_inside = false;
    bool hit_back_faces = true;
};

// intersect_ray's Dictionary: collider, position, normal, shape.
struct RayResult {
    Node* collider = nullptr;
    Vector3 position;
    Vector3 normal;
    int shape = -1;
};

class PhysicsDirectSpaceState3D {
public:
    // One enabled shape as the physics server holds it after a sync.
    struct Entry {
        CollisionObject3D* body = nullptr;
        const Shape3D* shape = nullptr;
        core::Mat4 xform;       // body global * shape local
        core::Mat4 inverse;
        uint32_t layer = 0;
        int shape_index = 0;
    };

    void clear() { entries_.clear(); }
    void add(const Entry& e) { entries_.push_back(e); }
    std::size_t size() const { return entries_.size(); }

    bool intersect_ray(const RayQuery& query, RayResult& result) const;

private:
    std::vector<Entry> entries_;
};

}  // namespace godot
