// Transcribed from Godot 4.1.4-stable; see physics.h for the file list.
#include "godot/physics.h"

#include <cmath>
#include <memory>

namespace godot {
namespace {

constexpr float kCmpEpsilon = 0.00001f;   // math_defs.h CMP_EPSILON

Vector3 Add(const Vector3& a, const Vector3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vector3 Sub(const Vector3& a, const Vector3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vector3 Scale(const Vector3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
float Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vector3 Normalized(const Vector3& v) {
    const float l = std::sqrt(Dot(v, v));
    return l == 0.0f ? Vector3{} : Scale(v, 1.0f / l);
}
float Axis(const Vector3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }
void SetAxis(Vector3& v, int i, float value) { (i == 0 ? v.x : (i == 1 ? v.y : v.z)) = value; }

// geometry_3d.h:166 Geometry3D::segment_intersects_cylinder, transcribed line
// for line from 4.1.4-stable. The segment is clipped against the cylinder
// seen side-on as a 2D box: its half-width is the chord at the segment's
// distance from the axis, its half-height the cylinder's.
bool SegmentIntersectsCylinder(const Vector3& from, const Vector3& to, float height, float radius,
                               Vector3* r_res, Vector3* r_norm, int cylinderAxis) {
    const Vector3 rel = Sub(to, from);
    const float rel_l = std::sqrt(Dot(rel, rel));
    if (rel_l < kCmpEpsilon) return false;   // both points are the same
    if (cylinderAxis < 0 || cylinderAxis > 2) return false;
    Vector3 cylinder_axis;
    SetAxis(cylinder_axis, cylinderAxis, 1.0f);

    // First check if they are parallel.
    const Vector3 normal = Scale(rel, 1.0f / rel_l);
    const Vector3 crs = Cross(normal, cylinder_axis);
    const float crs_l = std::sqrt(Dot(crs, crs));
    Vector3 axis_dir;
    if (crs_l < kCmpEpsilon) {
        Vector3 side_axis;
        SetAxis(side_axis, (cylinderAxis + 1) % 3, 1.0f);   // any side axis OK
        axis_dir = side_axis;
    } else {
        axis_dir = Scale(crs, 1.0f / crs_l);
    }

    const float dist = Dot(axis_dir, from);
    if (dist >= radius) return false;   // too far away

    // Convert to 2D.
    const float w2 = radius * radius - dist * dist;
    if (w2 < kCmpEpsilon) return false;   // avoid numerical error
    const float size[2] = {std::sqrt(w2), height * 0.5f};
    const Vector3 side_dir = Normalized(Cross(axis_dir, cylinder_axis));
    const float from2D[2] = {Dot(side_dir, from), Axis(from, cylinderAxis)};
    const float to2D[2] = {Dot(side_dir, to), Axis(to, cylinderAxis)};

    float min = 0.0f;
    float max = 1.0f;
    int axis = -1;
    for (int i = 0; i < 2; ++i) {
        const float seg_from = from2D[i];
        const float seg_to = to2D[i];
        const float box_begin = -size[i];
        const float box_end = size[i];
        float cmin = 0.0f;
        float cmax = 1.0f;
        if (seg_from < seg_to) {
            if (seg_from > box_end || seg_to < box_begin) return false;
            const float length = seg_to - seg_from;
            cmin = (seg_from < box_begin) ? ((box_begin - seg_from) / length) : 0.0f;
            cmax = (seg_to > box_end) ? ((box_end - seg_from) / length) : 1.0f;
        } else {
            if (seg_to > box_end || seg_from < box_begin) return false;
            const float length = seg_to - seg_from;
            cmin = (seg_from > box_end) ? (box_end - seg_from) / length : 0.0f;
            cmax = (seg_to < box_begin) ? (box_begin - seg_from) / length : 1.0f;
        }
        if (cmin > min) {
            min = cmin;
            axis = i;
        }
        if (cmax < max) max = cmax;
        if (max < min) return false;
    }

    // Convert to 3D again.
    const Vector3 result = Add(from, Scale(rel, min));
    Vector3 res_normal = result;
    if (axis == 0) {
        SetAxis(res_normal, cylinderAxis, 0.0f);
    } else {
        int axis_side = (cylinderAxis + 1) % 3;
        SetAxis(res_normal, axis_side, 0.0f);
        axis_side = (axis_side + 1) % 3;
        SetAxis(res_normal, axis_side, 0.0f);
    }
    res_normal = Normalized(res_normal);
    if (r_res) *r_res = result;
    if (r_norm) *r_norm = res_normal;
    return true;
}

Vector3 XformPoint(const core::Mat4& m, const Vector3& v) {
    return {m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12],
            m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13],
            m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14]};
}

// Basis::xform_inv: the transpose applied (dot with each column).
Vector3 BasisXformInv(const core::Mat4& m, const Vector3& v) {
    return {m.m[0] * v.x + m.m[1] * v.y + m.m[2] * v.z,
            m.m[4] * v.x + m.m[5] * v.y + m.m[6] * v.z,
            m.m[8] * v.x + m.m[9] * v.y + m.m[10] * v.z};
}

// plane.cpp Plane(p1, p2, p3, CLOCKWISE).normal
Vector3 ClockwiseNormal(const Vector3& p1, const Vector3& p2, const Vector3& p3) {
    return Normalized(Cross(Sub(p1, p3), Sub(p1, p2)));
}

// geometry_3d.h Geometry3D::segment_intersects_triangle
bool SegmentIntersectsTriangle(const Vector3& from, const Vector3& to, const Vector3& v0, const Vector3& v1,
                               const Vector3& v2, Vector3* r_res) {
    const Vector3 rel = Sub(to, from);
    const Vector3 e1 = Sub(v1, v0);
    const Vector3 e2 = Sub(v2, v0);
    const Vector3 h = Cross(rel, e2);
    const float a = Dot(e1, h);
    if (std::fabs(a) < kCmpEpsilon) return false;   // parallel
    const float f = 1.0f / a;
    const Vector3 s = Sub(from, v0);
    const float u = f * Dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;
    const Vector3 q = Cross(s, e1);
    const float v = f * Dot(rel, q);
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = f * Dot(e2, q);
    if (t > kCmpEpsilon && t <= 1.0f) {
        if (r_res) *r_res = Add(from, Scale(rel, t));
        return true;
    }
    return false;
}

// aabb.h AABB::expand_to
void ExpandTo(AABB& box, const Vector3& p) {
    Vector3 begin = box.position;
    Vector3 end = Add(box.position, box.size);
    for (int i = 0; i < 3; ++i) {
        if (Axis(p, i) < Axis(begin, i)) SetAxis(begin, i, Axis(p, i));
        if (Axis(p, i) > Axis(end, i)) SetAxis(end, i, Axis(p, i));
    }
    box.position = begin;
    box.size = Sub(end, begin);
}

// face3.cpp Face3::get_aabb
AABB FaceAabb(const Vector3& a, const Vector3& b, const Vector3& c) {
    AABB box{a, {}};
    ExpandTo(box, b);
    ExpandTo(box, c);
    return box;
}

// --- the BVH build: godot_shape_3d.cpp _volume_build_bvh + _fill_bvh --------------

struct Element {
    AABB aabb;
    Vector3 center;
    int face_index = 0;
};

// sort_array.h SortArray<_Volume_BVH_Element, _Volume_BVH_Compare{X,Y,Z}>::sort,
// transcribed (introsort + heap fallback + insertion sort; Validate is off in
// release builds). The element order it leaves decides the BVH shape, so it is
// copied rather than replaced with std::sort.
class ElementSort {
public:
    explicit ElementSort(int axis) : axis_(axis) {}
    void sort(Element* a, int len) const { sort_range(0, len, a); }

private:
    static constexpr int kIntrosortThreshold = 16;
    int axis_;

    bool compare(const Element& a, const Element& b) const { return Axis(a.center, axis_) < Axis(b.center, axis_); }

    const Element& median_of_3(const Element& a, const Element& b, const Element& c) const {
        if (compare(a, b)) {
            if (compare(b, c)) return b;
            if (compare(a, c)) return c;
            return a;
        }
        if (compare(a, c)) return a;
        if (compare(b, c)) return c;
        return b;
    }
    static int bitlog(int n) {
        int k = 0;
        for (; n != 1; n >>= 1) ++k;
        return k;
    }
    void push_heap(int first, int hole, int top, Element value, Element* a) const {
        int parent = (hole - 1) / 2;
        while (hole > top && compare(a[first + parent], value)) {
            a[first + hole] = a[first + parent];
            hole = parent;
            parent = (hole - 1) / 2;
        }
        a[first + hole] = value;
    }
    void adjust_heap(int first, int hole, int len, Element value, Element* a) const {
        const int top = hole;
        int second_child = 2 * hole + 2;
        while (second_child < len) {
            if (compare(a[first + second_child], a[first + (second_child - 1)])) second_child--;
            a[first + hole] = a[first + second_child];
            hole = second_child;
            second_child = 2 * (second_child + 1);
        }
        if (second_child == len) {
            a[first + hole] = a[first + (second_child - 1)];
            hole = second_child - 1;
        }
        push_heap(first, hole, top, value, a);
    }
    void pop_heap(int first, int last, int result, Element value, Element* a) const {
        a[result] = a[first];
        adjust_heap(first, 0, last - first, value, a);
    }
    void pop_heap(int first, int last, Element* a) const { pop_heap(first, last - 1, last - 1, a[last - 1], a); }
    void sort_heap(int first, int last, Element* a) const {
        while (last - first > 1) pop_heap(first, last--, a);
    }
    void make_heap(int first, int last, Element* a) const {
        if (last - first < 2) return;
        const int len = last - first;
        int parent = (len - 2) / 2;
        while (true) {
            adjust_heap(first, parent, len, a[first + parent], a);
            if (parent == 0) return;
            parent--;
        }
    }
    void partial_sort(int first, int last, int middle, Element* a) const {
        make_heap(first, middle, a);
        for (int i = middle; i < last; i++) {
            if (compare(a[i], a[first])) pop_heap(first, middle, i, a[i], a);
        }
        sort_heap(first, middle, a);
    }
    int partitioner(int first, int last, Element pivot, Element* a) const {
        while (true) {
            while (compare(a[first], pivot)) first++;
            last--;
            while (compare(pivot, a[last])) last--;
            if (!(first < last)) return first;
            std::swap(a[first], a[last]);
            first++;
        }
    }
    void introsort(int first, int last, Element* a, int max_depth) const {
        while (last - first > kIntrosortThreshold) {
            if (max_depth == 0) {
                partial_sort(first, last, last, a);
                return;
            }
            max_depth--;
            const int cut =
                partitioner(first, last, median_of_3(a[first], a[first + (last - first) / 2], a[last - 1]), a);
            introsort(cut, last, a, max_depth);
            last = cut;
        }
    }
    void unguarded_linear_insert(int last, Element value, Element* a) const {
        int next = last - 1;
        while (compare(value, a[next])) {
            a[last] = a[next];
            last = next;
            next--;
        }
        a[last] = value;
    }
    void linear_insert(int first, int last, Element* a) const {
        Element val = a[last];
        if (compare(val, a[first])) {
            for (int i = last; i > first; i--) a[i] = a[i - 1];
            a[first] = val;
        } else {
            unguarded_linear_insert(last, val, a);
        }
    }
    void insertion_sort(int first, int last, Element* a) const {
        if (first == last) return;
        for (int i = first + 1; i != last; i++) linear_insert(first, i, a);
    }
    void unguarded_insertion_sort(int first, int last, Element* a) const {
        for (int i = first; i != last; i++) unguarded_linear_insert(i, a[i], a);
    }
    void final_insertion_sort(int first, int last, Element* a) const {
        if (last - first > kIntrosortThreshold) {
            insertion_sort(first, first + kIntrosortThreshold, a);
            unguarded_insertion_sort(first + kIntrosortThreshold, last, a);
        } else {
            insertion_sort(first, last, a);
        }
    }
    void sort_range(int first, int last, Element* a) const {
        if (first != last) {
            introsort(first, last, a, bitlog(last - first) * 2);
            final_insertion_sort(first, last, a);
        }
    }
};

struct TreeNode {
    AABB aabb;
    std::unique_ptr<TreeNode> left;
    std::unique_ptr<TreeNode> right;
    int face_index = 0;
};

std::unique_ptr<TreeNode> BuildBvh(Element* elements, int size, int& count) {
    auto bvh = std::make_unique<TreeNode>();
    if (size == 1) {
        bvh->aabb = elements[0].aabb;
        bvh->face_index = elements->face_index;
        count++;
        return bvh;
    }
    bvh->face_index = -1;
    AABB box;
    for (int i = 0; i < size; i++) {
        if (i == 0) {
            box = elements[i].aabb;
        } else {
            box.merge_with(elements[i].aabb);
        }
    }
    bvh->aabb = box;
    ElementSort(box.get_longest_axis_index()).sort(elements, size);
    const int split = size / 2;
    bvh->left = BuildBvh(elements, split, count);
    bvh->right = BuildBvh(&elements[split], size - split, count);
    count++;
    return bvh;
}

void FillBvh(const TreeNode* tree, std::vector<ConcavePolygonShape3D::BVH>& out, int& idx) {
    const int at = idx;
    out[static_cast<std::size_t>(at)].aabb = tree->aabb;
    out[static_cast<std::size_t>(at)].face_index = tree->face_index;
    if (tree->left) {
        out[static_cast<std::size_t>(at)].left = ++idx;
        FillBvh(tree->left.get(), out, idx);
    } else {
        out[static_cast<std::size_t>(idx)].left = -1;
    }
    if (tree->right) {
        out[static_cast<std::size_t>(at)].right = ++idx;
        FillBvh(tree->right.get(), out, idx);
    } else {
        out[static_cast<std::size_t>(idx)].right = -1;
    }
}

struct SegmentCull {
    Vector3 from;
    Vector3 to;
    Vector3 dir;
    const ConcavePolygonShape3D* shape = nullptr;
    bool backface_collision = false;   // GodotFaceShape3D::backface_collision
    Vector3 result;
    Vector3 normal;
    float min_d = 1e20f;
    int collisions = 0;
};

// godot_shape_3d.cpp GodotFaceShape3D::intersect_segment, called with
// p_hit_back_faces = true by _cull_segment.
bool FaceIntersectSegment(const Vector3& v0, const Vector3& v1, const Vector3& v2, bool backface_collision,
                          const Vector3& begin, const Vector3& end, Vector3& r_result, Vector3& r_normal) {
    bool c = SegmentIntersectsTriangle(begin, end, v0, v1, v2, &r_result);
    if (c) {
        r_normal = ClockwiseNormal(v0, v1, v2);
        if (Dot(r_normal, Sub(end, begin)) > 0.0f) {
            if (backface_collision) {
                r_normal = Scale(r_normal, -1.0f);
            } else {
                c = false;
            }
        }
    }
    return c;
}

// godot_shape_3d.cpp:1348 _cull_segment
void CullSegment(int idx, SegmentCull& p) {
    const ConcavePolygonShape3D::BVH& node = p.shape->bvh[static_cast<std::size_t>(idx)];
    if (!node.aabb.intersects_segment(p.from, p.to)) return;
    if (node.face_index >= 0) {
        const ConcavePolygonShape3D::Face& f = p.shape->faces[static_cast<std::size_t>(node.face_index)];
        Vector3 res;
        Vector3 normal;
        if (FaceIntersectSegment(p.shape->vertices[static_cast<std::size_t>(f.indices[0])],
                                 p.shape->vertices[static_cast<std::size_t>(f.indices[1])],
                                 p.shape->vertices[static_cast<std::size_t>(f.indices[2])], p.backface_collision,
                                 p.from, p.to, res, normal)) {
            const float d = Dot(p.dir, res) - Dot(p.dir, p.from);
            if (d > 0.0f && d < p.min_d) {
                p.min_d = d;
                p.result = res;
                p.normal = normal;
                p.collisions++;
            }
        }
    } else {
        if (node.left >= 0) CullSegment(node.left, p);
        if (node.right >= 0) CullSegment(node.right, p);
    }
}

}  // namespace

// --- AABB -------------------------------------------------------------------------

// aabb.cpp AABB::intersects_segment
bool AABB::intersects_segment(const Vector3& from, const Vector3& to, Vector3* r_clip, Vector3* r_normal) const {
    float min = 0.0f;
    float max = 1.0f;
    int axis = 0;
    float sign = 0.0f;
    for (int i = 0; i < 3; i++) {
        const float seg_from = Axis(from, i);
        const float seg_to = Axis(to, i);
        const float box_begin = Axis(position, i);
        const float box_end = box_begin + Axis(size, i);
        float cmin;
        float cmax;
        float csign;
        if (seg_from < seg_to) {
            if (seg_from > box_end || seg_to < box_begin) return false;
            const float length = seg_to - seg_from;
            cmin = (seg_from < box_begin) ? ((box_begin - seg_from) / length) : 0.0f;
            cmax = (seg_to > box_end) ? ((box_end - seg_from) / length) : 1.0f;
            csign = -1.0f;
        } else {
            if (seg_to > box_end || seg_from < box_begin) return false;
            const float length = seg_to - seg_from;
            cmin = (seg_from > box_end) ? (box_end - seg_from) / length : 0.0f;
            cmax = (seg_to < box_begin) ? (box_begin - seg_from) / length : 1.0f;
            csign = 1.0f;
        }
        if (cmin > min) {
            min = cmin;
            axis = i;
            sign = csign;
        }
        if (cmax < max) max = cmax;
        if (max < min) return false;
    }
    const Vector3 rel = Sub(to, from);
    if (r_normal) {
        Vector3 normal;
        SetAxis(normal, axis, sign);
        *r_normal = normal;
    }
    if (r_clip) *r_clip = Add(from, Scale(rel, min));
    return true;
}

// aabb.cpp AABB::merge_with
void AABB::merge_with(const AABB& other) {
    const Vector3 beg_1 = position;
    const Vector3 beg_2 = other.position;
    const Vector3 end_1 = Add(size, beg_1);
    const Vector3 end_2 = Add(other.size, beg_2);
    Vector3 mn;
    Vector3 mx;
    for (int i = 0; i < 3; ++i) {
        SetAxis(mn, i, Axis(beg_1, i) < Axis(beg_2, i) ? Axis(beg_1, i) : Axis(beg_2, i));
        SetAxis(mx, i, Axis(end_1, i) > Axis(end_2, i) ? Axis(end_1, i) : Axis(end_2, i));
    }
    position = mn;
    size = Sub(mx, mn);
}

// aabb.cpp:275 AABB::get_longest_axis_index; ties keep the lower axis.
int AABB::get_longest_axis_index() const {
    int axis = 0;
    float max_size = size.x;
    if (size.y > max_size) {
        axis = 1;
        max_size = size.y;
    }
    if (size.z > max_size) axis = 2;
    return axis;
}

// --- shapes ------------------------------------------------------------------------

// godot_shape_3d.cpp:420
bool BoxShape3D::intersect_segment(const Vector3& begin, const Vector3& end, Vector3& r_result, Vector3& r_normal,
                                   bool /*hit_back_faces*/) const {
    const AABB ext{Scale(half_extents, -1.0f), Scale(half_extents, 2.0f)};
    return ext.intersects_segment(begin, end, &r_result, &r_normal);
}

// godot_shape_3d.cpp:746: the height axis is Y (cylinder axis 1).
bool CylinderShape3D::intersect_segment(const Vector3& begin, const Vector3& end, Vector3& r_result,
                                        Vector3& r_normal, bool /*hit_back_faces*/) const {
    return SegmentIntersectsCylinder(begin, end, height, radius, &r_result, &r_normal, 1);
}

// godot_shape_3d.cpp:750
bool CylinderShape3D::intersect_point(const Vector3& p) const {
    if (std::fabs(p.y) < height * 0.5f) {
        return std::sqrt(p.x * p.x + p.z * p.z) < radius;
    }
    return false;
}

// godot_shape_3d.cpp:426
bool BoxShape3D::intersect_point(const Vector3& p) const {
    return std::fabs(p.x) < half_extents.x && std::fabs(p.y) < half_extents.y && std::fabs(p.z) < half_extents.z;
}

// godot_shape_3d.cpp GodotConcavePolygonShape3D::_setup
void ConcavePolygonShape3D::set_faces(const std::vector<Vector3>& soup) {
    faces.clear();
    vertices.clear();
    bvh.clear();
    const int src_face_count = static_cast<int>(soup.size() / 3);
    if (src_face_count == 0) return;

    std::vector<Element> elements(static_cast<std::size_t>(src_face_count));
    faces.resize(static_cast<std::size_t>(src_face_count));
    vertices.resize(static_cast<std::size_t>(src_face_count) * 3u);
    for (int i = 0; i < src_face_count; i++) {
        const std::size_t k = static_cast<std::size_t>(i) * 3u;
        const Vector3& v0 = soup[k];
        const Vector3& v1 = soup[k + 1];
        const Vector3& v2 = soup[k + 2];
        Element& el = elements[static_cast<std::size_t>(i)];
        el.aabb = FaceAabb(v0, v1, v2);
        el.center = el.aabb.get_center();
        el.face_index = i;
        Face& f = faces[static_cast<std::size_t>(i)];
        f.indices[0] = i * 3;
        f.indices[1] = i * 3 + 1;
        f.indices[2] = i * 3 + 2;
        f.normal = ClockwiseNormal(v0, v1, v2);
        vertices[k] = v0;
        vertices[k + 1] = v1;
        vertices[k + 2] = v2;
    }

    int count = 0;
    const std::unique_ptr<TreeNode> tree = BuildBvh(elements.data(), src_face_count, count);
    bvh.resize(static_cast<std::size_t>(count) + 1u);
    int idx = 0;
    FillBvh(tree.get(), bvh, idx);
}

// godot_shape_3d.cpp:1384
bool ConcavePolygonShape3D::intersect_segment(const Vector3& begin, const Vector3& end, Vector3& r_result,
                                              Vector3& r_normal, bool hit_back_faces) const {
    if (faces.empty()) return false;
    SegmentCull p;
    p.from = begin;
    p.to = end;
    p.dir = Normalized(Sub(end, begin));
    p.shape = this;
    p.backface_collision = backface_collision && hit_back_faces;
    CullSegment(0, p);
    if (p.collisions > 0) {
        r_result = p.result;
        r_normal = p.normal;
        return true;
    }
    return false;
}

bool ConcavePolygonShape3D::intersect_point(const Vector3& /*point*/) const {
    return false;   // godot_shape_3d.cpp: a concave shape has no inside
}

// --- the query ---------------------------------------------------------------------

// godot_space_3d.cpp GodotPhysicsDirectSpaceState3D::intersect_ray
bool PhysicsDirectSpaceState3D::intersect_ray(const RayQuery& q, RayResult& r) const {
    r = RayResult{};
    const Vector3 normal = Normalized(Sub(q.to, q.from));
    bool collided = false;
    Vector3 res_point;
    Vector3 res_normal;
    const Entry* res = nullptr;
    float min_d = 1e10f;

    for (const Entry& e : entries_) {
        // _can_collide_with: layer & mask, and the object type (bodies here).
        if ((e.layer & q.collision_mask) == 0 || !q.collide_with_bodies) continue;

        const Vector3 local_from = XformPoint(e.inverse, q.from);
        const Vector3 local_to = XformPoint(e.inverse, q.to);

        if (e.shape->intersect_point(local_from)) {
            if (q.hit_from_inside) {
                min_d = 0.0f;
                res_point = q.from;
                res_normal = {};
                res = &e;
                collided = true;
                break;
            }
            continue;
        }

        Vector3 shape_point;
        Vector3 shape_normal;
        if (e.shape->intersect_segment(local_from, local_to, shape_point, shape_normal, q.hit_back_faces)) {
            shape_point = XformPoint(e.xform, shape_point);
            const float ld = Dot(normal, shape_point);
            if (ld < min_d) {
                min_d = ld;
                res_point = shape_point;
                res_normal = Normalized(BasisXformInv(e.inverse, shape_normal));
                res = &e;
                collided = true;
            }
        }
    }

    if (!collided || !res) return false;
    r.collider = res->body;
    r.normal = res_normal;
    r.position = res_point;
    r.shape = res->shape_index;
    return true;
}

}  // namespace godot
