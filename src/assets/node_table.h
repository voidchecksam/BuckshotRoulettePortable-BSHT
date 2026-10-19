// The runtime transform hierarchy.
//
// Animation often moves a node with no mesh of its own: "player shoot dealer"
// animates `tabletop parent/shotgun main parent`, and every shotgun mesh under
// it inherits the motion. So any node may be animated and its descendants
// compose onto it. This table holds only the nodes that matter (every prop and
// every ancestor of a prop), not the whole scene.
//
// Headless and dependency-free, so the same code runs on both targets.
#pragma once

#include "core/mat4.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace assets {

struct TransformNode {
    std::string path;
    int         parent = -1;     // index into the table, -1 for a root
    core::Mat4  restLocal;       // as authored in the scene

    // The node's own visible flag at load, not visible_in_tree. Whether it is
    // on screen is this and every ancestor's, resolved each frame, because
    // scripts show and hide parents (HealthCounter.Bootup shows three panels
    // and every digit under them appears).
    bool        visible = true;
};

class NodeTable {
public:
    // Adds a node (and returns its index), or returns the existing one.
    int Add(const std::string& path, int parent, const core::Mat4& restLocal,
            bool visible = true);

    int Find(const std::string& path) const;

    std::size_t Size() const { return nodes_.size(); }
    const std::vector<TransformNode>& Nodes() const { return nodes_; }
    const TransformNode& At(int i) const { return nodes_[static_cast<std::size_t>(i)]; }

    void Clear() {
        nodes_.clear();
        byPath_.clear();
    }

    void Reserve(std::size_t n) {
        nodes_.reserve(n);
        byPath_.reserve(n * 2);
    }

private:
    std::vector<TransformNode> nodes_;
    std::unordered_map<std::string, int> byPath_;
};

// Live world transforms for a NodeTable.
//
// Locals start at their rest pose; animation overwrites the ones it drives.
// World transforms are recomputed from the locals each frame: the table holds
// a few hundred nodes, so composing them all is cheaper than tracking which
// subtrees are dirty, and it cannot go stale.
class TransformCache {
public:
    void Reset(const NodeTable& table);

    // The local an animation writes into. Index must be valid.
    core::Mat4& Local(int i) { return locals_[static_cast<std::size_t>(i)]; }

    void Recompute(const NodeTable& table);

    const core::Mat4& World(int i) const {
        if (i < 0 || i >= static_cast<int>(world_.size())) return identity_;
        return world_[static_cast<std::size_t>(i)];
    }

    std::size_t Size() const { return locals_.size(); }

private:
    std::vector<core::Mat4> locals_;
    std::vector<core::Mat4> world_;
    core::Mat4 identity_;
};

}  // namespace assets
