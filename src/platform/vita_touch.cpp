#include "platform/vita_touch.h"

#include "platform/touch_tracker.h"
#include "platform/viewport_map.h"

#include <psp2/touch.h>

#include <cstring>

namespace platform {
namespace {

// The panel the game is drawn on. The touch panel itself reports at twice this
// (1920x1088), which TouchTracker halves.
constexpr float kPanelWidth = 960.0f;
constexpr float kPanelHeight = 544.0f;

}  // namespace

void StartTouchSampling() {
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
}

void ReadTouch(FrameInput& in) {
    static TouchTracker tracker;
    static const ViewportMap map = FitKeepAspect(kPanelWidth, kPanelHeight);

    SceTouchData touch;
    std::memset(&touch, 0, sizeof(touch));
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) < 0) return;

    TouchReport reports[TouchTracker::kMaxTouches];
    int count = 0;
    for (SceUInt32 i = 0; i < touch.reportNum && count < TouchTracker::kMaxTouches; ++i) {
        reports[count].id = touch.report[i].id;
        reports[count].panelX = static_cast<float>(touch.report[i].x);
        reports[count].panelY = static_cast<float>(touch.report[i].y);
        ++count;
    }
    tracker.Update(reports, count, map, in.events);
}

}  // namespace platform
