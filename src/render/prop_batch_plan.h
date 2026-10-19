// Prop batching, the headless half: which props share a chunk (one vertex
// buffer, one draw, a matrix palette slot per prop) and the index list a chunk
// draws for the members that joined it this frame. No GL, so the rules are
// tested on their own; render::SceneRenderer does the uploads.
#pragma once

#include "assets/room.h"

#include <cstdint>
#include <vector>

namespace render {

// Whether a prop may live in a chunk at all: not skinned (it skins on its
// own), not a PackedScene template (drawn per clone), not in the moving
// light's scope (the MOVE program), not baked at its own grade (a chunk's
// palette carries one offset per program: the car and the highway are graded
// against their own grade), and with indices.
bool PropCanBatch(const assets::RoomProp& prop);

// One prop, as the planner needs it: its base page, its vertex count, and
// whether it can ever batch (not skinned, not a PackedScene template, outside
// the moving light's scope).
struct BatchProp {
    int page = -1;
    uint32_t vertices = 0;
    bool batchable = false;
};

// Props on one page, at most `slots` of them and `maxVertices` vertices (16-bit
// indices), in upload order. `firstVertex[i]` is props[i]'s first vertex in
// the chunk's buffer.
struct PropChunk {
    int page = -1;
    std::vector<int> props;
    std::vector<uint32_t> firstVertex;
    uint32_t vertices = 0;
};

// Chunks every batchable prop with a page (an untextured prop draws alone).
std::vector<PropChunk> BuildChunks(const std::vector<BatchProp>& props, int slots, uint32_t maxVertices);

// A member's indices within its chunk's index array.
struct IndexRange {
    uint32_t first = 0;
    uint32_t count = 0;
};

// The indices of the members whose bit is set in `joined` (bit i = slot i),
// in slot order, into `out` (cleared first; reuses its capacity).
void BuildIndexList(const std::vector<uint16_t>& all, const std::vector<IndexRange>& ranges, uint32_t joined,
                    std::vector<uint16_t>& out);

}  // namespace render
