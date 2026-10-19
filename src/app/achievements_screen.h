// The ACHIEVEMENTS screen and the unlock toast. The screen is the one addition
// to the original's menu: reached with the pad by pressing down from EXIT, or
// by touching its entry under EXIT. The toast is what Steam's overlay shows
// the first time an achievement unlocks: name and description, 4 s, bottom
// right, one after another.
//
// Headless: this half builds rows and lays them out as quads; GameApp draws
// them through the sprite batch it already owns.
#pragma once

#include "app/text_item.h"
#include "assets/font.h"
#include "game/achievements.h"

#include <string>
#include <vector>

namespace app {

// A flat fill behind the text, in 960x540 design pixels.
struct FillRect {
    float x = 0, y = 0, w = 0, h = 0;
    float rgba[4] = {0, 0, 0, 1};
};

struct OverlayDraw {
    std::vector<FillRect> fills;
    std::vector<TextItem> text;
    std::vector<FillRect> logo;   // the port's title logo (PortBranding), drawn with its texture
    void Clear() {
        fills.clear();
        text.clear();
        logo.clear();
    }
};

class AchievementsScreen {
public:
    static constexpr int kVisibleRows = 7;
    // The entry the pad reaches from EXIT: its size; its place is the menu
    // Button's (app::AddAchievementsEntry), given with SetEntryText.
    static constexpr float kEntryW = 200, kEntryH = 25;

    explicit AchievementsScreen(const game::Achievements& achievements) : achievements_(&achievements) {}

    struct Row {
        std::string title;
        std::string description;   // "???" while a hidden one is locked
        float rgba[4] = {1, 1, 1, 1};
    };
    std::vector<Row> Rows() const;

    void Open() { open_ = true; first_ = 0; }
    void Cancel() { open_ = false; }
    bool IsOpen() const { return open_; }
    void MoveDown();
    void MoveUp();
    int FirstVisible() const { return first_; }

    // The menu entry under EXIT: selected (pad) or not.
    void SetEntrySelected(bool on) { entrySelected_ = on; }
    bool EntrySelected() const { return entrySelected_; }
    // `x`: the entry Button's left edge. `y`, `h`: the box its text is centred
    // in, the menu's own label box one row below EXIT's
    // (app::AchievementsEntryTextBox), so it lays out as the buttons do.
    void SetEntryText(float x, float y, float h) {
        entryX_ = x;
        textY_ = y;
        textH_ = h;
    }

    // The entry (when `showEntry`) and, when open, the screen.
    void Layout(const assets::FontAtlas& fonts, bool showEntry, OverlayDraw& out) const {
        LayoutEntry(fonts, showEntry, out);
        LayoutScreen(fonts, out);
    }
    // The menu entry alone (nothing while the screen is open): drawn under the
    // menu's own controls, so the splash's black covers it.
    void LayoutEntry(const assets::FontAtlas& fonts, bool showEntry, OverlayDraw& out) const;
    // The open screen alone (nothing while closed): drawn over everything.
    void LayoutScreen(const assets::FontAtlas& fonts, OverlayDraw& out) const;

private:
    const game::Achievements* achievements_;
    bool open_ = false;
    bool entrySelected_ = false;
    float entryX_ = 0.0f;   // the menu Button's left edge (SetEntryText)
    float textY_ = 0.0f;    // the box its text is centred in
    float textH_ = 0.0f;
    int first_ = 0;
};

class AchievementToast {
public:
    static constexpr float kSeconds = 4.0f;

    // Advances the clock, and takes the next first-time unlock when idle.
    void Update(float dt, game::Achievements& achievements);
    bool Showing() const { return showing_; }
    const game::AchievementText& Current() const { return current_; }
    void Layout(const assets::FontAtlas& fonts, OverlayDraw& out) const;

private:
    game::AchievementText current_;
    bool showing_ = false;
    float left_ = 0.0f;
};

// `text` broken at spaces so each line fits `width` in `face`.
std::vector<std::string> WrapText(const assets::FontAtlas& fonts, int face, const std::string& text, float width);

}  // namespace app
