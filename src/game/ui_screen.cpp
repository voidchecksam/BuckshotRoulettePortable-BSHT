#include "game/ui_screen.h"

#include "godot/control.h"
#include "godot/scene_tree.h"

#include <algorithm>

namespace game {
namespace {

// TextServer::OVERRUN_TRIM_CHAR (Label.text_overrun_behavior).
constexpr int kOverrunTrimChar = 1;

// CanvasItem::get_modulate inheritance: a CanvasItem draws with the product of
// its own modulate and every CanvasItem ancestor's (self_modulate is not
// inherited). The port's CanvasItems are Controls; the chain stops at the first
// non-Control, as Godot's canvas parent chain does.
godot::Color InheritedModulate(const godot::Control& node) {
    godot::Color out{1.0f, 1.0f, 1.0f, 1.0f};
    for (const godot::Node* n = &node; n; n = n->get_parent()) {
        const godot::Control* c = godot::cast_to<godot::Control>(n);
        if (!c) break;
        const godot::Color m = c->get_modulate();
        out.r *= m.r;
        out.g *= m.g;
        out.b *= m.b;
        out.a *= m.a;
    }
    return out;
}

}  // namespace

void UiScreen::BindTree(const godot::SceneTree* tree) {
    tree_ = tree;
    missingTextures_.clear();   // each scene reports its own
    Resolve();
}

void UiScreen::Resolve() {
    nodes_.assign(members_.size(), nullptr);
    unbound_ = 0;
    for (std::size_t k = 0; k < members_.size(); ++k) {
        // A member the layout no longer has (the layout was replaced under an
        // open screen) is unbound: its index belongs to the old layout.
        const bool inLayout = layout_ && members_[k] >= 0 &&
                              static_cast<std::size_t>(members_[k]) < layout_->Controls().size();
        if (tree_ && inLayout) {
            const assets::UiControl& c = layout_->Controls()[static_cast<std::size_t>(members_[k])];
            nodes_[k] = tree_->get_node_as<godot::Control>(c.path);
        }
        if (!nodes_[k]) ++unbound_;
    }
}

bool UiScreen::Open(const std::string& prefix) {
    return OpenAll(std::vector<std::string>{prefix});
}

bool UiScreen::OpenAll(const std::vector<std::string>& prefixes) {
    Close();
    if (!layout_) return false;

    prefixes_ = prefixes;
    for (const std::string& prefix : prefixes_) {
        for (int i : layout_->FindByPrefix(prefix)) {
            // Deduplicate: a control listed twice would be drawn twice, doubling
            // its alpha, which looks like a brightness bug.
            if (std::find(members_.begin(), members_.end(), i) == members_.end()) members_.push_back(i);
        }
    }
    // Scene order, not prefix order, so later siblings still sit on top.
    std::sort(members_.begin(), members_.end());
    Resolve();
    return !members_.empty();
}

void UiScreen::Close() {
    prefixes_.clear();
    members_.clear();
    nodes_.clear();
    unbound_ = 0;
    overrides_.clear();
    shown_.clear();
}

bool UiScreen::SetText(int control, std::string_view text) {
    if (std::find(members_.begin(), members_.end(), control) == members_.end()) return false;
    for (TextOverride& o : overrides_) {
        if (o.control != control) continue;
        // Set every frame by the dialogue box; unchanged text costs nothing.
        if (o.text != text) o.text.assign(text.data(), text.size());
        return true;
    }
    overrides_.push_back(TextOverride{control, std::string(text)});
    return true;
}

bool UiScreen::SetShown(int control, bool shown) {
    if (std::find(members_.begin(), members_.end(), control) == members_.end()) return false;
    for (ShownOverride& o : shown_) {
        if (o.control == control) {
            o.shown = shown;
            return true;
        }
    }
    shown_.push_back(ShownOverride{control, shown});
    return true;
}

bool UiScreen::ShownOr(int control, bool fallback) const {
    for (const ShownOverride& o : shown_) {
        if (o.control == control) return o.shown;
    }
    return fallback;
}

void UiScreen::BuildDrawList(std::vector<UiDrawItem>& out) const {
    out.clear();
    if (!layout_ || !tree_) return;

    for (std::size_t k = 0; k < members_.size(); ++k) {
        const godot::Control* node = nodes_[k];
        if (!node) continue;
        const int i = members_[k];
        const assets::UiControl& c = layout_->Controls()[static_cast<std::size_t>(i)];
        // An untextured ColorRect draws as a flat fill: texture -1 tells the
        // caller to use its 1x1 white.
        const bool flatFill = c.texture < 0 && c.type == "ColorRect" && c.hasColour;
        if (c.texture < 0 && !flatFill) continue;
        if (!ShownOr(i, node->is_visible_in_tree())) continue;

        const godot::Color m = InheritedModulate(*node);
        // A ColorRect fills with its LIVE colour once anything has set it (the
        // menu's splash fades viewblocker.color); until then, the export's.
        const auto* rect = flatFill ? godot::cast_to<godot::ColorRect>(node) : nullptr;
        const godot::Color live = rect && rect->has_color()
                                      ? rect->get_color()
                                      : godot::Color{c.colour[0], c.colour[1], c.colour[2], c.colour[3]};
        const float tr = flatFill ? live.r : 1.0f;
        const float tg = flatFill ? live.g : 1.0f;
        const float tb = flatFill ? live.b : 1.0f;
        const float ta = flatFill ? live.a : 1.0f;
        // self_modulate: the node's own, live (animations fade it).
        const godot::Color sm = node->get_self_modulate();
        const float alpha = m.a * sm.a * ta;
        // Fill rate is this port's measured budget and every UI pixel blends
        // over an already-shaded frame, so an invisible quad is dropped here.
        if (alpha <= 0.0f) continue;

        // The layout's rect is the load-time one; TextShaker moves controls.
        const godot::Vector2 moved = node->get_offset_since_load();
        UiDrawItem item;
        item.control = i;
        item.texture = c.texture;
        // A TextureRect shows the texture a script set (ChecmarkBranch), when
        // the layout loaded it.
        if (const auto* texRect = godot::cast_to<godot::TextureRect>(node); texRect && !texRect->get_texture().empty()) {
            const int live = layout_->FindTexture(texRect->get_texture());
            if (live >= 0) {
                item.texture = live;
            } else if (log_ && missingTextures_.insert(texRect->get_texture()).second) {
                log_("ui: " + texRect->get_texture() + " is not in the layout's textures; " +
                     layout_->Controls()[static_cast<std::size_t>(i)].path + " keeps its own");
            }
        }
        item.x = c.x + moved.x;
        item.y = c.y + moved.y;
        item.w = c.w;
        item.h = c.h;
        item.rgba[0] = m.r * sm.r * tr;
        item.rgba[1] = m.g * sm.g * tg;
        item.rgba[2] = m.b * sm.b * tb;
        item.rgba[3] = alpha;
        out.push_back(item);
    }
}

void UiDrawSequence(const std::vector<UiDrawItem>& items, const std::vector<UiTextRun>& runs,
                    std::vector<UiDrawStep>& out) {
    out.clear();
    std::size_t i = 0, t = 0;
    while (i < items.size() || t < runs.size()) {
        const bool takeText = i == items.size() || (t < runs.size() && runs[t].control < items[i].control);
        out.push_back(takeText ? UiDrawStep{true, t++} : UiDrawStep{false, i++});
    }
}

namespace {

// Label.visible_characters, VC_CHARS_BEFORE_SHAPING (4.1's default): the text
// cut to its first `n` characters (code points, newlines included).
std::size_t Utf8Prefix(const std::string& s, int n) {
    std::size_t at = 0;
    for (int k = 0; k < n && at < s.size(); ++k) {
        ++at;
        while (at < s.size() && (static_cast<unsigned char>(s[at]) & 0xC0u) == 0x80u) ++at;
    }
    return at;
}

}  // namespace

void UiScreen::BuildTextList(std::vector<UiTextRun>& out) const {
    std::size_t used = 0;
    if (layout_ && tree_) {
        for (std::size_t k = 0; k < members_.size(); ++k) {
            const godot::Control* node = nodes_[k];
            if (!node) continue;
            const int i = members_[k];
            const assets::UiControl& c = layout_->Controls()[static_cast<std::size_t>(i)];
            if (c.fontKey.empty()) continue;
            if (!ShownOr(i, node->is_visible_in_tree())) continue;

            // The node's text: scripts write it (PickupIndicator, DialogueManager).
            // A Label draws it translated (label.cpp xl_text). The export's
            // authored text is only a fallback for other control types.
            const std::string* text = &c.text;
            if (const auto* label = godot::cast_to<godot::Label>(node)) {
                text = &label->get_xl_text();
            } else if (const auto* button = godot::cast_to<godot::Button>(node)) {
                text = &button->get_text();
            }
            for (const TextOverride& o : overrides_) {
                if (o.control == i) {
                    text = &o.text;
                    break;
                }
            }
            if (text->empty()) continue;

            const godot::Color m = InheritedModulate(*node);
            // theme_override_colors/font_color, when animated (the menu's
            // volume text flickers through it).
            const auto* label = godot::cast_to<godot::Label>(node);
            const bool overridden = label && label->has_font_color_override();
            const godot::Color fc = overridden ? label->get_font_color_override()
                                               : godot::Color{c.fontColour[0], c.fontColour[1],
                                                              c.fontColour[2], c.fontColour[3]};
            const float alpha = fc.a * m.a * node->get_self_modulate().a;
            if (alpha <= 0.0f) continue;

            if (used == out.size()) out.emplace_back();
            UiTextRun& run = out[used++];
            run.control = i;
            // assign() reuses the run's buffer: no allocation once warmed up.
            run.fontKey.assign(c.fontKey);
            run.text.assign(*text);
            if (label && label->get_visible_characters() >= 0) {
                run.text.resize(Utf8Prefix(run.text, label->get_visible_characters()));
            }
            run.sx = c.scale[0];
            run.sy = c.scale[1];
            const godot::Vector2 moved = node->get_offset_since_load();
            run.x = c.x + moved.x;
            run.y = c.y + moved.y;
            run.w = c.w;
            run.h = c.h;
            run.rgba[0] = fc.r * m.r;
            run.rgba[1] = fc.g * m.g;
            run.rgba[2] = fc.b * m.b;
            run.rgba[3] = alpha;
            run.alignH = static_cast<assets::AlignH>(c.alignH);
            run.alignV = static_cast<assets::AlignV>(c.alignV);
            run.trimChars = c.overrun == kOverrunTrimChar;
            run.lineSpacing = c.lineSpacing;
        }
    }
    out.resize(used);
}

}  // namespace game
