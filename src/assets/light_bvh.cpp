#include "assets/light_bvh.h"

#include <algorithm>
#include <cmath>

namespace assets {

namespace {

constexpr uint32_t kLeafSize = 4;
constexpr float kEnds = 1e-4f;   // fraction of the segment ignored at each end

// Moller-Trumbore; returns the segment parameter t of the hit, or -1.
float IntersectSegment(const float* v, const float o[3], const float d[3]) {
    const float e1[3] = {v[3] - v[0], v[4] - v[1], v[5] - v[2]};
    const float e2[3] = {v[6] - v[0], v[7] - v[1], v[8] - v[2]};
    const float p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2],
                        d[0] * e2[1] - d[1] * e2[0]};
    const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (std::fabs(det) < 1e-12f) return -1.0f;
    const float inv = 1.0f / det;
    const float s[3] = {o[0] - v[0], o[1] - v[1], o[2] - v[2]};
    const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
    if (u < 0.0f || u > 1.0f) return -1.0f;
    const float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2],
                        s[0] * e1[1] - s[1] * e1[0]};
    const float w = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (w < 0.0f || u + w > 1.0f) return -1.0f;
    return (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
}

// Slab test against the segment o + t d, t in [0, 1].
bool SegmentHitsBox(const float lo[3], const float hi[3], const float o[3], const float d[3]) {
    float t0 = 0.0f, t1 = 1.0f;
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(d[a]) < 1e-12f) {
            if (o[a] < lo[a] || o[a] > hi[a]) return false;
            continue;
        }
        const float inv = 1.0f / d[a];
        float ta = (lo[a] - o[a]) * inv, tb = (hi[a] - o[a]) * inv;
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    return true;
}

}  // namespace

void LightBvh::BuildFlat(const std::vector<float>& tris) {
    tris_ = tris;
    const uint32_t n = static_cast<uint32_t>(tris_.size() / 9);
    order_.resize(n);
    for (uint32_t i = 0; i < n; ++i) order_[i] = i;
    nodes_.clear();
    Node root{{-1e30f, -1e30f, -1e30f}, {1e30f, 1e30f, 1e30f}, 0, n};
    nodes_.push_back(root);
}

void LightBvh::Build(const std::vector<float>& tris) {
    tris_ = tris;
    const uint32_t n = static_cast<uint32_t>(tris_.size() / 9);
    order_.resize(n);
    std::vector<float> centroids(static_cast<std::size_t>(n) * 3);
    for (uint32_t i = 0; i < n; ++i) {
        order_[i] = i;
        const float* v = &tris_[static_cast<std::size_t>(i) * 9];
        for (int a = 0; a < 3; ++a) centroids[i * 3 + a] = (v[a] + v[3 + a] + v[6 + a]) / 3.0f;
    }
    nodes_.clear();
    nodes_.reserve(static_cast<std::size_t>(n) * 2 + 1);
    if (n == 0) {
        nodes_.push_back(Node{{1, 1, 1}, {0, 0, 0}, 0, 0});   // empty box: nothing hits
        return;
    }
    BuildNode(0, n, centroids);
}

uint32_t LightBvh::BuildNode(uint32_t first, uint32_t count, const std::vector<float>& centroids) {
    const uint32_t index = static_cast<uint32_t>(nodes_.size());
    nodes_.push_back(Node{});
    Node node{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}, first, count};
    float clo[3] = {1e30f, 1e30f, 1e30f}, chi[3] = {-1e30f, -1e30f, -1e30f};
    for (uint32_t i = first; i < first + count; ++i) {
        const float* v = &tris_[static_cast<std::size_t>(order_[i]) * 9];
        for (int k = 0; k < 3; ++k) {
            for (int a = 0; a < 3; ++a) {
                node.lo[a] = std::min(node.lo[a], v[k * 3 + a]);
                node.hi[a] = std::max(node.hi[a], v[k * 3 + a]);
            }
        }
        for (int a = 0; a < 3; ++a) {
            clo[a] = std::min(clo[a], centroids[order_[i] * 3 + a]);
            chi[a] = std::max(chi[a], centroids[order_[i] * 3 + a]);
        }
    }
    if (count <= kLeafSize) {
        nodes_[index] = node;
        return index;
    }
    int axis = 0;
    for (int a = 1; a < 3; ++a) {
        if (chi[a] - clo[a] > chi[axis] - clo[axis]) axis = a;
    }
    const uint32_t mid = first + count / 2;
    std::nth_element(order_.begin() + first, order_.begin() + mid, order_.begin() + first + count,
                     [&](uint32_t a, uint32_t b) { return centroids[a * 3 + axis] < centroids[b * 3 + axis]; });
    // Children are built depth-first, so the left child is always index + 1;
    // the right child's index is stored in `first`.
    BuildNode(first, mid - first, centroids);
    const uint32_t right = BuildNode(mid, first + count - mid, centroids);
    node.first = right;
    node.count = 0;
    nodes_[index] = node;
    return index;
}

bool LightBvh::Occluded(const float from[3], const float to[3]) const {
    if (nodes_.empty() || tris_.empty()) return false;
    const float d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    uint32_t stack[64];
    int top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Node& n = nodes_[stack[--top]];
        if (!SegmentHitsBox(n.lo, n.hi, from, d)) continue;
        if (n.count > 0) {
            for (uint32_t i = n.first; i < n.first + n.count; ++i) {
                const float t = IntersectSegment(&tris_[static_cast<std::size_t>(order_[i]) * 9], from, d);
                if (t > kEnds && t < 1.0f - kEnds) return true;
            }
            continue;
        }
        const uint32_t self = static_cast<uint32_t>(&n - nodes_.data());
        if (top + 2 > 64) return false;   // depth cannot reach this for < 2^60 triangles
        stack[top++] = n.first;           // right
        stack[top++] = self + 1;          // left
    }
    return false;
}

}  // namespace assets
