// Action matching as Godot 4.1.4 does it: input_event.cpp InputEventKey::action_match,
// InputEventJoypadButton::action_match, InputEvent::is_action_pressed /
// is_action_released / is_pressed; input_map.cpp event_get_action_status.
#include "godot/input_event.h"
#include "godot/input_map.h"
#include "godot/audio_server.h"
#include "godot/math_funcs.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

godot::InputEventRef Key(int64_t code, bool pressed, bool echo = false) {
    auto k = std::make_shared<godot::InputEventKey>();
    k->keycode = code;
    k->pressed = pressed;
    k->echo = echo;
    return k;
}

godot::InputEventRef Pad(int button, bool pressed) {
    auto j = std::make_shared<godot::InputEventJoypadButton>();
    j->button_index = button;
    j->pressed = pressed;
    return j;
}

struct InputMapFixture : ::testing::Test {
    void SetUp() override {
        auto& m = godot::InputMap::get_singleton();
        m.clear();
        m.add_action("ui_accept");
        m.action_add_event("ui_accept", Key(godot::key::ENTER, false));
        m.action_add_event("ui_accept", Pad(godot::joy::A, false));
        m.add_action("ui_down");
        m.action_add_event("ui_down", Key(godot::key::DOWN, false));
        m.action_add_event("ui_down", Pad(godot::joy::DPAD_DOWN, false));
    }
};

}  // namespace

TEST_F(InputMapFixture, EnterAndPadAAreAcceptSpaceIsNot) {
    EXPECT_TRUE(Key(godot::key::ENTER, true)->is_action_pressed("ui_accept"));
    EXPECT_TRUE(Pad(godot::joy::A, true)->is_action_pressed("ui_accept"));
    EXPECT_FALSE(Key(32 /* KEY_SPACE */, true)->is_action_pressed("ui_accept"));
}

TEST_F(InputMapFixture, ReleaseIsActionButNotPressed) {
    const godot::InputEventRef up = Key(godot::key::ENTER, false);
    EXPECT_TRUE(up->is_action("ui_accept"));
    EXPECT_FALSE(up->is_action_pressed("ui_accept"));
    EXPECT_TRUE(up->is_action_released("ui_accept"));
}

TEST_F(InputMapFixture, EchoCountsOnlyWhenAllowed) {
    const godot::InputEventRef rep = Key(godot::key::DOWN, true, true);
    EXPECT_FALSE(rep->is_action_pressed("ui_down"));
    EXPECT_TRUE(rep->is_action_pressed("ui_down", true, true));
}

TEST_F(InputMapFixture, CanceledIsNeverPressed) {
    godot::InputEventRef k = Key(godot::key::ENTER, true);
    k->canceled = true;
    EXPECT_FALSE(k->is_pressed());
    EXPECT_FALSE(k->is_action_pressed("ui_accept"));
}

// input_map.cpp _find_event:134: a binding pinned to a device matches only that
// device; one on ALL_DEVICES (-1) matches any.
TEST(GodotInputMapDevice, PinnedBindingMatchesOnlyItsDevice) {
    auto& m = godot::InputMap::get_singleton();
    m.clear();
    m.add_action("pinned");
    godot::InputEventRef bound = Pad(godot::joy::A, false);
    bound->device = 0;
    m.action_add_event("pinned", bound);
    godot::InputEventRef onZero = Pad(godot::joy::A, true);
    godot::InputEventRef onOne = Pad(godot::joy::A, true);
    onOne->device = 1;
    EXPECT_TRUE(onZero->is_action_pressed("pinned"));
    EXPECT_FALSE(onOne->is_action_pressed("pinned"));
}

TEST_F(InputMapFixture, UnknownActionIsNotAnAction) {
    EXPECT_FALSE(Key(godot::key::ENTER, true)->is_action("no_such_action"));
}

TEST(GodotInputMap, LoadsTheProjectsOwnMap) {
    auto& m = godot::InputMap::get_singleton();
    m.clear();
    if (!m.load(BSHT_SOURCE_DIR "/assets/extracted/input_map.json")) {
        GTEST_SKIP() << "input_map.json not exported (tools/godot/export_input_map.gd)";
    }
    EXPECT_TRUE(Key(godot::key::ENTER, true)->is_action_pressed("ui_accept"));
    EXPECT_TRUE(Key(godot::key::BACKSPACE, true)->is_action_pressed("ui_cancel"));
    EXPECT_TRUE(Pad(godot::joy::DPAD_UP, true)->is_action_pressed("ui_up"));
    // START and Escape are "exit game"; reset is R and pad Y (input_map.json).
    EXPECT_TRUE(Pad(godot::joy::START, true)->is_action_pressed("exit game"));
    EXPECT_TRUE(Key(godot::key::ESCAPE, true)->is_action_pressed("exit game"));
    EXPECT_TRUE(Key(82 /* KEY_R */, true)->is_action_pressed("reset"));
    EXPECT_TRUE(Pad(godot::joy::Y, true)->is_action_pressed("reset"));
    EXPECT_FALSE(Pad(godot::joy::START, true)->is_action_pressed("reset"));
    // The original binds neither Space nor WASD.
    EXPECT_FALSE(Key(32, true)->is_action_pressed("ui_accept"));
    EXPECT_FALSE(Key(87 /* KEY_W */, true)->is_action_pressed("ui_up"));
}

// ---------------------------------------------------------------------------
// Input singleton (core/input/input.cpp at 4.1.4-stable).
#include "godot/input.h"

#include <vector>

namespace {

godot::InputEventRef Touch(int index, float x, float y, bool pressed) {
    auto t = std::make_shared<godot::InputEventScreenTouch>();
    t->index = index;
    t->position = {x, y};
    t->pressed = pressed;
    return t;
}

godot::InputEventRef Drag(int index, float x, float y) {
    auto d = std::make_shared<godot::InputEventScreenDrag>();
    d->index = index;
    d->position = {x, y};
    return d;
}

std::vector<godot::InputEventType> Kinds(const std::vector<godot::InputEventRef>& events) {
    std::vector<godot::InputEventType> out;
    for (const auto& e : events) out.push_back(e->type);
    return out;
}

}  // namespace

// input.cpp _parse_input_event_impl:603-670. A touch that claims the mouse is
// parsed as a LEFT mouse button at its position FIRST (from the emulation
// device), then the touch itself is dispatched at the end of the function. No
// motion precedes the press. A second finger never drives the mouse.
TEST(GodotInput, TouchIsPrecededByItsEmulatedMouseButton) {
    auto& in = godot::Input::get_singleton();
    in.reset();
    std::vector<godot::InputEventRef> got;
    in.set_dispatch([&](const godot::InputEventRef& e) { got.push_back(e); });
    in.parse_input_event(Touch(0, 453, 175, true));
    in.parse_input_event(Drag(0, 460, 180));
    in.parse_input_event(Touch(1, 10, 10, true));
    in.parse_input_event(Touch(0, 460, 180, false));
    in.flush_buffered_events();

    using T = godot::InputEventType;
    ASSERT_EQ(Kinds(got), (std::vector<T>{T::MouseButton, T::ScreenTouch, T::MouseMotion, T::ScreenDrag,
                                          T::ScreenTouch, T::MouseButton, T::ScreenTouch}));
    const auto* press = godot::event_cast<godot::InputEventMouseButton>(got[0].get());
    EXPECT_TRUE(press->is_pressed());
    EXPECT_EQ(press->button_index, godot::mouse::LEFT);
    EXPECT_EQ(press->device, godot::DEVICE_ID_EMULATION);
    EXPECT_FLOAT_EQ(press->position.x, 453.0f);
    EXPECT_FLOAT_EQ(press->position.y, 175.0f);
    EXPECT_EQ(press->button_mask, godot::mouse::MASK_LEFT);
    const auto* release = godot::event_cast<godot::InputEventMouseButton>(got[5].get());
    EXPECT_FALSE(release->is_pressed());
    EXPECT_EQ(release->button_mask, 0);
}

// input.cpp:307 is_action_just_pressed compares the press's process frame with
// Engine's process-frame count, which main.cpp:3467 increments after process.
// So a press flushed before a frame is "just pressed" throughout that frame.
TEST_F(InputMapFixture, JustPressedLastsTheFrameItWasFlushedBefore) {
    auto& in = godot::Input::get_singleton();
    in.reset();
    in.set_dispatch([](const godot::InputEventRef&) {});
    in.parse_input_event(Key(godot::key::ENTER, true));
    in.flush_buffered_events();
    EXPECT_TRUE(in.is_action_pressed("ui_accept"));
    EXPECT_TRUE(in.is_action_just_pressed("ui_accept"));
    in.end_process_frame();
    EXPECT_TRUE(in.is_action_pressed("ui_accept"));
    EXPECT_FALSE(in.is_action_just_pressed("ui_accept"));

    in.parse_input_event(Key(godot::key::ENTER, false));
    in.flush_buffered_events();
    EXPECT_FALSE(in.is_action_pressed("ui_accept"));
    EXPECT_TRUE(in.is_action_just_released("ui_accept"));
}

// input.cpp parse_input_event: with use_accumulated_input (default true,
// input.h:117) a motion merges into the buffered motion before it.
TEST(GodotInput, ConsecutiveMotionAccumulates) {
    auto& in = godot::Input::get_singleton();
    in.reset();
    std::vector<godot::InputEventRef> got;
    in.set_dispatch([&](const godot::InputEventRef& e) { got.push_back(e); });
    for (float x : {1.0f, 2.0f, 3.0f}) {
        auto m = std::make_shared<godot::InputEventMouseMotion>();
        m->position = {x, 0};
        m->global_position = {x, 0};
        m->relative = {1, 0};
        in.parse_input_event(m);
    }
    in.flush_buffered_events();
    ASSERT_EQ(got.size(), 1u);
    const auto* m = godot::event_cast<godot::InputEventMouseMotion>(got[0].get());
    EXPECT_FLOAT_EQ(m->position.x, 3.0f);
    EXPECT_FLOAT_EQ(m->relative.x, 3.0f);
}

TEST(GodotInput, MouseModeAndCustomCursorAreRecorded) {
    auto& in = godot::Input::get_singleton();
    in.reset();
    EXPECT_EQ(in.get_mouse_mode(), godot::Input::MouseMode::Visible);
    in.set_mouse_mode(godot::Input::MouseMode::Hidden);
    in.set_custom_mouse_cursor("cursor xp_hover.png", 0, {9, 0});
    EXPECT_EQ(in.get_mouse_mode(), godot::Input::MouseMode::Hidden);
    EXPECT_EQ(in.custom_cursor_image(), "cursor xp_hover.png");
    EXPECT_FLOAT_EQ(in.custom_cursor_hotspot().x, 9.0f);
}

// KeyRebinding shows each binding's as_text() and edits the InputMap;
// OptionsManager saves the map. The expected strings were printed by Godot
// 4.1.4 itself.
TEST(GodotInputText, AsTextMatchesGodot) {
    godot::InputEventKey enter;
    enter.keycode = 4194309;
    EXPECT_EQ(enter.as_text(), "Enter");
    godot::InputEventKey right;
    right.physical_keycode = 4194321;
    EXPECT_EQ(right.as_text(), "Right (Physical)");
    godot::InputEventKey r;
    r.keycode = 82;
    EXPECT_EQ(r.as_text(), "R");
    godot::InputEventJoypadButton a;
    a.button_index = 0;
    EXPECT_EQ(a.as_text(), "Joypad Button 0 (Bottom Action, Sony Cross, Xbox A, Nintendo B)");
    godot::InputEventMouseButton m;
    m.button_index = 1;
    EXPECT_EQ(m.as_text(), "Left Mouse Button");
}

TEST(GodotInputText, EventsRoundTripThroughTheSaveText) {
    auto key = std::make_shared<godot::InputEventKey>();
    key->keycode = 4194305;
    key->device = -1;
    const godot::InputEventRef back = godot::str_to_event(godot::event_to_str(*key));
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->as_text(), "Escape");
    EXPECT_EQ(back->device, -1);
    auto joy = std::make_shared<godot::InputEventJoypadButton>();
    joy->button_index = 6;
    EXPECT_EQ(godot::str_to_event(godot::event_to_str(*joy))->as_text(), joy->as_text());
    EXPECT_EQ(godot::str_to_event("nonsense"), nullptr);
}

TEST(GodotInputText, ActionEventsCanBeReadErasedAndReAdded) {
    godot::InputMap map;
    map.add_action("reset");
    auto key = std::make_shared<godot::InputEventKey>();
    key->keycode = 82;
    map.action_add_event("reset", key);
    ASSERT_EQ(map.action_get_events("reset").size(), 1u);
    map.action_erase_events("reset");
    EXPECT_TRUE(map.action_get_events("reset").empty());
    EXPECT_TRUE(map.action_get_events("no such action").empty());
}

TEST(GodotAudioServer, Bus0VolumeIsKept) {
    godot::AudioServer::get_singleton().set_bus_volume_db(0, -0.9151498112135);
    EXPECT_NEAR(godot::AudioServer::get_singleton().get_bus_volume_db(0), -0.9151498f, 1e-5f);
    EXPECT_NEAR(godot::linear_to_db(0.9), -0.9151498112135, 1e-9);
    godot::AudioServer::get_singleton().set_bus_volume_db(0, 0.0);
}

namespace {

// input.cpp VelocityTrack (4.1.4): the speed over at least 0.1 s of motion; the
// first move after 3 s still reports zero. v2.2's ControllerManager ignores a
// mouse motion whose velocity is zero.
TEST(InputVelocity, TheTrackMeasuresOverATenthOfASecond) {
    godot::Input& input = godot::Input::get_singleton();
    input.reset();
    uint64_t now = 10'000'000;
    input.set_ticks_usec([&now] { return now; });
    auto move = [&](float dx) {
        auto m = std::make_shared<godot::InputEventMouseMotion>();
        m->relative = {dx, 0.0f};
        m->velocity = input.get_last_mouse_velocity();
        input.parse_input_event(m);
        input.flush_buffered_events();
        return m->velocity;
    };
    EXPECT_FLOAT_EQ(move(5.0f).x, 0.0f) << "nothing measured yet";
    now += 60'000;
    EXPECT_FLOAT_EQ(move(5.0f).x, 0.0f) << "0.06 s is under the reference frame";
    now += 60'000;
    move(5.0f);
    EXPECT_GT(input.get_last_mouse_velocity().x, 0.0f);
    now += 4'000'000;
    EXPECT_FLOAT_EQ(input.get_last_mouse_velocity().x, 0.0f) << "a long pause resets it";
    input.reset();
}

}  // namespace
