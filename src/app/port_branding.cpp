#include "app/port_branding.h"

#include "godot/control.h"
#include "godot/node.h"
#include "godot/translation.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

namespace app {

namespace {

constexpr float kDesignWidth = 960.0f;

// The logo's rect: whole pixels, so its texels land 1:1 on the screen's.
FillRect LogoRect(PortBranding::LogoSize logo) {
    const float band = PortBranding::kLogoBottom - PortBranding::kLogoTop;
    const float scale = std::min(1.0f, band / static_cast<float>(logo.h));
    FillRect r;
    r.w = std::round(static_cast<float>(logo.w) * scale);
    r.h = std::round(static_cast<float>(logo.h) * scale);
    r.x = std::floor((kDesignWidth - r.w) * 0.5f);
    r.y = PortBranding::kLogoTop + std::floor((band - r.h) * 0.5f);
    r.rgba[0] = r.rgba[1] = r.rgba[2] = PortBranding::kLogoGrey;
    r.rgba[3] = 1.0f;
    return r;
}

// Without a logo file: faces the menu atlas already holds (the loading screen's two).
constexpr const char* kTitleFace = "res://fonts/fake receipt.otf @ 56";
constexpr const char* kSubtitleFace = "res://fonts/fake receipt.otf @ 23";

// Where the 3D card sat on the 960x540 design screen.
constexpr float kTitleY = 118.0f;
constexpr float kTitleH = 64.0f;
constexpr float kSubtitleY = 184.0f;
constexpr float kSubtitleH = 30.0f;

void AddCentred(const assets::FontAtlas& fonts, int face, const char* text, float y, float h, const float rgba[4],
                OverlayDraw& out) {
    if (face < 0) return;
    TextItem item;
    fonts.Layout(face, text, 0.0f, y, kDesignWidth, h, assets::AlignH::Centre, assets::AlignV::Centre, item.quads);
    for (int c = 0; c < 4; ++c) item.rgba[c] = rgba[c];
    out.text.push_back(std::move(item));
}

}  // namespace

PortBranding::Applied PortBranding::Apply(godot::SceneTree& tree) {
    Applied done;
    auto* title = tree.get_node_as<godot::VisualInstance3D>(kTitleNode);
    auto* version = tree.get_node_as<godot::Label>(kVersionNode);
    if (!title || !version) return done;
    done.ok = true;
    title->set_layers(0);   // never drawn; its visibility still says when the text shows

    godot::set_message_suffix(kMainCreditsKey, kPortCredit);
    for (const char* path : kHiddenMenuNodes) {
        if (auto* c = tree.get_node_as<godot::Control>(path)) {
            c->set_visible(false);
            ++done.hidden;
        }
    }
    return done;
}

float PortBranding::CreditHeight(const assets::FontAtlas& fonts, const assets::UiLayout& layout) {
    const int ci = layout.Find(kMainCreditsNode);
    if (ci < 0) return 0.0f;
    const assets::UiControl& c = layout.Controls()[static_cast<std::size_t>(ci)];
    const int face = fonts.Find(c.fontKey);
    if (face < 0) return 0.0f;
    // The lines the credit adds after the original's last: the blank one, the
    // credit and the handle (the credit's first newline ends the original's).
    const std::string_view added = std::string_view(kPortCredit).substr(1);
    return fonts.MeasureHeight(face, added, c.lineSpacing) * c.scale[1];
}

int PortBranding::MakeRoomForTheCredit(godot::SceneTree& tree, float height) {
    auto* credits = tree.get_node_as<godot::Control>(kMainCreditsNode);
    if (!credits || height <= 0.0f || !credits->get_parent()) return 0;
    godot::Node* parent = credits->get_parent();
    // Global tops: a scaled logo's local position is not where it draws.
    const float top = credits->get_global_transform().oy;
    int moved = 0;
    for (int i = 0; i < parent->get_child_count(); ++i) {
        auto* c = godot::cast_to<godot::Control>(parent->get_child(i));
        if (!c || c == credits || c->get_global_transform().oy <= top) continue;
        c->set_position({c->get_position().x, c->get_position().y + height});
        ++moved;
    }
    return moved;
}

std::string PortBranding::WriteVersion(godot::SceneTree& tree) {
    auto* version = tree.get_node_as<godot::Label>(kVersionNode);
    if (!version) return "";
    std::string replaced = version->get_text();
    version->set_text(kVersion);
    return replaced;
}

std::string PortBranding::Describe(const Applied& done) {
    if (!done.ok) return "menu: the port's title/version nodes are missing";
    return "menu: port branding applied: credit after the main credits, " + std::to_string(done.hidden) + "/" +
           std::to_string(std::size(kHiddenMenuNodes)) + " Discord nodes hidden";
}

void PortBranding::Layout(const godot::SceneTree& tree, const assets::FontAtlas& fonts, LogoSize logo,
                          OverlayDraw& out) {
    const auto* title = tree.get_node_as<godot::Node3D>(kTitleNode);
    if (!title || !title->is_visible_in_tree()) return;
    if (logo.w > 0 && logo.h > 0) {
        out.logo.push_back(LogoRect(logo));
        return;
    }
    const float white[4] = {0.82f, 0.82f, 0.82f, 1.0f};
    AddCentred(fonts, fonts.Find(kTitleFace), kTitle, kTitleY, kTitleH, white, out);
    AddCentred(fonts, fonts.Find(kSubtitleFace), kSubtitle, kSubtitleY, kSubtitleH, white, out);
}

}  // namespace app
