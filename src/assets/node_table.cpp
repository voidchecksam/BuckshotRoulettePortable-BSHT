#include "assets/node_table.h"

namespace assets {

int NodeTable::Add(const std::string& path, int parent,
                   const core::Mat4& restLocal, bool visible) {
    const auto it = byPath_.find(path);
    if (it != byPath_.end()) return it->second;

    TransformNode n;
    n.path = path;
    n.parent = parent;
    n.restLocal = restLocal;
    n.visible = visible;
    nodes_.push_back(std::move(n));

    const int index = static_cast<int>(nodes_.size()) - 1;
    byPath_.emplace(path, index);
    return index;
}

int NodeTable::Find(const std::string& path) const {
    const auto it = byPath_.find(path);
    return it == byPath_.end() ? -1 : it->second;
}

void TransformCache::Reset(const NodeTable& table) {
    locals_.resize(table.Size());
    world_.assign(table.Size(), core::Mat4{});
    for (std::size_t i = 0; i < table.Size(); ++i) {
        locals_[i] = table.At(static_cast<int>(i)).restLocal;
    }
    Recompute(table);
}

void TransformCache::Recompute(const NodeTable& table) {
    if (world_.size() != table.Size()) {
        world_.assign(table.Size(), core::Mat4{});
    }

    // Parents are added before their children by construction, so a single
    // forward pass is enough. The guard below checks it: a node whose parent
    // comes later composes onto identity instead of onto stale data.
    for (std::size_t i = 0; i < table.Size(); ++i) {
        const TransformNode& n = table.At(static_cast<int>(i));
        if (n.parent >= 0 && n.parent < static_cast<int>(i)) {
            world_[i] = core::Mul(world_[static_cast<std::size_t>(n.parent)],
                                  locals_[i]);
        } else {
            world_[i] = locals_[i];
        }
    }
}

}  // namespace assets
