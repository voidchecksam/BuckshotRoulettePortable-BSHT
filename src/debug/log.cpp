#include "debug/log.h"

#include <cstdarg>
#include <cstdio>

namespace debug {

namespace {

// Finds the row the action refers to, so the log can say what the thing WAS as
// well as what was done to it. Returns nullptr when the action names something
// the state does not contain, which is itself worth logging.
const AnimRow* FindAnim(const DebugState& s, const std::string& name) {
    for (const AnimRow& r : s.anims) {
        if (r.name == name) return &r;
    }
    return nullptr;
}

const PropRow* FindProp(const DebugState& s, const std::string& path) {
    for (const PropRow& r : s.props) {
        if (r.path == path) return &r;
    }
    return nullptr;
}

void Emit(const LogSink& sink, const char* fmt, ...) {
    if (!sink) return;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    sink(std::string(buf));
}

}  // namespace

void LogOverlayToggle(const LogSink& sink, bool nowVisible, Page page) {
    Emit(sink, "  debug: overlay %s on page %s",
         nowVisible ? "OPENED" : "closed", PageName(page));
}

void LogAction(const LogSink& sink, const OverlayAction& action,
               const DebugState& s, bool applied) {
    switch (action.kind) {
        case OverlayAction::Kind::None:
            return;

        case OverlayAction::Kind::PlayAnimation: {
            const AnimRow* r = FindAnim(s, action.name);
            if (!r) {
                // The overlay only offers animations from the state, so this
                // means the state moved underneath it.
                Emit(sink, "  debug: play '%s' -- NOT IN THE CENSUS",
                     action.name.c_str());
                return;
            }
            // Bound-track count is the number that says whether playing this
            // could possibly have looked like anything.
            Emit(sink, "  debug: play '%s' %s -- %d/%d tracks bound, "
                       "%d visible tracks, %.2fs",
                 r->name.c_str(), applied ? "started" : "REFUSED",
                 r->bound, r->tracks, r->visibleTracks,
                 static_cast<double>(r->length));
            if (r->bound == 0) {
                Emit(sink, "         ^^ binds NOTHING in this room; it cannot "
                           "move anything on screen");
            }
            return;
        }

        case OverlayAction::Kind::TogglePropOverride: {
            const PropRow* r = FindProp(s, action.nodePath);
            if (!r) {
                Emit(sink, "  debug: force '%s' -- NOT IN THE CENSUS",
                     action.nodePath.c_str());
                return;
            }
            Emit(sink, "  debug: force %s '%s' %s",
                 action.visible ? "VISIBLE" : "hidden", r->path.c_str(),
                 applied ? "ok" : "NO SUCH PROP IN THE RENDERER");
            // The distinction the operator is usually chasing: a prop that was
            // already visible and still cannot be seen is not a visibility
            // problem, it is a placement or a geometry one.
            Emit(sink, "         was %s at load, %s animation target, "
                       "%s texture, %d tris",
                 r->visibleAtLoad ? "VISIBLE" : "hidden",
                 r->hasTarget ? "has an" : "has NO",
                 r->hasTexture ? "has a" : "has NO", r->triangles);
            return;
        }

        case OverlayAction::Kind::OpenScreen: {
            if (action.screen < 0 ||
                action.screen >= static_cast<int>(s.ui.screens.size())) {
                Emit(sink, "  debug: open screen %d -- out of range",
                     action.screen);
                return;
            }
            const ScreenCensus& c =
                s.ui.screens[static_cast<std::size_t>(action.screen)];
            Emit(sink, "  debug: open '%s' %s -- %d controls, %d focusable, "
                       "%d labels",
                 c.prefix.c_str(), applied ? "ok" : "FAILED", c.controls,
                 c.focusable, c.labels);
            return;
        }
    }
}

}  // namespace debug
