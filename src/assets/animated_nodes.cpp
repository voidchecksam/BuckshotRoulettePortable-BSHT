#include "assets/animated_nodes.h"

namespace assets {

void AnimatedNodeSet::Build(const AnimationLibrary& lib) {
    paths_.clear();
    moving_.clear();

    for (const AnimationPlayerData& player : lib.Players()) {
        for (const Animation& anim : player.animations) {
            for (const Track& track : anim.tracks) {
                // Managers, not geometry.
                if (track.type == TrackType::Method ||
                    track.type == TrackType::Audio) {
                    continue;
                }
                if (track.nodePath.empty()) continue;

                // Only properties that require per-node identity break a batch.
                // A track animating, say, an environment value does not stop
                // its node's geometry being merged.
                const bool needsIdentity =
                    track.type == TrackType::Position3D ||
                    track.type == TrackType::Rotation3D ||
                    track.type == TrackType::Scale3D ||
                    track.property == "position" ||
                    track.property == "rotation" ||
                    track.property == "scale" ||
                    track.property == "visible" ||
                    track.property == "transparency" ||
                    track.property == "material_override:albedo_color" ||
                    track.property == "mesh:surface_0/material:albedo_color";
                if (!needsIdentity) continue;

                const std::string path = ResolveNodePath(player.root, track.nodePath);
                paths_.insert(path);
                const bool transform = track.type == TrackType::Position3D ||
                                       track.type == TrackType::Rotation3D ||
                                       track.type == TrackType::Scale3D || track.property == "position" ||
                                       track.property == "rotation" || track.property == "scale" ||
                                       track.property == "rotation_degrees";
                if (transform) moving_.insert(path);
            }
        }
    }
}

}  // namespace assets
