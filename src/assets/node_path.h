// Godot NodePath resolution.
//
// Animation track paths are relative to their AnimationPlayer's root node,
// which defaults to the player's parent but need not be: one player in this
// scene sets it a level higher. A track pointing at a missing node does
// nothing, silently, so the exporter writes each player's real root and this
// resolves against it.
//
// Header-only and dependency-free so the runtime and the offline tools resolve
// paths the same way. Two implementations would drift, and the drift would
// show up as props that do not move.
#pragma once

#include <string>
#include <vector>

namespace assets {

// Joins a relative Godot NodePath onto a base path, collapsing "." and "..".
//
// A ".." that would climb above the root is dropped rather than producing a
// path with a leading "..", matching Godot's behaviour at the scene root.
inline std::string ResolveNodePath(const std::string& base, const std::string& rel) {
    std::vector<std::string> parts;

    auto split = [&parts](const std::string& s) {
        std::string seg;
        for (char c : s) {
            if (c == '/') {
                if (!seg.empty()) {
                    if (seg == "..") {
                        if (!parts.empty()) parts.pop_back();
                    } else if (seg != ".") {
                        parts.push_back(seg);
                    }
                    seg.clear();
                }
            } else {
                seg.push_back(c);
            }
        }
        if (!seg.empty()) {
            if (seg == "..") {
                if (!parts.empty()) parts.pop_back();
            } else if (seg != ".") {
                parts.push_back(seg);
            }
        }
    };

    split(base);
    split(rel);

    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out.push_back('/');
        out += parts[i];
    }
    return out;
}

}  // namespace assets
