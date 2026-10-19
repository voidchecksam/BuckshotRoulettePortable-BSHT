// The Vita's front panel as Godot's touch events.
//
// sceTouchPeek reports every finger that is down, every frame. Godot's input
// is edges: InputEventScreenTouch pressed when a finger arrives, ScreenDrag
// while it moves, ScreenTouch released when it goes. This turns one into the
// other, so the Vita feeds godot::Input what a touchscreen would, and with
// emulate_mouse_from_touch on, touch drives the original's mouse path.
//
// The panel reports at twice the screen resolution (1920x1088 for 960x544,
// vitasdk psp2/touch.h), so coordinates are halved before the viewport fit.
//
// No psp2 headers: the entry point reads SceTouchData and passes plain ids and
// coordinates, which keeps this unit-testable on desktop.
#pragma once

#include "godot/input_event.h"
#include "platform/viewport_map.h"

#include <memory>
#include <vector>

namespace platform {

struct TouchReport {
    int   id = 0;
    float panelX = 0.0f;
    float panelY = 0.0f;
};

class TouchTracker {
public:
    // SCE_TOUCH_MAX_REPORT is 8; the game never uses more than one finger.
    static constexpr int kMaxTouches = 8;

    // Appends this frame's events. `reports` is what the panel says is down
    // now; anything it stops reporting has been lifted.
    void Update(const TouchReport* reports, int count, const ViewportMap& map,
                std::vector<godot::InputEventRef>& out) {
        Active next[kMaxTouches];
        int nextCount = 0;

        for (int i = 0; i < count && nextCount < kMaxTouches; ++i) {
            const TouchReport& r = reports[i];
            const godot::Vector2 position =
                ToViewport(map, r.panelX / 2.0f, r.panelY / 2.0f);
            const Active* was = Find(r.id);
            if (!was) {
                auto touch = std::make_shared<godot::InputEventScreenTouch>();
                touch->index = r.id;
                touch->position = position;
                touch->pressed = true;
                out.push_back(touch);
            } else if (position.x != was->position.x || position.y != was->position.y) {
                auto drag = std::make_shared<godot::InputEventScreenDrag>();
                drag->index = r.id;
                drag->position = position;
                drag->relative = {position.x - was->position.x, position.y - was->position.y};
                out.push_back(drag);
            }
            next[nextCount++] = Active{r.id, position};
        }

        // Gone since last frame: released where it was last seen.
        for (int i = 0; i < count_; ++i) {
            bool stillDown = false;
            for (int j = 0; j < nextCount; ++j) {
                if (next[j].id == active_[i].id) stillDown = true;
            }
            if (stillDown) continue;
            auto touch = std::make_shared<godot::InputEventScreenTouch>();
            touch->index = active_[i].id;
            touch->position = active_[i].position;
            touch->pressed = false;
            out.push_back(touch);
        }

        for (int i = 0; i < nextCount; ++i) active_[i] = next[i];
        count_ = nextCount;
    }

private:
    struct Active {
        int id = 0;
        godot::Vector2 position;
    };

    const Active* Find(int id) const {
        for (int i = 0; i < count_; ++i) {
            if (active_[i].id == id) return &active_[i];
        }
        return nullptr;
    }

    Active active_[kMaxTouches];
    int count_ = 0;
};

}  // namespace platform
