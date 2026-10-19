// One chunk of batched props on the GPU: every member's model-space vertices
// in one buffer, each tagged with its palette slot, and the members' indices.
// A frame draws the members that joined, per texture (a member may show a
// variant page), from an index list rebuilt only when that set changes; a
// member that must draw alone uses its own range.
#pragma once

#include "render/mesh_gpu.h"
#include "render/prop_batch_plan.h"

#include <cstdint>
#include <vector>

namespace render {

class PropChunkGpu {
public:
    PropChunkGpu() = default;
    ~PropChunkGpu();
    PropChunkGpu(const PropChunkGpu&) = delete;
    PropChunkGpu& operator=(const PropChunkGpu&) = delete;
    PropChunkGpu(PropChunkGpu&& other) noexcept;
    PropChunkGpu& operator=(PropChunkGpu&& other) noexcept;

    // `ranges[slot]` are member `slot`'s indices within `indices`.
    bool Upload(const std::vector<Vertex>& vertices, std::vector<uint16_t> indices, std::vector<IndexRange> ranges);
    void Destroy();

    // Draws the members in `mask` with the bound texture and program. Index
    // lists are cached per (key, mask), since a chunk can draw several runs a
    // frame, and the least recently used of kLists is rebuilt for a new one.
    void DrawJoined(int key, uint32_t mask);
    static constexpr int kLists = 8;
    // One member, from the chunk's full indices.
    void DrawMember(int slot) const;

    int MemberTriangles(int slot) const {
        return slot >= 0 && slot < static_cast<int>(ranges_.size()) ? static_cast<int>(ranges_[slot].count / 3) : 0;
    }
    // Index lists rebuilt since the last call (for the census log).
    int TakeRebuilds() {
        const int n = rebuilds_;
        rebuilds_ = 0;
        return n;
    }

private:
    struct List {
        int key = -2;
        uint32_t mask = 0;
        GLuint ibo = 0;
        GLsizei count = 0;
        uint64_t used = 0;
    };
    uint64_t clock_ = 0;
    void DrawIndices(GLuint ibo, GLsizei count, const void* offset) const;

    GLuint vbo_ = 0;
    GLuint ibo_ = 0;
    std::vector<uint16_t> indices_;   // CPU copy: the lists are built from it
    std::vector<IndexRange> ranges_;
    std::vector<List> lists_;
    std::vector<uint16_t> scratch_;
    int rebuilds_ = 0;
};

}  // namespace render
