#include "app/achievements_screen.h"

#include <algorithm>

namespace app {
namespace {

// The menu's own faces: its buttons' and the small one every table has.
constexpr const char* kTitleFace = "res://fonts/fake receipt.otf @ 26";
constexpr const char* kBodyFace = "(embedded) @ 16";
constexpr float kLocked = 0.45f;

void AddText(const assets::FontAtlas& fonts, int face, const std::string& s, float x, float y, float w, float h,
             assets::AlignH ah, const float rgba[4], OverlayDraw& out, assets::AlignV av = assets::AlignV::Top) {
    if (face < 0 || s.empty()) return;
    TextItem item;
    fonts.Layout(face, s, x, y, w, h, ah, av, item.quads);
    std::copy(rgba, rgba + 4, item.rgba);
    out.text.push_back(std::move(item));
}

}  // namespace

std::vector<std::string> WrapText(const assets::FontAtlas& fonts, int face, const std::string& text, float width) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t space = text.find(' ', at);
        if (space == std::string::npos) space = text.size();
        const std::string word = text.substr(at, space - at);
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && fonts.MeasureWidth(face, tryLine) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = tryLine;
        }
        at = space + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

std::vector<AchievementsScreen::Row> AchievementsScreen::Rows() const {
    std::vector<Row> rows;
    for (const game::AchievementText& t : achievements_->All()) {
        Row r;
        const bool unlocked = achievements_->IsUnlocked(t.id);
        r.title = t.name;
        r.description = (t.hidden && !unlocked) ? "???" : t.description;
        const float c = unlocked ? 1.0f : kLocked;
        r.rgba[0] = r.rgba[1] = r.rgba[2] = c;
        rows.push_back(std::move(r));
    }
    return rows;
}

void AchievementsScreen::MoveDown() {
    const int last = std::max(0, static_cast<int>(achievements_->All().size()) - kVisibleRows);
    first_ = std::min(first_ + 1, last);
}

void AchievementsScreen::MoveUp() {
    first_ = std::max(first_ - 1, 0);
}

void AchievementsScreen::LayoutEntry(const assets::FontAtlas& fonts, bool showEntry, OverlayDraw& out) const {
    const int title = fonts.Find(kTitleFace);
    if (showEntry && !open_) {
        // As the menu's own buttons draw: white text, half grey while focused or
        // hovered (measured 128/255), and no box. A fill reads as a rectangle
        // the other entries lack.
        const float white[4] = {1, 1, 1, 1};
        const float dimmed[4] = {0.5f, 0.5f, 0.5f, 1};
        const std::size_t before = out.text.size();
        AddText(fonts, title, "ACHIEVEMENTS", entryX_, textY_, kEntryW, textH_, assets::AlignH::Centre,
                entrySelected_ ? dimmed : white, out, assets::AlignV::Centre);
        if (out.text.size() == before) return;   // no face: nothing laid out
        // The menu's buttons carry a Control scale of 0.76 across: squeeze the
        // entry the same way, about its centre, so it reads as one of them.
        constexpr float kButtonScaleX = 0.76f;
        const float cx = entryX_ + kEntryW * 0.5f;
        for (assets::TextQuad& q : out.text.back().quads) {
            q.x = cx + (q.x - cx) * kButtonScaleX;
            q.w *= kButtonScaleX;
        }
    }
}

void AchievementsScreen::LayoutScreen(const assets::FontAtlas& fonts, OverlayDraw& out) const {
    if (!open_) return;
    const int title = fonts.Find(kTitleFace);
    const int body = fonts.Find(kBodyFace);
    const float white[4] = {1, 1, 1, 1};
    out.fills.push_back(FillRect{0, 0, 960, 540, {0, 0, 0, 0.9f}});
    AddText(fonts, title, "ACHIEVEMENTS", 0, 12, 960, 30, assets::AlignH::Centre, white, out);
    const std::vector<Row> rows = Rows();
    float y = 56;
    for (int i = first_; i < static_cast<int>(rows.size()) && i < first_ + kVisibleRows; ++i) {
        const Row& r = rows[static_cast<std::size_t>(i)];
        AddText(fonts, title, r.title, 90, y, 780, 28, assets::AlignH::Left, r.rgba, out);
        y += 28;
        for (const std::string& line : WrapText(fonts, body, r.description, 780)) {
            AddText(fonts, body, line, 110, y, 760, 20, assets::AlignH::Left, r.rgba, out);
            y += 19;
        }
        y += 12;
    }
    const float grey[4] = {0.6f, 0.6f, 0.6f, 1};
    AddText(fonts, body, "UP / DOWN: SCROLL      BACK: RETURN", 0, 514, 960, 20, assets::AlignH::Centre, grey, out);
}

void AchievementToast::Update(float dt, game::Achievements& achievements) {
    if (showing_) {
        left_ -= dt;
        if (left_ > 0.0f) return;
        showing_ = false;
    }
    if (achievements.PopToast(current_)) {
        showing_ = true;
        left_ = kSeconds + (left_ < 0.0f ? left_ : 0.0f);
        if (left_ <= 0.0f) left_ = kSeconds;
    }
}

void AchievementToast::Layout(const assets::FontAtlas& fonts, OverlayDraw& out) const {
    if (!showing_) return;
    const int title = fonts.Find(kTitleFace);
    const int body = fonts.Find(kBodyFace);
    const std::vector<std::string> lines = WrapText(fonts, body, current_.description, 340);
    const float h = 40.0f + 19.0f * static_cast<float>(lines.size());
    const float x = 960 - 370, y = 540 - 12 - h;
    out.fills.push_back(FillRect{x, y, 360, h, {0.05f, 0.05f, 0.05f, 0.92f}});
    const float gold[4] = {0.95f, 0.85f, 0.45f, 1};
    const float white[4] = {1, 1, 1, 1};
    AddText(fonts, title, current_.name, x + 10, y + 6, 340, 28, assets::AlignH::Left, gold, out);
    float ly = y + 34;
    for (const std::string& line : lines) {
        AddText(fonts, body, line, x + 10, ly, 340, 20, assets::AlignH::Left, white, out);
        ly += 19;
    }
}

}  // namespace app
