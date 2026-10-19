// A triangle BVH for the surface bake's shadow rays. Bake-time only: the device
// never traces a ray.
//
// Median split on the longest axis of the centroid bounds; leaves hold up to
// four triangles. A shadow query only needs any hit strictly inside the
// segment, so traversal stops at the first one.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace assets {

class LightBvh {
public:
    // `tris`: 9 floats per triangle (three world-space vertices).
    void Build(const std::vector<float>& tris);

    // One leaf over every triangle: the reference a test compares against.
    void BuildFlat(const std::vector<float>& tris);

    // True when a triangle crosses the open segment from -> to (hits within a
    // small epsilon of either end do not count, so a surface does not shadow
    // itself and a lamp's own fitting at the far end is not the occluder).
    bool Occluded(const float from[3], const float to[3]) const;

    std::size_t TriangleCount() const { return tris_.size() / 9; }

private:
    struct Node {
        float lo[3];
        float hi[3];
        uint32_t first;   // leaf: first triangle index into order_; inner: left child
        uint32_t count;   // leaf: triangle count; inner: 0 (right child = left + 1)
    };
    uint32_t BuildNode(uint32_t first, uint32_t count, const std::vector<float>& centroids);

    std::vector<float>    tris_;
    std::vector<uint32_t> order_;
    std::vector<Node>     nodes_;
};

}  // namespace assets
