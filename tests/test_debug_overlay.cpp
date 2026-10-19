// The overlay must never be able to take the game down with it.
//
// A debug view that crashes makes the bug being chased indistinguishable from
// the tool chasing it, and on a device where each attempt costs a round trip
// that is worse than no tool at all. Everything here is about bounds: an empty
// state, a state that shrinks underneath the cursor, a cursor driven past both
// ends.
//
// Drawing needs GL and is not tested here. Input handling and action selection
// do not, and they are where the indices live.
#include "debug/log.h"
#include "debug/overlay.h"
#include "debug/state.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

platform::FrameInput Toggle() {
    platform::FrameInput in{};
    in.toggleDebug = true;
    return in;
}

platform::FrameInput Nav(bool down = false, bool up = false,
                         bool left = false, bool right = false) {
    platform::FrameInput in{};
    in.navDown = down;
    in.navUp = up;
    in.navLeft = left;
    in.navRight = right;
    return in;
}

platform::FrameInput Confirm() {
    platform::FrameInput in{};
    in.confirm = true;
    return in;
}

platform::FrameInput Page(bool next) {
    platform::FrameInput in{};
    in.debugNext = next;
    in.debugPrev = !next;
    return in;
}

debug::DebugState Populated(int props, int anims, int screens) {
    debug::DebugState s;
    for (int i = 0; i < props; ++i) {
        debug::PropRow r;
        r.path = "prop/" + std::to_string(i);
        r.hasTarget = (i % 2) == 0;
        s.props.push_back(r);
    }
    for (int i = 0; i < anims; ++i) {
        debug::AnimRow r;
        r.name = "anim " + std::to_string(i);
        r.playerPath = "player/" + std::to_string(i);
        r.tracks = 10;
        r.bound = i;
        s.anims.push_back(r);
    }
    for (int i = 0; i < screens; ++i) {
        debug::ScreenCensus c;
        c.prefix = "screen/" + std::to_string(i);
        c.controls = 3;
        s.ui.screens.push_back(c);
    }
    s.room.props = props;
    return s;
}

TEST(DebugOverlay, StartsClosedAndDoesNothing) {
    debug::Overlay o;
    const debug::DebugState s = Populated(10, 10, 3);
    EXPECT_FALSE(o.Visible());

    // Every button while closed must be inert, or the overlay steers the game.
    for (const platform::FrameInput& in : {Nav(true), Confirm(), Page(true)}) {
        const debug::OverlayAction a = o.Handle(in, s);
        EXPECT_EQ(debug::OverlayAction::Kind::None, a.kind);
        EXPECT_FALSE(o.Visible());
    }
}

TEST(DebugOverlay, TogglesOpenAndShut) {
    debug::Overlay o;
    const debug::DebugState s = Populated(4, 4, 1);
    o.Handle(Toggle(), s);
    EXPECT_TRUE(o.Visible());
    o.Handle(Toggle(), s);
    EXPECT_FALSE(o.Visible());
}

TEST(DebugOverlay, SurvivesACompletelyEmptyState) {
    // The state is empty before the first gather, and gathering can fail.
    debug::Overlay o;
    const debug::DebugState empty;
    o.Handle(Toggle(), empty);
    ASSERT_TRUE(o.Visible());

    for (int i = 0; i < static_cast<int>(debug::Page::Count) + 2; ++i) {
        for (const platform::FrameInput& in :
             {Nav(true), Nav(false, true), Nav(false, false, true),
              Nav(false, false, false, true), Confirm()}) {
            const debug::OverlayAction a = o.Handle(in, empty);
            EXPECT_EQ(debug::OverlayAction::Kind::None, a.kind);
        }
        o.Handle(Page(true), empty);
    }
    EXPECT_GE(o.Cursor(), 0);
}

TEST(DebugOverlay, CyclesEveryPageInBothDirections) {
    debug::Overlay o;
    const debug::DebugState s = Populated(5, 5, 2);
    o.Handle(Toggle(), s);

    const int pages = static_cast<int>(debug::Page::Count);
    for (int i = 0; i < pages * 3; ++i) {
        o.Handle(Page(true), s);
        EXPECT_LT(static_cast<int>(o.CurrentPage()), pages);
        EXPECT_GE(static_cast<int>(o.CurrentPage()), 0);
    }
    for (int i = 0; i < pages * 3; ++i) {
        o.Handle(Page(false), s);
        EXPECT_LT(static_cast<int>(o.CurrentPage()), pages);
        EXPECT_GE(static_cast<int>(o.CurrentPage()), 0);
    }
}

TEST(DebugOverlay, TheCursorCannotLeaveTheList) {
    debug::Overlay o;
    const debug::DebugState s = Populated(3, 3, 1);
    o.Handle(Toggle(), s);
    o.Handle(Page(true), s);          // Props

    for (int i = 0; i < 50; ++i) o.Handle(Nav(true), s);
    EXPECT_LT(o.Cursor(), static_cast<int>(s.props.size()));
    for (int i = 0; i < 50; ++i) o.Handle(Nav(false, true), s);
    EXPECT_GE(o.Cursor(), 0);

    // Page jumps too, which move further than one row.
    for (int i = 0; i < 20; ++i) o.Handle(Nav(false, false, false, true), s);
    EXPECT_LT(o.Cursor(), static_cast<int>(s.props.size()));
    for (int i = 0; i < 20; ++i) o.Handle(Nav(false, false, true), s);
    EXPECT_GE(o.Cursor(), 0);
}

TEST(DebugOverlay, ActionsNameSomethingTheStateContains) {
    debug::Overlay o;
    const debug::DebugState s = Populated(6, 6, 2);
    o.Handle(Toggle(), s);

    o.Handle(Page(true), s);          // Props
    for (int i = 0; i < 4; ++i) o.Handle(Nav(true), s);
    debug::OverlayAction a = o.Handle(Confirm(), s);
    ASSERT_EQ(debug::OverlayAction::Kind::TogglePropOverride, a.kind);
    EXPECT_EQ(s.props[static_cast<std::size_t>(o.Cursor())].path, a.nodePath);

    o.Handle(Page(true), s);          // Animation
    o.Handle(Nav(true), s);
    a = o.Handle(Confirm(), s);
    ASSERT_EQ(debug::OverlayAction::Kind::PlayAnimation, a.kind);
    EXPECT_EQ(s.anims[static_cast<std::size_t>(o.Cursor())].name, a.name);

    o.Handle(Page(true), s);          // Screens
    a = o.Handle(Confirm(), s);
    ASSERT_EQ(debug::OverlayAction::Kind::OpenScreen, a.kind);
    EXPECT_GE(a.screen, 0);
    EXPECT_LT(a.screen, static_cast<int>(s.ui.screens.size()));
}

TEST(DebugOverlay, AStateThatShrinksUnderTheCursorIsSafe) {
    // The census can be re-gathered while the overlay is open. A cursor left
    // pointing past the new end must do nothing, not read past the vector.
    debug::Overlay o;
    const debug::DebugState big = Populated(40, 40, 5);
    o.Handle(Toggle(), big);
    o.Handle(Page(true), big);        // Props
    for (int i = 0; i < 30; ++i) o.Handle(Nav(true), big);
    ASSERT_GT(o.Cursor(), 2);

    const debug::DebugState small = Populated(2, 2, 1);
    const debug::OverlayAction a = o.Handle(Confirm(), small);
    if (a.kind != debug::OverlayAction::Kind::None) {
        EXPECT_EQ(debug::OverlayAction::Kind::TogglePropOverride, a.kind);
        EXPECT_LT(o.Cursor(), static_cast<int>(small.props.size()));
    }
}

TEST(DebugOverlay, SummaryOffersNoAction) {
    // It is a readout. Pressing confirm on it must not do something arbitrary.
    debug::Overlay o;
    const debug::DebugState s = Populated(5, 5, 2);
    o.Handle(Toggle(), s);
    ASSERT_EQ(debug::Page::Summary, o.CurrentPage());
    EXPECT_EQ(debug::OverlayAction::Kind::None, o.Handle(Confirm(), s).kind);
}

TEST(DebugOverlay, OpeningResetsTheCursor) {
    // Reopening onto a row the last gather no longer has would be a stale read.
    debug::Overlay o;
    const debug::DebugState s = Populated(20, 20, 3);
    o.Handle(Toggle(), s);
    o.Handle(Page(true), s);
    for (int i = 0; i < 10; ++i) o.Handle(Nav(true), s);
    ASSERT_GT(o.Cursor(), 0);

    o.Handle(Toggle(), s);            // close
    o.Handle(Toggle(), s);            // open
    EXPECT_EQ(0, o.Cursor());
}

TEST(DebugOverlay, EveryPageHasAName) {
    for (int i = 0; i < static_cast<int>(debug::Page::Count); ++i) {
        const char* n = debug::PageName(static_cast<debug::Page>(i));
        ASSERT_NE(nullptr, n);
        EXPECT_NE(std::string("?"), std::string(n));
    }
}

// ---------------------------------------------------------------------------
// The log, which is what makes a run survive the trip home.

TEST(DebugLog, RecordsWhatWasPlayedAndWhatItBinds) {
    // "played 'dealer use beer'" is worth little; that it bound 17 of 22 tracks
    // is worth a round trip.
    const debug::DebugState s = Populated(2, 3, 1);
    std::vector<std::string> lines;
    const debug::LogSink sink = [&lines](const std::string& l) {
        lines.push_back(l);
    };

    debug::OverlayAction a;
    a.kind = debug::OverlayAction::Kind::PlayAnimation;
    a.name = "anim 2";
    a.playerPath = "player/2";
    debug::LogAction(sink, a, s, /*applied=*/true);

    ASSERT_FALSE(lines.empty());
    EXPECT_NE(lines[0].find("anim 2"), std::string::npos);
    EXPECT_NE(lines[0].find("tracks bound"), std::string::npos);
}

TEST(DebugLog, CallsOutAnAnimationThatBindsNothing) {
    // The single most useful thing the log can say: this could not possibly
    // have looked like anything.
    debug::DebugState s = Populated(1, 1, 1);
    s.anims[0].bound = 0;
    std::vector<std::string> lines;
    const debug::LogSink sink = [&lines](const std::string& l) {
        lines.push_back(l);
    };

    debug::OverlayAction a;
    a.kind = debug::OverlayAction::Kind::PlayAnimation;
    a.name = s.anims[0].name;
    debug::LogAction(sink, a, s, true);

    bool warned = false;
    for (const std::string& l : lines) {
        if (l.find("binds NOTHING") != std::string::npos) warned = true;
    }
    EXPECT_TRUE(warned);
}

TEST(DebugLog, RecordsWhatAForcedPropWasBefore) {
    // A prop that was already visible and still cannot be seen is not a
    // visibility problem; the log has to make that distinguishable.
    debug::DebugState s = Populated(2, 1, 1);
    s.props[1].visibleAtLoad = true;
    s.props[1].hasTexture = false;
    std::vector<std::string> lines;
    const debug::LogSink sink = [&lines](const std::string& l) {
        lines.push_back(l);
    };

    debug::OverlayAction a;
    a.kind = debug::OverlayAction::Kind::TogglePropOverride;
    a.nodePath = s.props[1].path;
    a.visible = true;
    debug::LogAction(sink, a, s, true);

    bool sawState = false;
    for (const std::string& l : lines) {
        if (l.find("was VISIBLE at load") != std::string::npos &&
            l.find("has NO texture") != std::string::npos) {
            sawState = true;
        }
    }
    EXPECT_TRUE(sawState) << "the log did not say what the prop was";
}

TEST(DebugLog, SaysSoWhenAnActionNamesSomethingAbsent) {
    const debug::DebugState s = Populated(1, 1, 1);
    std::vector<std::string> lines;
    const debug::LogSink sink = [&lines](const std::string& l) {
        lines.push_back(l);
    };

    debug::OverlayAction a;
    a.kind = debug::OverlayAction::Kind::PlayAnimation;
    a.name = "no such animation";
    debug::LogAction(sink, a, s, false);

    ASSERT_FALSE(lines.empty());
    EXPECT_NE(lines[0].find("NOT IN THE CENSUS"), std::string::npos);
}

TEST(DebugLog, ANullSinkIsHarmless) {
    // The host may not have a report open yet.
    const debug::DebugState s = Populated(1, 1, 1);
    debug::OverlayAction a;
    a.kind = debug::OverlayAction::Kind::PlayAnimation;
    a.name = s.anims[0].name;
    debug::LogAction(nullptr, a, s, true);
    debug::LogOverlayToggle(nullptr, true, debug::Page::Summary);
    SUCCEED();
}

TEST(DebugOverlay, ForcingAPropTogglesRatherThanRepeating) {
    // Confirm on a forced prop turns it back off rather than logging "force
    // VISIBLE" on the same prop again, which would say nothing about whether
    // it appeared and could not be undone.
    debug::Overlay o;
    const debug::DebugState s = Populated(4, 1, 1);
    o.Handle(Toggle(), s);
    o.Handle(Page(true), s);          // Props

    const debug::OverlayAction on = o.Handle(Confirm(), s);
    ASSERT_EQ(debug::OverlayAction::Kind::TogglePropOverride, on.kind);
    EXPECT_TRUE(on.visible);

    const debug::OverlayAction off = o.Handle(Confirm(), s);
    ASSERT_EQ(debug::OverlayAction::Kind::TogglePropOverride, off.kind);
    EXPECT_EQ(on.nodePath, off.nodePath);
    EXPECT_FALSE(off.visible) << "a second press must turn it back off";

    const debug::OverlayAction again = o.Handle(Confirm(), s);
    EXPECT_TRUE(again.visible) << "a third press must turn it on again";
}

TEST(DebugOverlay, EachPropTogglesIndependently) {
    debug::Overlay o;
    const debug::DebugState s = Populated(4, 1, 1);
    o.Handle(Toggle(), s);
    o.Handle(Page(true), s);

    const debug::OverlayAction first = o.Handle(Confirm(), s);
    ASSERT_TRUE(first.visible);

    o.Handle(Nav(true), s);
    const debug::OverlayAction second = o.Handle(Confirm(), s);
    EXPECT_NE(first.nodePath, second.nodePath);
    EXPECT_TRUE(second.visible) << "a different prop starts from off";
}

}  // namespace
