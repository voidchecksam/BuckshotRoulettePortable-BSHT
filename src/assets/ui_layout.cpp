#include "assets/ui_layout.h"

#include "core/json_lite.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace assets {

const char* UiLayoutErrorText(UiLayoutError e) {
    switch (e) {
        case UiLayoutError::None:            return "ok";
        case UiLayoutError::FileNotFound:    return "layout file not found";
        case UiLayoutError::BadJson:         return "layout is not the expected JSON";
        case UiLayoutError::NoControls:      return "layout has no controls";
        case UiLayoutError::PixelsMissing:   return "pixel pool not found";
        case UiLayoutError::PixelsTruncated: return "a texture runs past the end of the pixel pool";
        case UiLayoutError::Inconsistent:    return "a texture's size does not match its byte count";
    }
    return "unknown";
}

namespace {

bool ReadWholeFile(const char* path, std::vector<char>& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0) { std::fclose(f); return false; }
    out.resize(static_cast<std::size_t>(n));
    const std::size_t got = out.empty()
        ? 0u : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

void ReadColour(const char* b, const char* e, const char* key, float* out) {
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (!core::json::FindArray(b, e, key, &ab, &ae)) return;
    std::vector<double> v;
    core::json::ReadNumberArray(ab, ae, v);
    for (std::size_t i = 0; i < 4 && i < v.size(); ++i) {
        out[i] = static_cast<float>(v[i]);
    }
}

}  // namespace

UiLayoutError UiLayout::Load(const char* layoutPath, const char* pixelsPath) {
    controls_.clear();
    textures_.clear();
    chunks_.clear();

    std::vector<char> text;
    if (!ReadWholeFile(layoutPath, text) || text.empty()) {
        return UiLayoutError::FileNotFound;
    }
    const char* b = text.data();
    const char* e = b + text.size();

    designWidth_  = core::json::ReadIntTopLevel(b, e, "design_width", 0);
    designHeight_ = core::json::ReadIntTopLevel(b, e, "design_height", 0);

    // Textures first: a control's `texture` index refers into this list.
    const char* tb = nullptr;
    const char* te = nullptr;
    if (core::json::FindArrayTopLevel(b, e, "textures", &tb, &te)) {
        std::vector<core::json::Span> items;
        core::json::SplitObjects(tb, te, items);
        textures_.reserve(items.size());
        for (const core::json::Span& s : items) {
            UiTexture t;
            t.key    = core::json::ReadStringTopLevel(s.first, s.second, "key");
            t.width  = core::json::ReadIntTopLevel(s.first, s.second, "width", 0);
            t.height = core::json::ReadIntTopLevel(s.first, s.second, "height", 0);
            t.offset = static_cast<uint32_t>(
                core::json::ReadIntTopLevel(s.first, s.second, "offset", 0));
            t.bytes  = static_cast<uint32_t>(
                core::json::ReadIntTopLevel(s.first, s.second, "bytes", 0));
            if (!t.Valid()) return UiLayoutError::Inconsistent;
            textures_.push_back(std::move(t));
        }
    }

    const char* cb = nullptr;
    const char* ce = nullptr;
    if (!core::json::FindArrayTopLevel(b, e, "controls", &cb, &ce)) {
        return UiLayoutError::BadJson;
    }
    std::vector<core::json::Span> items;
    core::json::SplitObjects(cb, ce, items);
    if (items.empty()) return UiLayoutError::NoControls;

    controls_.reserve(items.size());
    for (const core::json::Span& s : items) {
        UiControl c;
        // Top-level reads throughout: the entry contains nested arrays, and a
        // plain scan would happily find a key inside one of them.
        c.path = core::json::ReadStringTopLevel(s.first, s.second, "path");
        c.name = core::json::ReadStringTopLevel(s.first, s.second, "name");
        c.type = core::json::ReadStringTopLevel(s.first, s.second, "type");
        c.text = core::json::ReadStringTopLevel(s.first, s.second, "text");

        c.x = static_cast<float>(core::json::ReadNumberTopLevel(s.first, s.second, "x", 0.0));
        c.y = static_cast<float>(core::json::ReadNumberTopLevel(s.first, s.second, "y", 0.0));
        c.w = static_cast<float>(core::json::ReadNumberTopLevel(s.first, s.second, "w", 0.0));
        c.h = static_cast<float>(core::json::ReadNumberTopLevel(s.first, s.second, "h", 0.0));

        c.visible       = core::json::ReadBool(s.first, s.second, "visible", false);
        c.visibleInTree = core::json::ReadBool(s.first, s.second, "visible_in_tree", false);

        ReadColour(s.first, s.second, "modulate", c.modulate);
        ReadColour(s.first, s.second, "self_modulate", c.selfModulate);
        {
            const char* ab = nullptr;
            const char* ae = nullptr;
            if (core::json::FindArray(s.first, s.second, "scale", &ab, &ae)) {
                std::vector<double> v;
                core::json::ReadNumberArray(ab, ae, v);
                // A zero scale would divide the layout box by zero.
                if (v.size() >= 2 && v[0] > 0.0 && v[1] > 0.0) {
                    c.scale[0] = static_cast<float>(v[0]);
                    c.scale[1] = static_cast<float>(v[1]);
                }
            }
        }

        c.focusMode = static_cast<FocusMode>(
            core::json::ReadIntTopLevel(s.first, s.second, "focus_mode", 0));

        c.texture = core::json::ReadIntTopLevel(s.first, s.second, "texture", -1);
        if (c.texture >= static_cast<int>(textures_.size())) c.texture = -1;

        c.focusTop    = core::json::ReadIntTopLevel(s.first, s.second, "focus_top_index", -1);
        c.focusBottom = core::json::ReadIntTopLevel(s.first, s.second, "focus_bottom_index", -1);
        c.focusLeft   = core::json::ReadIntTopLevel(s.first, s.second, "focus_left_index", -1);
        c.focusRight  = core::json::ReadIntTopLevel(s.first, s.second, "focus_right_index", -1);

        if (c.type == "ColorRect" && core::json::HasKey(s.first, s.second, "color")) {
            ReadColour(s.first, s.second, "color", c.colour);
            c.hasColour = true;
        }

        if (c.type == "Label") {
            c.fontKey = core::json::ReadStringTopLevel(s.first, s.second, "font");
            c.fontSize = core::json::ReadIntTopLevel(s.first, s.second, "font_size", 0);
            ReadColour(s.first, s.second, "font_colour", c.fontColour);
            c.alignH = core::json::ReadIntTopLevel(s.first, s.second, "align_h", 0);
            c.alignV = core::json::ReadIntTopLevel(s.first, s.second, "align_v", 0);
            c.overrun = core::json::ReadIntTopLevel(s.first, s.second, "overrun", 0);
            c.lineSpacing = core::json::ReadIntTopLevel(s.first, s.second, "line_spacing", kDefaultLineSpacing);
        }

        c.isButton = core::json::HasKey(s.first, s.second, "button_class");
        if (c.isButton) {
            c.isActive  = core::json::ReadBool(s.first, s.second, "is_active", false);
            c.isDynamic = core::json::ReadBool(s.first, s.second, "is_dynamic", false);
            c.opacityInactive = static_cast<float>(
                core::json::ReadNumberTopLevel(s.first, s.second, "opacity_inactive", 1.0));
            c.opacityActive = static_cast<float>(
                core::json::ReadNumberTopLevel(s.first, s.second, "opacity_active", 1.0));
            c.highlight = core::json::ReadIntTopLevel(s.first, s.second, "highlight_index", -1);
            c.is3d = core::json::ReadBool(s.first, s.second, "is_3d", false);
            c.highlight3d = core::json::ReadStringTopLevel(s.first, s.second, "highlight_3d");
        }

        controls_.push_back(std::move(c));
    }

    // A link that points outside the table would index out of bounds later, at
    // a moment when the cause is no longer visible. Clear it here instead.
    const int n = static_cast<int>(controls_.size());
    for (UiControl& c : controls_) {
        if (c.focusTop    >= n) c.focusTop    = -1;
        if (c.focusBottom >= n) c.focusBottom = -1;
        if (c.focusLeft   >= n) c.focusLeft   = -1;
        if (c.focusRight  >= n) c.focusRight  = -1;
        if (c.highlight   >= n) c.highlight    = -1;
    }

    if (!pixelsPath) return UiLayoutError::None;

    // Each texture straight from the file into a buffer of its own: the whole
    // pool read into one vector and copied into another (17.6 MB twice) fails
    // in a fragmented heap on the way back to the menu.
    std::FILE* f = std::fopen(pixelsPath, "rb");
    if (!f) return UiLayoutError::PixelsMissing;
    std::fseek(f, 0, SEEK_END);
    const long fileBytes = std::ftell(f);
    chunks_.assign(textures_.size(), {});
    for (std::size_t i = 0; i < textures_.size(); ++i) {
        const UiTexture& t = textures_[i];
        const long endByte = static_cast<long>(t.offset) + static_cast<long>(t.bytes);
        if (fileBytes < 0 || endByte > fileBytes) {
            std::fclose(f);
            chunks_.clear();
            return UiLayoutError::PixelsTruncated;
        }
        chunks_[i].resize(t.bytes);
        if (std::fseek(f, static_cast<long>(t.offset), SEEK_SET) != 0 ||
            (t.bytes && std::fread(chunks_[i].data(), 1, t.bytes, f) != t.bytes)) {
            std::fclose(f);
            chunks_.clear();
            return UiLayoutError::PixelsTruncated;
        }
    }
    std::fclose(f);
    return UiLayoutError::None;
}

int UiLayout::Find(const std::string& path) const {
    for (std::size_t i = 0; i < controls_.size(); ++i) {
        if (controls_[i].path == path) return static_cast<int>(i);
    }
    return -1;
}

int UiLayout::FindTexture(const std::string& key) const {
    for (std::size_t i = 0; i < textures_.size(); ++i) {
        if (textures_[i].key == key) return static_cast<int>(i);
    }
    return -1;
}

std::vector<int> UiLayout::FindByPrefix(const std::string& prefix) const {
    std::vector<int> out;
    for (std::size_t i = 0; i < controls_.size(); ++i) {
        const std::string& p = controls_[i].path;
        if (p.size() >= prefix.size() &&
            p.compare(0, prefix.size(), prefix) == 0) {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

const uint8_t* UiLayout::TexturePixels(int textureIndex) const {
    if (textureIndex < 0 || textureIndex >= static_cast<int>(textures_.size())) {
        return nullptr;
    }
    const std::size_t i = static_cast<std::size_t>(textureIndex);
    if (i >= chunks_.size()) return nullptr;
    const std::vector<uint8_t>& c = chunks_[i];
    return c.size() == textures_[i].bytes && !c.empty() ? c.data() : nullptr;
}

int UiLayout::Neighbour(int from, Direction d) const {
    if (from < 0 || from >= static_cast<int>(controls_.size())) return -1;
    const UiControl& c = controls_[static_cast<std::size_t>(from)];

    (void)c;
    // The scene's own wiring wins. 185 of these links were placed by hand and
    // encode intent that geometry cannot: which bracket follows which, and
    // where a row wraps.
    const int link = ExplicitLink(from, d);
    if (link >= 0) return link;

    return NearestInDirection(from, d);
}

int UiLayout::ExplicitLink(int from, Direction d) const {
    if (from < 0 || from >= static_cast<int>(controls_.size())) return -1;
    const UiControl& c = controls_[static_cast<std::size_t>(from)];
    switch (d) {
        case Direction::Up:    return c.focusTop;
        case Direction::Down:  return c.focusBottom;
        case Direction::Left:  return c.focusLeft;
        case Direction::Right: return c.focusRight;
    }
    return -1;
}

// Shared scoring for both overloads, so the restricted form cannot drift from
// the open one and quietly navigate differently inside a screen.
bool UiLayout::Scores(int from, int to, Direction d, float* outScore) const {
    const UiControl& a = controls_[static_cast<std::size_t>(from)];
    const UiControl& b = controls_[static_cast<std::size_t>(to)];
    if (!b.CanFocus()) return false;

    const float dx = b.CentreX() - a.CentreX();
    const float dy = b.CentreY() - a.CentreY();

    // "In this direction" means the dominant axis agrees. A control that is
    // slightly up and far left is a Left neighbour, not an Up one, which is
    // what stops one press from jumping across the screen.
    float along = 0.0f;
    float across = 0.0f;
    switch (d) {
        case Direction::Up:    along = -dy; across = std::fabs(dx); break;
        case Direction::Down:  along =  dy; across = std::fabs(dx); break;
        case Direction::Left:  along = -dx; across = std::fabs(dy); break;
        case Direction::Right: along =  dx; across = std::fabs(dy); break;
    }
    if (along <= 0.0f) return false;
    if (across > along) return false;

    // Off-axis drift is weighted heavily so a control directly ahead beats a
    // nearer one out to the side.
    *outScore = along + across * 2.0f;
    return true;
}

int UiLayout::NearestInDirection(int from, Direction d,
                                 const std::vector<int>& candidates) const {
    if (from < 0 || from >= static_cast<int>(controls_.size())) return -1;

    int best = -1;
    float bestScore = std::numeric_limits<float>::max();
    for (int i : candidates) {
        if (i == from) continue;
        if (i < 0 || i >= static_cast<int>(controls_.size())) continue;
        float score = 0.0f;
        if (!Scores(from, i, d, &score)) continue;
        if (score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}

int UiLayout::Neighbour(int from, Direction d,
                        const std::vector<int>& candidates) const {
    const int link = ExplicitLink(from, d);
    if (link >= 0) {
        for (int i : candidates) {
            if (i == link) return link;
        }
        // The scene points somewhere that is not on screen. Falling through to
        // geometry keeps the d-pad working instead of dead-ending.
    }
    return NearestInDirection(from, d, candidates);
}

int UiLayout::NearestInDirection(int from, Direction d) const {
    if (from < 0 || from >= static_cast<int>(controls_.size())) return -1;

    int best = -1;
    float bestScore = std::numeric_limits<float>::max();

    for (std::size_t i = 0; i < controls_.size(); ++i) {
        if (static_cast<int>(i) == from) continue;

        float score = 0.0f;
        if (!Scores(from, static_cast<int>(i), d, &score)) continue;
        if (score < bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

}  // namespace assets
