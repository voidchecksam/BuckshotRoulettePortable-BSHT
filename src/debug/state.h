// What the game is doing, gathered once and shared.
//
// One census, gathered from the live systems, formatted into the report and
// drawn by the on-device overlay. Both read the same struct, so they cannot
// disagree about what the game is doing.
//
// No GL and no SDL. Gathering and formatting are pure, so the census can be
// asserted against the real room in a headless test. Only Overlay draws.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace assets {
class Room;
class AnimationLibrary;
class UiLayout;
class FontAtlas;
}  // namespace assets

namespace game {
class UiScreen;
}  // namespace game

namespace godot {
class SceneTree;
}  // namespace godot

namespace debug {

// How many offenders to name per section. Enough to see the pattern; a report
// with 370 paths in it is one nobody reads.
inline constexpr int kMaxNamed = 12;

struct RoomCensus {
    int batches      = 0;
    int props        = 0;
    int triangles    = 0;
    int textures     = 0;
    int textureBytes = 0;

    int propsVisibleAtLoad = 0;
    int propsHiddenAtLoad  = 0;
    int propsWithTexture   = 0;
    int propsWithTarget    = 0;

    // Props hidden at load whose node is not in the tree: uploaded and
    // unreachable, which hints that a join is wrong.
    int propsStranded = 0;
    std::vector<std::string> strandedNames;
};

struct AnimationCensus {
    int players    = 0;
    int animations = 0;
    int tracks     = 0;

    // Resolved against the room's node table by the SAME lookup the animator
    // uses, not by a script that can be wrong in its own way. A throwaway
    // analysis put this at 370 and could not say whether that was real.
    int tracksBound      = 0;
    int tracksUnresolved = 0;

    // DISTINCT nodes behind those tracks. 281 unresolved tracks sounds like a
    // broken system; 20 distinct nodes, all of them Camera or 2D UI that the
    // room deliberately does not contain, is a different fact entirely. The
    // track count alone cannot tell those apart.
    int unresolvedNodes  = 0;
    std::vector<std::string> unresolvedPaths;

    // Tracks that would drive a property the runtime cannot apply.
    int tracksUnsupported = 0;
    std::vector<std::string> unsupportedProps;

    // `visible` tracks specifically: the property that decides whether the room
    // looks alive at all.
    int visibleTracks = 0;
};

struct ScreenCensus {
    std::string prefix;
    int controls  = 0;
    int focusable = 0;
    int labels    = 0;
    int textures  = 0;

    // A screen with no focusable control is legitimate (dialogue, prompts).
    // A screen with no controls at all means the prefix is wrong.
    bool Exists() const { return controls > 0; }
};

struct UiCensus {
    int controls   = 0;
    int textures   = 0;
    int fontFaces  = 0;
    int glyphs     = 0;
    int atlasBytes = 0;
    std::vector<ScreenCensus> screens;
};

// Sampled every frame rather than gathered once.
struct FrameCensus {
    float fps          = 0.0f;
    float frameMs      = 0.0f;
    float worstMs      = 0.0f;
    int   drawnBatches = 0;
    int   totalBatches = 0;
    int   drawnTris    = 0;
    int   totalTris    = 0;
    int   hiddenProps  = 0;
    int   textureBinds = 0;
    int   uiQuads      = 0;
    int   uiDrawCalls  = 0;

    // Live animator state, so "is anything moving?" is answerable at a glance.
    bool  animPlaying   = false;
    float animTime      = 0.0f;
    // Two different questions: a cumulative count read early in a run looks
    // like nothing is animating.
    int   movingTargets = 0;    // written THIS frame
    int   everAnimated  = 0;    // written at any point this run
    int   totalTargets  = 0;
    int   animsPlayed   = 0;
    int   missingAnims  = 0;
};

// One prop, for the browser page. Kept flat and small: this list is 215 entries
// and is walked to draw the page.
struct PropRow {
    std::string path;
    bool  visibleAtLoad = false;
    bool  hasTarget     = false;
    bool  hasTexture    = false;
    int   triangles     = 0;
};

// One animation, for the page that plays them by hand.
struct AnimRow {
    std::string playerPath;
    std::string name;
    int   tracks        = 0;
    int   bound         = 0;
    int   visibleTracks = 0;
    float length        = 0.0f;
};

struct DebugState {
    RoomCensus      room;
    AnimationCensus animation;
    UiCensus        ui;
    FrameCensus     frame;

    std::vector<PropRow> props;
    std::vector<AnimRow> anims;

    // Build identity, so a stale report is obvious on line 1.
    std::string buildId;
};

// The static half: everything knowable before the first frame.
//
// `tree` supplies the nodes, which makes the unresolved count trustworthy: it
// is the lookup AnimationPlayer itself binds tracks with.
void GatherStatic(DebugState& out,
                  const assets::Room& room,
                  const assets::AnimationLibrary& lib,
                  const godot::SceneTree& tree,
                  const assets::UiLayout* ui,
                  const assets::FontAtlas* fonts,
                  const std::vector<std::string>& screenPrefixes);

}  // namespace debug
