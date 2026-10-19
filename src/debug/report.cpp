#include "debug/report.h"

#include <cstdarg>
#include <cstdio>

namespace debug {

namespace {

// One place that appends a formatted line, so no caller has to remember the
// buffer size or the newline.
void Line(std::string& out, const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    out += buf;
    out += '\n';
}

void NameList(std::string& out, const char* label,
              const std::vector<std::string>& names, int total) {
    if (names.empty()) return;
    Line(out, "    %s (%d total, first %lu):", label, total,
         static_cast<unsigned long>(names.size()));
    for (const std::string& n : names) {
        Line(out, "      %s", n.c_str());
    }
    if (total > static_cast<int>(names.size())) {
        // Say what was dropped. A truncated list that does not admit it reads
        // as a complete one.
        Line(out, "      ... and %d more not listed",
             total - static_cast<int>(names.size()));
    }
}

}  // namespace

std::string FormatCensus(const DebugState& s) {
    std::string out;

    Line(out, "");
    Line(out, "================ CENSUS ================");

    // --- room ---------------------------------------------------------------
    Line(out, "room     %d batches, %d props, %d triangles",
         s.room.batches, s.room.props, s.room.triangles);
    Line(out, "         %d textures, %.2f MB", s.room.textures,
         static_cast<double>(s.room.textureBytes) / 1048576.0);
    Line(out, "props    %d visible at load, %d hidden at load",
         s.room.propsVisibleAtLoad, s.room.propsHiddenAtLoad);
    Line(out, "         %d have a texture, %d have an animation target",
         s.room.propsWithTexture, s.room.propsWithTarget);

    // The number that says whether the room can come alive. A prop the scene
    // hides and no animation can reach is uploaded and unreachable.
    Line(out, "         %d STRANDED (hidden at load, no animation can show them)",
         s.room.propsStranded);
    NameList(out, "stranded", s.room.strandedNames, s.room.propsStranded);

    // --- animation ----------------------------------------------------------
    Line(out, "");
    Line(out, "anim     %d players, %d animations, %d tracks",
         s.animation.players, s.animation.animations, s.animation.tracks);
    Line(out, "         %d tracks bound, %d UNRESOLVED across %d distinct nodes",
         s.animation.tracksBound, s.animation.tracksUnresolved,
         s.animation.unresolvedNodes);
    NameList(out, "unresolved nodes", s.animation.unresolvedPaths,
             s.animation.unresolvedNodes);
    Line(out, "         %d `visible` tracks -- the property that decides "
              "whether the room moves", s.animation.visibleTracks);

    // --- ui -----------------------------------------------------------------
    Line(out, "");
    Line(out, "ui       %d controls, %d textures", s.ui.controls, s.ui.textures);
    Line(out, "fonts    %d faces, %d glyphs, %.2f MB atlas", s.ui.fontFaces,
         s.ui.glyphs, static_cast<double>(s.ui.atlasBytes) / 1048576.0);
    if (s.ui.screens.empty()) {
        Line(out, "screens  none were asked about");
    }
    for (const ScreenCensus& sc : s.ui.screens) {
        Line(out, "screen   %-46s %3d controls %2d focusable %2d labels %2d tex",
             sc.prefix.c_str(), sc.controls, sc.focusable, sc.labels,
             sc.textures);
        if (!sc.Exists()) {
            // A prefix that matches nothing is a typo or a re-export that moved
            // the subtree; either way it is not "an empty screen".
            Line(out, "         ^^ MATCHES NOTHING -- wrong prefix?");
        }
    }

    Line(out, "========================================");
    return out;
}

std::string FormatFrame(const FrameCensus& f) {
    std::string out;
    Line(out, "frame    %.1f fps (%.2f ms, worst %.2f)", f.fps, f.frameMs,
         f.worstMs);
    Line(out, "draw     %d/%d batches, %d/%d triangles, %d props hidden, "
              "%d texture binds",
         f.drawnBatches, f.totalBatches, f.drawnTris, f.totalTris,
         f.hiddenProps, f.textureBinds);
    Line(out, "ui       %d quads in %d draw calls", f.uiQuads, f.uiDrawCalls);
    Line(out, "anim     playing=%d t=%.2f  %d moving now, %d/%d ever animated, "
              "%d played, %d missing",
         f.animPlaying ? 1 : 0, f.animTime, f.movingTargets, f.everAnimated,
         f.totalTargets, f.animsPlayed, f.missingAnims);
    return out;
}

}  // namespace debug
