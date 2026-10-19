// Screen pixels -> the 960x540 viewport the game is laid out in.
//
// Transcribed from Godot 4.1.4-stable scene/main/window.cpp
// Window::_update_viewport_size (:987), stretch mode `viewport`, aspect `keep`,
// as project.godot sets. For a window wider in aspect than 960x540
// (viewport_aspect > video_mode_aspect is false there; the game's own case is
// the opposite branch at :1030):
//   viewport_size = 960x540
//   screen_size.x = video_mode.x, screen_size.y = video_mode.x / (960/540)
//   both floored (:1046)
//   margin.y = round((video_mode.y - screen_size.y) / 2) (:1056)
// The Vita's 960x544 screen therefore gets a 2-pixel bar top and bottom, and a
// 1920x1080 window scales by exactly 2 with no bars.
#include "platform/viewport_map.h"

#include "godot/input_event.h"
#include "platform/touch_tracker.h"

#include <gtest/gtest.h>

#include <vector>

TEST(ViewportMap, VitaScreenHasTwoPixelBars) {
    const platform::ViewportMap m = platform::FitKeepAspect(960, 544);
    EXPECT_FLOAT_EQ(m.scale, 1.0f);
    EXPECT_FLOAT_EQ(m.offsetX, 0.0f);
    EXPECT_FLOAT_EQ(m.offsetY, 2.0f);
    const godot::Vector2 p = platform::ToViewport(m, 453, 177);
    EXPECT_FLOAT_EQ(p.x, 453.0f);
    EXPECT_FLOAT_EQ(p.y, 175.0f);
}

TEST(ViewportMap, DesktopWindowScalesDown) {
    const platform::ViewportMap m = platform::FitKeepAspect(1920, 1080);
    EXPECT_FLOAT_EQ(m.scale, 2.0f);
    EXPECT_FLOAT_EQ(m.offsetX, 0.0f);
    EXPECT_FLOAT_EQ(m.offsetY, 0.0f);
    const godot::Vector2 p = platform::ToViewport(m, 906, 350);
    EXPECT_FLOAT_EQ(p.x, 453.0f);
    EXPECT_FLOAT_EQ(p.y, 175.0f);
}

// A window TALLER in aspect than the viewport gets bars left and right
// instead: screen_size.y = video_mode.y, screen_size.x = 540 * (960/540).
TEST(ViewportMap, ATallWindowGetsSideBars) {
    const platform::ViewportMap m = platform::FitKeepAspect(1000, 540);
    EXPECT_FLOAT_EQ(m.scale, 1.0f);
    EXPECT_FLOAT_EQ(m.offsetX, 20.0f);
    EXPECT_FLOAT_EQ(m.offsetY, 0.0f);
    const godot::Vector2 p = platform::ToViewport(m, 500, 270);
    EXPECT_FLOAT_EQ(p.x, 480.0f);
    EXPECT_FLOAT_EQ(p.y, 270.0f);
}

// sceTouch reports the front panel at twice the screen resolution
// (1920x1088 for 960x544), so the Vita halves before mapping.
TEST(ViewportMap, VitaFrontPanelIsTwiceScreen) {
    const platform::ViewportMap m = platform::FitKeepAspect(960, 544);
    const godot::Vector2 p = platform::ToViewport(m, 906 / 2.0f, 354 / 2.0f);
    EXPECT_FLOAT_EQ(p.x, 453.0f);
    EXPECT_FLOAT_EQ(p.y, 175.0f);
}

// What is DRAWN must use the same fit as what is HIT, or the cursor and the
// thing under it drift apart. Both the 3D composite and the 2D interface place
// themselves with this rect.
TEST(ViewportMap, TheDrawnRectIsTheSameFit) {
    const platform::ViewportRect vita = platform::ViewportRectFor(960, 544);
    EXPECT_FLOAT_EQ(vita.x, 0.0f);
    EXPECT_FLOAT_EQ(vita.y, 2.0f);
    EXPECT_FLOAT_EQ(vita.width, 960.0f);
    EXPECT_FLOAT_EQ(vita.height, 540.0f);

    const platform::ViewportRect big = platform::ViewportRectFor(1920, 1080);
    EXPECT_FLOAT_EQ(big.x, 0.0f);
    EXPECT_FLOAT_EQ(big.y, 0.0f);
    EXPECT_FLOAT_EQ(big.width, 1920.0f);
    EXPECT_FLOAT_EQ(big.height, 1080.0f);

    const platform::ViewportRect wide = platform::ViewportRectFor(1000, 540);
    EXPECT_FLOAT_EQ(wide.x, 20.0f);
    EXPECT_FLOAT_EQ(wide.width, 960.0f);

    // A point at the rect's top-left corner is viewport (0,0).
    const platform::ViewportMap m = platform::FitKeepAspect(960, 544);
    const godot::Vector2 corner = platform::ToViewport(m, vita.x, vita.y);
    EXPECT_FLOAT_EQ(corner.x, 0.0f);
    EXPECT_FLOAT_EQ(corner.y, 0.0f);
}

// The Vita reports every touch every frame; Godot's events are edges. The
// tracker turns one into the other: a new id presses, a moved id drags, an id
// that stopped being reported releases at its last position.
TEST(TouchTracker, ReportsBecomePressDragAndRelease) {
    const platform::ViewportMap m = platform::FitKeepAspect(960, 544);
    platform::TouchTracker tracker;
    std::vector<godot::InputEventRef> events;

    // Panel coordinates are twice the screen's (1920x1088 for 960x544).
    const platform::TouchReport first[] = {{3, 906.0f, 354.0f}};
    tracker.Update(first, 1, m, events);
    ASSERT_EQ(events.size(), 1u);
    const auto* press = godot::event_cast<godot::InputEventScreenTouch>(events[0].get());
    ASSERT_NE(press, nullptr);
    EXPECT_TRUE(press->pressed);
    EXPECT_EQ(press->index, 3);
    EXPECT_FLOAT_EQ(press->position.x, 453.0f);
    EXPECT_FLOAT_EQ(press->position.y, 175.0f);

    // Held and moved: a drag, with the move in viewport units.
    events.clear();
    const platform::TouchReport moved[] = {{3, 916.0f, 354.0f}};
    tracker.Update(moved, 1, m, events);
    ASSERT_EQ(events.size(), 1u);
    const auto* drag = godot::event_cast<godot::InputEventScreenDrag>(events[0].get());
    ASSERT_NE(drag, nullptr);
    EXPECT_EQ(drag->index, 3);
    EXPECT_FLOAT_EQ(drag->position.x, 458.0f);
    EXPECT_FLOAT_EQ(drag->relative.x, 5.0f);

    // Held and still: nothing at all.
    events.clear();
    tracker.Update(moved, 1, m, events);
    EXPECT_TRUE(events.empty());

    // Lifted: released where it was last seen.
    events.clear();
    tracker.Update(nullptr, 0, m, events);
    ASSERT_EQ(events.size(), 1u);
    const auto* release = godot::event_cast<godot::InputEventScreenTouch>(events[0].get());
    ASSERT_NE(release, nullptr);
    EXPECT_FALSE(release->pressed);
    EXPECT_EQ(release->index, 3);
    EXPECT_FLOAT_EQ(release->position.x, 458.0f);
}
