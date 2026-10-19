// One open UI screen: which controls are on it and what to draw for them.
//
// The scene tree decides what is on screen. A control is drawn only while its
// godot::Control is visible in the tree, with the node's modulate multiplied
// down its Control ancestors (Godot's CanvasItem modulate inherits), because
// the translated scripts show, hide and fade UI: ButtonClass_Main.SetUI writes
// a bracket's modulate.a, DescriptionManager fades its Labels, DialogueManager
// shows the dialogue box. Layout data (rects, textures, fonts) comes from
// ui_layout.json.
//
// Focus and navigation are not here: they are Godot's Viewport (godot/gui.cpp).
//
// A screen is a path prefix. The scene groups its UI that way already, so the
// port never keeps a hard-coded list a re-export could invalidate.
#pragma once

#include "assets/font.h"
#include "assets/ui_layout.h"

#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace godot {
class Control;
class SceneTree;
}  // namespace godot

namespace game {

// One quad the renderer should put up. Design-space pixels; colour already
// resolved, so the renderer makes no decisions.
struct UiDrawItem {
    int   control = -1;
    int   texture = -1;
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    float rgba[4] = {1, 1, 1, 1};
};

// A run of text to put on screen: which face, what string, where, and how it is
// aligned inside that rect. DATA, not quads: quads need the font atlas.
struct UiTextRun {
    int         control = -1;
    std::string fontKey;
    std::string text;
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;   // the SCALED box
    float sx = 1.0f, sy = 1.0f;   // the control's scale: lay out in w/sx x h/sy, then scale
    float rgba[4] = {1, 1, 1, 1};
    assets::AlignH alignH = assets::AlignH::Left;
    assets::AlignV alignV = assets::AlignV::Top;
    bool trimChars = false;   // Label TRIM_CHAR: lines cut at the unscaled width
    int lineSpacing = assets::kDefaultLineSpacing;   // the Label's own (theme or LabelSettings)
};

// One entry of a screen's draw sequence: a fill/texture (UiDrawItem) or a
// text run, by index into its list.
struct UiDrawStep {
    bool        text = false;
    std::size_t index = 0;
};

// Fills and text in tree order: Godot draws each Control in scene order, so a
// ColorRect after a Label covers it. Both lists come out of a screen in member
// (scene) order; at one control the fill comes first, as a Button draws its box
// under its own text.
void UiDrawSequence(const std::vector<UiDrawItem>& items, const std::vector<UiTextRun>& runs,
                    std::vector<UiDrawStep>& out);

class UiScreen {
public:
    // The layout must outlive this screen; it is shared and never copied.
    explicit UiScreen(const assets::UiLayout& layout) : layout_(&layout) {}

    // The tree whose Controls decide visibility and colour. Must outlive the
    // screen. Without one nothing is drawn: there is no second rule.
    void BindTree(const godot::SceneTree* tree);

    // Opens the screen whose controls' paths start with `prefix`. False only
    // when the prefix matches nothing, which means the caller named a screen
    // that does not exist.
    bool Open(const std::string& prefix);

    // Several prefixes at once, as one screen: the union in scene order,
    // deduplicated (a nested prefix must not list a control twice and draw it
    // twice). The shooting interface is two subtrees, brackets and text.
    bool OpenAll(const std::vector<std::string>& prefixes);

    void Close();

    bool IsOpen() const { return !members_.empty(); }
    const std::vector<std::string>& Prefixes() const { return prefixes_; }
    const std::vector<int>& Members() const { return members_; }

    // Members whose path has no godot::Control in the bound tree (all of them
    // when no tree is bound). Those are never drawn.
    int Unbound() const { return unbound_; }

    // The quads to draw, in scene order so later siblings sit on top. Only
    // controls visible in the tree, with a texture (or a ColorRect fill) and a
    // non-zero alpha, are emitted.
    void BuildDrawList(std::vector<UiDrawItem>& out) const;

    // The Labels to draw, in scene order, under the same rule. Called every
    // frame, so it REUSES `out`'s runs and their string buffers.
    void BuildTextList(std::vector<UiTextRun>& out) const;

    // Overrides a Label's text for this screen: how a dialogue line reaches
    // the interface. Cleared by Open().
    bool SetText(int control, std::string_view text);

    // Overrides a control's tree visibility for this screen. Cleared by Open().
    bool SetShown(int control, bool shown);

    // Where the screen reports what it could not draw as asked (a texture a
    // script set that the layout never exported), once per texture.
    void SetLog(std::function<void(const std::string&)> log) { log_ = std::move(log); }

private:
    void Resolve();
    bool ShownOr(int control, bool fallback) const;

    std::function<void(const std::string&)> log_;
    mutable std::set<std::string> missingTextures_;   // already reported
    const assets::UiLayout* layout_ = nullptr;
    const godot::SceneTree* tree_ = nullptr;
    std::vector<std::string> prefixes_;
    std::vector<int> members_;
    std::vector<const godot::Control*> nodes_;   // parallel to members_
    int unbound_ = 0;

    struct TextOverride { int control = -1; std::string text; };
    std::vector<TextOverride> overrides_;

    struct ShownOverride { int control = -1; bool shown = false; };
    std::vector<ShownOverride> shown_;
};

}  // namespace game
