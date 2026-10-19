#include "debug/state.h"

#include <algorithm>

#include "assets/animation.h"
#include "assets/node_path.h"
#include "assets/font.h"
#include "assets/room.h"
#include "assets/ui_layout.h"
#include "godot/scene_tree.h"

namespace debug {

namespace {

// No second path resolver: assets::ResolveNodePath is the one the animator
// uses, so the census cannot disagree with the animator about which tracks
// resolve. Track::nodePath is already split from the property by the loader,
// so there is nothing to parse here either.

void Name(std::vector<std::string>& out, const std::string& s) {
    if (static_cast<int>(out.size()) < kMaxNamed) out.push_back(s);
}

}  // namespace

void GatherStatic(DebugState& out,
                  const assets::Room& room,
                  const assets::AnimationLibrary& lib,
                  const godot::SceneTree& tree,
                  const assets::UiLayout* ui,
                  const assets::FontAtlas* fonts,
                  const std::vector<std::string>& screenPrefixes) {
    // --- room ---------------------------------------------------------------
    out.room = RoomCensus{};
    out.room.batches   = static_cast<int>(room.Batches().size());
    out.room.props     = static_cast<int>(room.Props().size());
    out.room.triangles = room.Stats().triangles;
    out.room.textures  = static_cast<int>(room.Textures().Size());
    out.room.textureBytes = static_cast<int>(room.Textures().ByteSize());

    out.props.clear();
    out.props.reserve(room.Props().size());
    for (const assets::RoomProp& p : room.Props()) {
        const bool hasTarget = tree.get_node(p.nodePath) != nullptr;

        PropRow row;
        row.path          = p.nodePath;
        row.visibleAtLoad = p.visibleAtLoad;
        row.hasTarget     = hasTarget;
        row.hasTexture    = p.texture >= 0;
        row.triangles     = p.mesh.TriangleCount();
        out.props.push_back(std::move(row));

        if (p.visibleAtLoad) ++out.room.propsVisibleAtLoad;
        else                 ++out.room.propsHiddenAtLoad;
        if (p.texture >= 0)  ++out.room.propsWithTexture;
        if (hasTarget)       ++out.room.propsWithTarget;

        // Hidden by the scene and with no node in the tree: uploaded and
        // unreachable. Either a join is wrong or the prop should not be here.
        if (!p.visibleAtLoad && !hasTarget) {
            ++out.room.propsStranded;
            Name(out.room.strandedNames, p.nodePath);
        }
    }

    // --- animation ----------------------------------------------------------
    out.animation = AnimationCensus{};
    out.anims.clear();
    std::vector<std::string> unresolvedSeen;
    out.animation.players = static_cast<int>(lib.Players().size());

    for (const assets::AnimationPlayerData& pl : lib.Players()) {
        for (const assets::Animation& a : pl.animations) {
            ++out.animation.animations;

            AnimRow row;
            row.playerPath = pl.path;
            row.name       = a.name;
            row.length     = a.length;
            row.tracks     = static_cast<int>(a.tracks.size());

            for (const assets::Track& t : a.tracks) {
                ++out.animation.tracks;
                if (t.property == "visible") {
                    ++out.animation.visibleTracks;
                    ++row.visibleTracks;
                }

                // METHOD and AUDIO tracks address no node, so they are not
                // "unresolved" when they fail to find one.
                if (t.property.empty()) continue;

                const std::string full =
                    assets::ResolveNodePath(pl.root, t.nodePath);
                if (tree.get_node(full.empty() ? "." : full)) {
                    ++out.animation.tracksBound;
                    ++row.bound;
                } else {
                    ++out.animation.tracksUnresolved;
                    if (std::find(unresolvedSeen.begin(), unresolvedSeen.end(),
                                  full) == unresolvedSeen.end()) {
                        unresolvedSeen.push_back(full);
                        Name(out.animation.unresolvedPaths, full);
                    }
                }
            }
            out.anims.push_back(std::move(row));
        }
    }

    out.animation.unresolvedNodes = static_cast<int>(unresolvedSeen.size());

    // --- ui -----------------------------------------------------------------
    out.ui = UiCensus{};
    if (ui) {
        out.ui.controls = static_cast<int>(ui->Controls().size());
        out.ui.textures = static_cast<int>(ui->Textures().size());
        for (const std::string& prefix : screenPrefixes) {
            ScreenCensus sc;
            sc.prefix = prefix;
            for (int i : ui->FindByPrefix(prefix)) {
                const assets::UiControl& c =
                    ui->Controls()[static_cast<std::size_t>(i)];
                ++sc.controls;
                if (c.CanFocus() && c.isActive) ++sc.focusable;
                if (!c.fontKey.empty() && !c.text.empty()) ++sc.labels;
                if (c.texture >= 0) ++sc.textures;
            }
            out.ui.screens.push_back(std::move(sc));
        }
    }
    if (fonts) {
        out.ui.fontFaces = static_cast<int>(fonts->Faces().size());
        for (const assets::FontFace& f : fonts->Faces()) {
            for (const assets::Glyph& g : f.glyphs) {
                if (g.present) ++out.ui.glyphs;
            }
        }
        out.ui.atlasBytes = static_cast<int>(fonts->Pixels().size());
    }
}

}  // namespace debug
