// Which scene nodes animation touches.
//
// It answers one question for the room builder: may this node's geometry be
// merged into a shared world-space batch? Merging is right for static walls and
// wrong for anything that moves: merged vertices are already at their final
// world positions, with no per-node handle left to translate, rotate or hide.
// Animated nodes are held out and drawn as props.
#pragma once

#include "assets/animation.h"
#include "assets/node_path.h"

#include <string>
#include <unordered_set>

namespace assets {

class AnimatedNodeSet {
public:
    // Collects every node path referenced by a transform-or-visibility track,
    // resolved against each player's real root node.
    //
    // METHOD and AUDIO tracks are skipped: they target managers, not geometry,
    // and treating a manager as an animated prop would pointlessly break a
    // batch apart.
    void Build(const AnimationLibrary& lib);

    bool Contains(const std::string& nodePath) const {
        return paths_.find(nodePath) != paths_.end();
    }

    // Whether this node moves when animation runs: it, or any ancestor, is
    // animated. What the room builder must ask before merging geometry.
    bool Covers(const std::string& nodePath) const {
        for (std::size_t end = nodePath.size();;) {
            if (paths_.count(nodePath.substr(0, end)) != 0) return true;
            end = nodePath.rfind('/', end - 1);
            if (end == std::string::npos || end == 0) return false;
        }
    }

    // Whether animation MOVES this node: it or an ancestor has a transform
    // track (position / rotation / scale). Visibility alone does not count.
    // A moving prop must not bake its shadow into the static surfaces: the
    // shadow would stay where it rested (the shotgun, the waiver).
    bool Moves(const std::string& nodePath) const {
        for (std::size_t end = nodePath.size();;) {
            if (moving_.count(nodePath.substr(0, end)) != 0) return true;
            end = nodePath.rfind('/', end - 1);
            if (end == std::string::npos || end == 0) return false;
        }
    }

    std::size_t Size() const { return paths_.size(); }
    const std::unordered_set<std::string>& Paths() const { return paths_; }

    void Clear() {
        paths_.clear();
        moving_.clear();
    }

private:
    std::unordered_set<std::string> paths_;
    std::unordered_set<std::string> moving_;   // transform tracks only
};

}  // namespace assets
