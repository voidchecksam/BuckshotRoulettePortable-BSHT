// Viewport GUI rules from Godot 4.1.4-stable:
//   control.cpp _get_focus_neighbor:2233 / _window_find_focus_neighbor:2310,
//   viewport.cpp _gui_control_grab_focus / gui_release_focus / _gui_input_event
//   (key branch ~2196, mouse branch 1726), base_button.cpp gui_input:56 and
//   on_action_event:147 (ACTION_MODE_BUTTON_RELEASE, base_button.h:58).
#include "godot/control.h"
#include "godot/input_event.h"
#include "godot/input_map.h"
#include "godot/scene_tree.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

// A pad button as main_vita.cpp sends it: device 0, edge-triggered.
godot::InputEventRef PadEv(int button, bool pressed) {
    auto j = std::make_shared<godot::InputEventJoypadButton>();
    j->button_index = button;
    j->pressed = pressed;
    j->device = 0;
    return j;
}

godot::InputEventRef KeyEv(int64_t code, bool pressed) {
    auto k = std::make_shared<godot::InputEventKey>();
    k->keycode = code;
    k->pressed = pressed;
    return k;
}

godot::InputEventRef MouseButtonEv(float x, float y, bool pressed) {
    auto m = std::make_shared<godot::InputEventMouseButton>();
    m->button_index = godot::mouse::LEFT;
    m->pressed = pressed;
    m->position = {x, y};
    m->global_position = {x, y};
    m->button_mask = pressed ? godot::mouse::MASK_LEFT : 0;
    return m;
}

struct GuiFixture : ::testing::Test {
    godot::SceneTree t;
    godot::Control* parent = nullptr;
    godot::Button* dealer = nullptr;
    godot::Button* you = nullptr;
    std::vector<std::string> log;

    void SetUp() override {
        auto& m = godot::InputMap::get_singleton();
        m.clear();
        const std::pair<const char*, int64_t> bindings[] = {
            {"ui_accept", godot::key::ENTER}, {"ui_up", godot::key::UP},       {"ui_down", godot::key::DOWN},
            {"ui_left", godot::key::LEFT},    {"ui_right", godot::key::RIGHT}, {"ui_focus_next", 0},
            {"ui_focus_prev", 0}};
        for (const auto& [name, code] : bindings) {
            m.add_action(name);
            if (code != 0) m.action_add_event(name, KeyEv(code, false));
        }
        // The pad bindings as assets/extracted/input_map.json has them: the
        // d-pad on device 0, ui_accept on CROSS for all devices. This is what
        // the Vita sends; it has no keyboard.
        const std::pair<const char*, int> pad[] = {{"ui_accept", godot::joy::A},
                                                   {"ui_up", godot::joy::DPAD_UP},
                                                   {"ui_down", godot::joy::DPAD_DOWN}};
        for (const auto& [name, button] : pad) {
            auto j = std::make_shared<godot::InputEventJoypadButton>();
            j->button_index = button;
            j->device = (button == godot::joy::A) ? godot::InputMap::ALL_DEVICES : 0;
            m.action_add_event(name, j);
        }

        godot::Node* root = t.add_node(std::make_unique<godot::Node>("main"), nullptr);
        parent = static_cast<godot::Control*>(t.add_node(std::make_unique<godot::Control>("shooting choice"), root));
        parent->set_global_rect({0, 0}, {960, 540});
        parent->set_mouse_filter(godot::Control::MOUSE_FILTER_IGNORE);
        dealer = static_cast<godot::Button*>(
            t.add_node(std::make_unique<godot::Button>("m button_choice dealer"), parent));
        you = static_cast<godot::Button*>(t.add_node(std::make_unique<godot::Button>("m button_choice you"), parent));
        dealer->set_global_rect({433, 125}, {64, 56});
        you->set_global_rect({433, 281}, {64, 56});
        dealer->set_focus_neighbor(godot::SIDE_BOTTOM, "../m button_choice you");
        you->set_focus_neighbor(godot::SIDE_TOP, "../m button_choice dealer");
        for (godot::Button* b : {dealer, you}) {
            const std::string n = b->get_name();
            b->connect("focus_entered", [this, n] { log.push_back("enter:" + n); });
            b->connect("focus_exited", [this, n] { log.push_back("exit:" + n); });
            b->connect("pressed", [this, n] { log.push_back("pressed:" + n); });
        }
        t.connect_gui_focus_changed([this](godot::Control* c) { log.push_back("changed:" + c->get_name()); });
    }
};

// On the Vita every ui_accept arrives as an InputEventJoypadButton (there is
// no keyboard). With the bindings in place, CROSS presses the focused button
// and the d-pad moves the focus.
TEST_F(GuiFixture, CrossOnThePadPressesTheFocusedButton) {
    dealer->grab_focus();
    ASSERT_EQ(t.gui_get_focus_owner(), dealer);

    t.push_input(PadEv(godot::joy::A, true));
    t.push_input(PadEv(godot::joy::A, false));

    EXPECT_EQ(log.back(), "pressed:m button_choice dealer")
        << "CROSS must press the focused button, as ENTER does";
}

// And the d-pad must still move the focus, so a pad-only player can choose.
TEST_F(GuiFixture, TheDpadMovesTheFocusLikeTheArrowKeys) {
    you->grab_focus();

    t.push_input(PadEv(godot::joy::DPAD_UP, true));
    t.push_input(PadEv(godot::joy::DPAD_UP, false));

    EXPECT_EQ(t.gui_get_focus_owner(), dealer);
}

}  // namespace

// A Button focuses by default (BaseButton constructor: FOCUS_ALL); a Control does not.
TEST_F(GuiFixture, ButtonsFocusByDefaultControlsDoNot) {
    EXPECT_EQ(dealer->get_focus_mode(), godot::Control::FOCUS_ALL);
    EXPECT_EQ(parent->get_focus_mode(), godot::Control::FOCUS_NONE);
    parent->grab_focus();
    EXPECT_EQ(t.gui_get_focus_owner(), nullptr);
}

// viewport.cpp _gui_control_grab_focus: the old owner is released first
// (focus_exited), then gui_focus_changed, then focus_entered.
TEST_F(GuiFixture, GrabFocusSignalOrder) {
    you->grab_focus();
    dealer->grab_focus();
    EXPECT_EQ(log, (std::vector<std::string>{"changed:m button_choice you", "enter:m button_choice you",
                                              "exit:m button_choice you", "changed:m button_choice dealer",
                                              "enter:m button_choice dealer"}));
    you->grab_focus();
    log.clear();
    you->grab_focus();   // already focused: nothing
    EXPECT_TRUE(log.empty());
}

TEST_F(GuiFixture, ReleaseFocusEmitsExited) {
    you->grab_focus();
    log.clear();
    you->release_focus();
    EXPECT_EQ(t.gui_get_focus_owner(), nullptr);
    EXPECT_EQ(log, (std::vector<std::string>{"exit:m button_choice you"}));
}

TEST_F(GuiFixture, ExplicitNeighbourMovesFocusOnPressOnly) {
    you->grab_focus();
    t.push_input(KeyEv(godot::key::UP, true));
    EXPECT_EQ(t.gui_get_focus_owner(), dealer);
    EXPECT_TRUE(t.is_input_handled());
    t.push_input(KeyEv(godot::key::UP, false));
    EXPECT_EQ(t.gui_get_focus_owner(), dealer);
}

// base_button.h:58 ACTION_MODE_BUTTON_RELEASE: pressed fires on the release.
TEST_F(GuiFixture, AcceptPressesOnRelease) {
    you->grab_focus();
    log.clear();
    t.push_input(KeyEv(godot::key::ENTER, true));
    EXPECT_TRUE(log.empty());
    t.push_input(KeyEv(godot::key::ENTER, false));
    EXPECT_EQ(log, (std::vector<std::string>{"pressed:m button_choice you"}));
}

// No focus: nothing to deliver keys to and nothing to navigate from.
TEST_F(GuiFixture, KeysWithoutFocusDoNothing) {
    t.push_input(KeyEv(godot::key::DOWN, true));
    EXPECT_EQ(t.gui_get_focus_owner(), nullptr);
    EXPECT_FALSE(t.is_input_handled());
}

// viewport.cpp ~2203: a focused control no longer visible in tree loses focus.
TEST_F(GuiFixture, HiddenFocusIsReleasedOnNextKey) {
    you->grab_focus();
    parent->set_visible(false);
    t.push_input(KeyEv(godot::key::UP, true));
    EXPECT_EQ(t.gui_get_focus_owner(), nullptr);
}

// control.cpp:2244: an explicit neighbour that is hidden is skipped by
// continuing the search from it.
TEST_F(GuiFixture, HiddenExplicitNeighbourIsNotFocused) {
    dealer->set_visible(false);
    you->grab_focus();
    t.push_input(KeyEv(godot::key::UP, true));
    EXPECT_NE(t.gui_get_focus_owner(), dealer);
}

// control.cpp:2256 geometric fallback: no neighbour set, the nearest FOCUS_ALL
// control on that side.
TEST_F(GuiFixture, GeometricNeighbourWhenNoneSet) {
    dealer->set_focus_neighbor(godot::SIDE_BOTTOM, "");
    dealer->grab_focus();
    t.push_input(KeyEv(godot::key::DOWN, true));
    EXPECT_EQ(t.gui_get_focus_owner(), you);
}

// viewport.cpp _gui_input_event mouse branch: the press finds the control under
// the pointer, grabs focus (FOCUS_ALL), and the button presses on release.
TEST_F(GuiFixture, MouseClickFocusesAndPressesOnRelease) {
    t.push_input(MouseButtonEv(450, 300, true));
    EXPECT_EQ(t.gui_get_focus_owner(), you);
    t.push_input(MouseButtonEv(450, 300, false));
    ASSERT_FALSE(log.empty());
    EXPECT_EQ(log.back(), "pressed:m button_choice you");
}

// A press outside every control reaches no control and grabs no focus.
TEST_F(GuiFixture, MouseClickOnNothing) {
    t.push_input(MouseButtonEv(10, 10, true));
    t.push_input(MouseButtonEv(10, 10, false));
    EXPECT_EQ(t.gui_get_focus_owner(), nullptr);
    EXPECT_TRUE(log.empty());
}

// MOUSE_FILTER_IGNORE controls are transparent to the pointer.
TEST_F(GuiFixture, IgnoredControlIsNotFound) {
    you->set_mouse_filter(godot::Control::MOUSE_FILTER_IGNORE);
    EXPECT_EQ(t.gui_find_control({450, 300}), nullptr);
    EXPECT_EQ(t.gui_find_control({450, 150}), dealer);
}

// canvas_item.cpp:72-93 set_visible notifies the item and its visible
// CanvasItem children; control.cpp:3251 hands each one that left the screen to
// viewport.cpp:2381 _gui_hide_control, which releases its focus.
// DecisionTextManager.SetUI(false) hides "shooting choice" with DEALER focused.
TEST_F(GuiFixture, HidingAParentReleasesTheFocusedChild) {
    dealer->grab_focus();
    ASSERT_EQ(t.gui_get_focus_owner(), dealer);
    log.clear();

    parent->set_visible(false);

    EXPECT_EQ(t.gui_get_focus_owner(), nullptr);
    EXPECT_EQ(log, (std::vector<std::string>{"exit:m button_choice dealer"}));
}

TEST_F(GuiFixture, HidingAnUnfocusedSiblingKeepsFocus) {
    dealer->grab_focus();
    you->set_visible(false);
    EXPECT_EQ(t.gui_get_focus_owner(), dealer);
}
