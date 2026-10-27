#include "render/prop_batch_plan.h"

#include "assets/moving_lights.h"

#include <cstddef>

namespace render {

bool PropCanBatch(const assets::RoomProp& prop) {
    const bool skinned = !prop.mesh.joints.empty() && prop.mesh.weights.size() == prop.mesh.joints.size();
    return !skinned && prop.templateKey.empty() && !prop.mesh.indices.empty() &&
           !assets::InMovingLightScope(prop.nodePath) && !prop.HasOwnGrade() && prop.variants.empty();
}

std::vector<PropChunk> BuildChunks(const std::vector<BatchProp>& props, int slots, uint32_t maxVertices) {
    std::vector<PropChunk> chunks;
    // The chunk still taking props for each page (index into `chunks`).
    std::vector<std::pair<int, std::size_t>> open;
    for (std::size_t i = 0; i < props.size(); ++i) {
        const BatchProp& p = props[i];
        if (!p.batchable || p.page < 0 || p.vertices == 0 || p.vertices > maxVertices) continue;
        PropChunk* chunk = nullptr;
        for (auto& [page, index] : open) {
            if (page != p.page) continue;
            PropChunk& c = chunks[index];
            if (static_cast<int>(c.props.size()) < slots && c.vertices + p.vertices <= maxVertices) chunk = &c;
            break;
        }
        if (!chunk) {
            chunks.push_back(PropChunk{p.page, {}, {}, 0});
            bool reopened = false;
            for (auto& [page, index] : open) {
                if (page == p.page) {
                    index = chunks.size() - 1;   // the full chunk closes; this one takes over
                    reopened = true;
                }
            }
            if (!reopened) open.emplace_back(p.page, chunks.size() - 1);
            chunk = &chunks.back();
        }
        chunk->props.push_back(static_cast<int>(i));
        chunk->firstVertex.push_back(chunk->vertices);
        chunk->vertices += p.vertices;
    }
    return chunks;
}

void BuildIndexList(const std::vector<uint16_t>& all, const std::vector<IndexRange>& ranges, uint32_t joined,
                    std::vector<uint16_t>& out) {
    out.clear();
    for (std::size_t slot = 0; slot < ranges.size() && slot < 32; ++slot) {
        if (!(joined & (1u << slot))) continue;
        const IndexRange& r = ranges[slot];
        out.insert(out.end(), all.begin() + r.first, all.begin() + r.first + r.count);
    }
}

}  // namespace render
