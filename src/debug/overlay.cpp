#include "debug/overlay.h"

#include <algorithm>

namespace debug {

namespace {

// Rows that fit under the header. The Vita panel is 544 tall and the smallest
// face is 16px at 24px line height, which leaves room for about 18 rows plus a
// header and a footer.
constexpr int kVisibleRows = 16;

int Clamp(int v, int lo, int hi) {
    if (hi < lo) return lo;
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

}  // namespace

const char* PageName(Page p) {
    switch (p) {
        case Page::Summary:   return "SUMMARY";
        case Page::Props:     return "PROPS";
        case Page::Animation: return "ANIMATION";
        case Page::Screens:   return "SCREENS";
        case Page::Count:     break;
    }
    return "?";
}

int Overlay::RowCount(const DebugState& s) const {
    switch (page_) {
        case Page::Summary:   return 0;
        case Page::Props:     return static_cast<int>(s.props.size());
        case Page::Animation: return static_cast<int>(s.anims.size());
        case Page::Screens:   return static_cast<int>(s.ui.screens.size());
        case Page::Count:     break;
    }
    return 0;
}

OverlayAction Overlay::Handle(const platform::FrameInput& in,
                              const DebugState& s) {
    OverlayAction action;

    if (in.toggleDebug) {
        visible_ = !visible_;
        // Reset the cursor on open so it never resumes pointing at a row that
        // a re-gather has shortened away.
        cursor_ = 0;
        scroll_ = 0;
    }
    if (!visible_) return action;

    const int pages = static_cast<int>(Page::Count);
    if (in.debugNext) page_ = static_cast<Page>((static_cast<int>(page_) + 1) % pages);
    if (in.debugPrev) page_ = static_cast<Page>((static_cast<int>(page_) + pages - 1) % pages);
    if (in.debugNext || in.debugPrev) { cursor_ = 0; scroll_ = 0; }

    const int rows = RowCount(s);
    if (rows > 0) {
        if (in.navDown) ++cursor_;
        if (in.navUp)   --cursor_;
        // A page is a lot of rows on a small screen; left/right jump by a page.
        if (in.navRight) cursor_ += kVisibleRows;
        if (in.navLeft)  cursor_ -= kVisibleRows;
        cursor_ = Clamp(cursor_, 0, rows - 1);

        // Keep the cursor on screen without letting the window run past the end.
        if (cursor_ < scroll_) scroll_ = cursor_;
        if (cursor_ >= scroll_ + kVisibleRows) scroll_ = cursor_ - kVisibleRows + 1;
        scroll_ = Clamp(scroll_, 0, rows > kVisibleRows ? rows - kVisibleRows : 0);
    } else {
        cursor_ = 0;
        scroll_ = 0;
    }

    if (!in.confirm) return action;

    // Every action is bounds checked against the state it was drawn from. A
    // stale cursor must do nothing rather than read past the end.
    switch (page_) {
        case Page::Props:
            if (cursor_ < static_cast<int>(s.props.size())) {
                const PropRow& r = s.props[static_cast<std::size_t>(cursor_)];
                action.kind = OverlayAction::Kind::TogglePropOverride;
                action.nodePath = r.path;

                // TOGGLES. Forcing visible unconditionally meant a second press
                // said nothing and could not be undone, so the only way to
                // check "did that one appear?" was to remember what the screen
                // looked like before.
                const auto it = std::find(forced_.begin(), forced_.end(), r.path);
                if (it == forced_.end()) {
                    action.visible = true;
                    forced_.push_back(r.path);
                } else {
                    action.visible = false;
                    forced_.erase(it);
                }
            }
            break;
        case Page::Animation:
            if (cursor_ < static_cast<int>(s.anims.size())) {
                const AnimRow& r = s.anims[static_cast<std::size_t>(cursor_)];
                action.kind = OverlayAction::Kind::PlayAnimation;
                action.playerPath = r.playerPath;
                action.name = r.name;
            }
            break;
        case Page::Screens:
            if (cursor_ < static_cast<int>(s.ui.screens.size())) {
                action.kind = OverlayAction::Kind::OpenScreen;
                action.screen = cursor_;
            }
            break;
        case Page::Summary:
        case Page::Count:
            break;
    }
    return action;
}

}  // namespace debug
