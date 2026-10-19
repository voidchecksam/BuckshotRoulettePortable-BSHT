#include "assets/label3d.h"

#include "core/json_lite.h"

#include <cstdio>
#include <string>
#include <vector>

namespace assets {
namespace {

bool ReadWholeFile(const char* path, std::string& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char buf[8192];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

// ReadStringTopLevel hands back the raw JSON body; decode the escapes the
// extractor writes (\n in multi-line screens, \" and \\ in the ASCII art).
std::string Unescape(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const char c = raw[i];
        if (c != '\\' || i + 1 >= raw.size()) {
            out.push_back(c);
            continue;
        }
        const char n = raw[++i];
        switch (n) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': break;
            default:  out.push_back(n);    break;
        }
    }
    return out;
}

void ReadFloats(const char* b, const char* e, const char* key, float* out, std::size_t n) {
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (!core::json::FindArrayTopLevel(b, e, key, &ab, &ae)) return;
    std::vector<double> v;
    core::json::ReadNumberArray(ab, ae, v);
    if (v.size() < n) return;
    for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<float>(v[i]);
}

void EmitPass(const FontAtlas& atlas, const FontFace& glyphFace, const FontFace& advanceFace,
              const std::string& line, float penX, float baseline, float pixelSize,
              const float colour[4], float alphaScale, bool outline,
              std::vector<LabelQuad>& out) {
    float x = penX;
    for (std::size_t i = 0; i < line.size();) {
        const int ch = NextCodePoint(line, i);
        const Glyph* g = glyphFace.Find(ch);
        // The outline face's glyphs are larger (they carry the outline), but
        // the pen advances by the TEXT glyph, as _generate_glyph_surfaces does.
        const Glyph* adv = advanceFace.Find(ch);
        const bool onPage = g && g->page >= 0 && g->page < static_cast<int>(atlas.Pages().size()) &&
                            atlas.Pages()[static_cast<std::size_t>(g->page)].width > 0;
        if (g && g->HasPixels() && onPage) {
            const FontPage& page = atlas.Pages()[static_cast<std::size_t>(g->page)];
            const float invW = 1.0f / static_cast<float>(page.width);
            const float invH = 1.0f / static_cast<float>(page.height);
            LabelQuad q;
            q.page = g->page;
            q.x0 = x + g->ox * pixelSize;
            q.x1 = q.x0 + static_cast<float>(g->w) * pixelSize;
            // Offsets are measured from the baseline and are negative going up.
            q.yTop = baseline - g->oy * pixelSize;
            q.yBottom = q.yTop - static_cast<float>(g->h) * pixelSize;
            q.u0 = static_cast<float>(g->x) * invW;
            q.v0 = static_cast<float>(g->y) * invH;
            q.u1 = static_cast<float>(g->x + g->w) * invW;
            q.v1 = static_cast<float>(g->y + g->h) * invH;
            for (int i = 0; i < 3; ++i) q.rgba[i] = colour[i];
            q.rgba[3] = colour[3] * alphaScale;
            q.outline = outline;
            out.push_back(q);
        }
        if (adv) x += adv->advance * pixelSize;
    }
}

}  // namespace

bool Labels3D::LoadFromFile(const char* path) {
    std::string json;
    if (!ReadWholeFile(path, json)) return false;
    return LoadFromString(json);
}

bool Labels3D::LoadFromString(const std::string& json) {
    labels_.clear();
    const char* b = json.data();
    const char* e = b + json.size();
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (!core::json::FindArrayTopLevel(b, e, "labels", &ab, &ae)) return false;

    std::vector<core::json::Span> objs;
    core::json::SplitObjects(ab, ae, objs);
    for (const core::json::Span& o : objs) {
        Label3D l;
        l.path = core::json::ReadStringTopLevel(o.first, o.second, "path");
        l.text = Unescape(core::json::ReadStringTopLevel(o.first, o.second, "text"));
        l.font = core::json::ReadStringTopLevel(o.first, o.second, "font");
        l.fontSize = core::json::ReadIntTopLevel(o.first, o.second, "font_size", l.fontSize);
        l.pixelSize = static_cast<float>(
            core::json::ReadNumberTopLevel(o.first, o.second, "pixel_size", l.pixelSize));
        ReadFloats(o.first, o.second, "modulate", l.modulate, 4);
        ReadFloats(o.first, o.second, "outline_modulate", l.outlineModulate, 4);
        l.outlineSize = core::json::ReadIntTopLevel(o.first, o.second, "outline_size", l.outlineSize);
        l.alignH = core::json::ReadIntTopLevel(o.first, o.second, "horizontal_alignment", l.alignH);
        l.alignV = core::json::ReadIntTopLevel(o.first, o.second, "vertical_alignment", l.alignV);
        l.lineSpacing = static_cast<float>(
            core::json::ReadNumberTopLevel(o.first, o.second, "line_spacing", l.lineSpacing));
        l.visible = core::json::ReadBool(o.first, o.second, "visible", l.visible);
        l.transparency = static_cast<float>(
            core::json::ReadNumberTopLevel(o.first, o.second, "transparency", l.transparency));
        l.uppercase = core::json::ReadBool(o.first, o.second, "uppercase", l.uppercase);

        const char* hb = nullptr;
        const char* he = nullptr;
        if (core::json::FindArrayTopLevel(o.first, o.second, "hidden_ancestors", &hb, &he)) {
            core::json::ReadStringArray(hb, he, l.hiddenAncestors);
        }

        float world[16];
        const char* wb = nullptr;
        const char* we = nullptr;
        if (core::json::FindArrayTopLevel(o.first, o.second, "world", &wb, &we)) {
            std::vector<double> v;
            core::json::ReadNumberArray(wb, we, v);
            if (v.size() == 16) {
                for (int i = 0; i < 16; ++i) world[i] = static_cast<float>(v[static_cast<std::size_t>(i)]);
                for (int i = 0; i < 16; ++i) l.world.m[i] = world[i];
                l.hasWorld = true;
            }
        }
        if (!l.path.empty()) labels_.push_back(std::move(l));
    }
    return !labels_.empty();
}

int Labels3D::Find(const std::string& path) const {
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        if (labels_[i].path == path) return static_cast<int>(i);
    }
    return -1;
}

bool Label3DVisible(const Label3D& l,
                    const std::unordered_map<std::string, bool>& nodeOverrides) {
    bool own = l.visible;
    const auto self = nodeOverrides.find(l.path);
    if (self != nodeOverrides.end()) own = self->second;
    if (!own) return false;

    for (const std::string& a : l.hiddenAncestors) {
        const auto it = nodeOverrides.find(a);
        if (it == nodeOverrides.end() || !it->second) return false;
    }
    // An ancestor that started visible and a script has since hidden.
    for (const auto& kv : nodeOverrides) {
        if (kv.second) continue;
        const std::string& p = kv.first;
        if (l.path.size() > p.size() && l.path.compare(0, p.size(), p) == 0 &&
            l.path[p.size()] == '/') {
            return false;
        }
    }
    return true;
}

std::string Label3DFaceKey(const Label3D& l) {
    return l.font + " @ " + std::to_string(l.fontSize);
}

std::string Label3DOutlineFaceKey(const Label3D& l) {
    return Label3DFaceKey(l) + " outline " + std::to_string(l.outlineSize);
}

bool LayoutLabel3D(const FontAtlas& atlas, const Label3D& l, std::vector<LabelQuad>& out) {
    const int fi = atlas.Find(Label3DFaceKey(l));
    if (fi < 0) return false;
    const FontFace& face = atlas.Faces()[static_cast<std::size_t>(fi)];

    const FontFace* outlineFace = nullptr;
    if (l.outlineModulate[3] != 0.0f && l.outlineSize > 0) {
        const int oi = atlas.Find(Label3DOutlineFaceKey(l));
        if (oi >= 0) outlineFace = &atlas.Faces()[static_cast<std::size_t>(oi)];
    }

    std::string text = l.text;
    if (l.uppercase) {
        for (char& c : text) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        }
    }

    // Autowrap is off for every Label3D in the scene; lines break as a Label's do.
    std::vector<std::string> lines;
    ForEachLabelLine(text, [&lines](std::string_view line) { lines.emplace_back(line); });

    const float ps = l.pixelSize;
    // Each line is as tall as the fonts its glyphs come from (LineMetricOf).
    std::vector<FontFace::LineMetric> metrics;
    metrics.reserve(lines.size());
    float totalH = 0.0f;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        metrics.push_back(LineMetricOf(face, lines[i]));
        totalH += (metrics.back().ascent + metrics.back().descent + l.lineSpacing) * ps;
    }

    float vbegin = 0.0f;
    if (l.alignV == 1) vbegin = (totalH - l.lineSpacing * ps) / 2.0f;       // CENTER
    else if (l.alignV == 2) vbegin = totalH - l.lineSpacing * ps;           // BOTTOM

    float alpha = 1.0f - l.transparency;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    float offsetY = vbegin;
    for (std::size_t li = 0; li < lines.size(); ++li) {
        const std::string& line = lines[li];
        float lineWidth = 0.0f;
        for (std::size_t i = 0; i < line.size();) {
            if (const Glyph* g = face.Find(NextCodePoint(line, i))) lineWidth += g->advance;
        }
        lineWidth *= ps;

        float offsetX = 0.0f;                                   // LEFT
        if (l.alignH == 1 || l.alignH == 3) offsetX = -lineWidth / 2.0f;   // CENTER, FILL
        else if (l.alignH == 2) offsetX = -lineWidth;           // RIGHT

        offsetY -= metrics[li].ascent * ps;
        if (outlineFace) {
            EmitPass(atlas, *outlineFace, face, line, offsetX, offsetY, ps,
                     l.outlineModulate, alpha, true, out);
        }
        EmitPass(atlas, face, face, line, offsetX, offsetY, ps, l.modulate, alpha, false, out);
        offsetY -= (metrics[li].descent + l.lineSpacing) * ps;
    }
    return true;
}

}  // namespace assets
