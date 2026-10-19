#include "assets/font.h"

#include <cmath>

#include "core/json_lite.h"

#include <cstdio>
#include <set>

namespace assets {

const char* FontErrorText(FontError e) {
    switch (e) {
        case FontError::None:            return "ok";
        case FontError::FileNotFound:    return "font metrics file not found";
        case FontError::BadJson:         return "font metrics are not the expected JSON";
        case FontError::NoFaces:         return "font metrics contain no faces";
        case FontError::PixelsMissing:   return "font atlas not found";
        case FontError::PixelsTruncated: return "font atlas is smaller than its declared size";
        case FontError::Inconsistent:    return "a glyph lies outside the atlas";
    }
    return "unknown";
}

namespace {

// `note`, when given, says how the read went ("<path>: read N of M bytes").
bool ReadWholeFile(const char* path, std::vector<char>& out, std::string* note = nullptr) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        if (note) *note = std::string(path) + ": cannot open";
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0) {
        if (note) *note = std::string(path) + ": no size (ftell failed)";
        std::fclose(f);
        return false;
    }
    out.resize(static_cast<std::size_t>(n));
    const std::size_t got =
        out.empty() ? 0u : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (note) *note = std::string(path) + ": read " + std::to_string(got) + " of " + std::to_string(out.size()) + " bytes";
    return got == out.size();
}

Glyph ParseGlyph(const char* b, const char* e) {
    Glyph g;
    g.ch = core::json::ReadIntTopLevel(b, e, "char", -1);
    g.present = core::json::ReadBool(b, e, "present", false);
    if (!g.present) return g;
    g.x = core::json::ReadIntTopLevel(b, e, "x", 0);
    g.y = core::json::ReadIntTopLevel(b, e, "y", 0);
    g.w = core::json::ReadIntTopLevel(b, e, "w", 0);
    g.h = core::json::ReadIntTopLevel(b, e, "h", 0);
    g.ox = static_cast<float>(core::json::ReadNumberTopLevel(b, e, "ox", 0.0));
    g.oy = static_cast<float>(core::json::ReadNumberTopLevel(b, e, "oy", 0.0));
    g.advance = static_cast<float>(core::json::ReadNumberTopLevel(b, e, "advance", 0.0));
    g.page = core::json::ReadIntTopLevel(b, e, "page", 0);
    g.font = core::json::ReadIntTopLevel(b, e, "font", 0);
    return g;
}

// Every glyph object in the array `key` of the span.
std::vector<Glyph> ParseGlyphArray(const char* b, const char* e, const char* key) {
    std::vector<Glyph> out;
    const char* gb = nullptr;
    const char* ge = nullptr;
    if (!core::json::FindArrayTopLevel(b, e, key, &gb, &ge)) return out;
    std::vector<core::json::Span> gs;
    core::json::SplitObjects(gb, ge, gs);
    out.reserve(gs.size());
    for (const core::json::Span& go : gs) out.push_back(ParseGlyph(go.first, go.second));
    return out;
}

std::string DirectoryOf(const char* path) {
    const std::string p(path);
    const std::size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
}

}  // namespace

int NextCodePoint(std::string_view text, std::size_t& i) {
    const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(text[k]); };
    const unsigned char b0 = byte(i);
    int len = 0;
    int cp = 0;
    if (b0 < 0x80) { ++i; return b0; }
    if ((b0 & 0xE0) == 0xC0) { len = 2; cp = b0 & 0x1F; }
    else if ((b0 & 0xF0) == 0xE0) { len = 3; cp = b0 & 0x0F; }
    else if ((b0 & 0xF8) == 0xF0) { len = 4; cp = b0 & 0x07; }
    else { ++i; return -1; }
    if (i + static_cast<std::size_t>(len) > text.size()) { ++i; return -1; }
    for (int k = 1; k < len; ++k) {
        const unsigned char b = byte(i + static_cast<std::size_t>(k));
        if ((b & 0xC0) != 0x80) { ++i; return -1; }
        cp = (cp << 6) | (b & 0x3F);
    }
    i += static_cast<std::size_t>(len);
    return cp;
}

const Glyph* FontFace::Find(int ch) const {
    const int i = ch - firstChar;
    if (i >= 0 && i < static_cast<int>(glyphs.size())) {
        const Glyph& g = glyphs[static_cast<std::size_t>(i)];
        if (g.present) return &g;
    }
    if (const auto it = extra.find(ch); it != extra.end() && it->second.present) return &it->second;
    if (active) {
        if (const auto it = active->find(ch); it != active->end() && it->second.present) {
            return &it->second;
        }
    }
    return nullptr;
}

void FontAtlas::ReleasePixels() {
    for (FontPage& p : pages_) std::vector<uint8_t>().swap(p.pixels);
}

const std::vector<uint8_t>& FontAtlas::Pixels() const {
    static const std::vector<uint8_t> kNone;
    return pages_.empty() ? kNone : pages_[0].pixels;
}

bool FontAtlas::ReadPage(FontPage& page) const {
    std::vector<char> raw;
    if (!ReadWholeFile((directory_ + "/" + page.file).c_str(), raw)) return false;
    const std::size_t expect = static_cast<std::size_t>(page.width) *
                               static_cast<std::size_t>(page.height) *
                               static_cast<std::size_t>(kFontAtlasChannels);
    if (raw.size() < expect) return false;
    page.pixels.assign(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(expect));
    return true;
}

FontError FontAtlas::Load(const char* jsonPath, const char* pixelsPath) {
    faces_.clear();
    pages_.clear();
    activeGroup_.clear();
    reported_.clear();
    directory_ = DirectoryOf(jsonPath);

    std::vector<char> text;
    if (!ReadWholeFile(jsonPath, text, &loadNote_) || text.empty()) {
        return FontError::FileNotFound;
    }
    const char* b = text.data();
    const char* e = b + text.size();

    width_  = core::json::ReadIntTopLevel(b, e, "atlas_width", 0);
    height_ = core::json::ReadIntTopLevel(b, e, "atlas_height", 0);
    const int firstChar = core::json::ReadIntTopLevel(b, e, "first_char", 32);
    const int lastChar  = core::json::ReadIntTopLevel(b, e, "last_char", 126);
    if (lastChar < firstChar) return FontError::BadJson;

    // The page table. An export from before the pages existed is one page.
    const char* pb = nullptr;
    const char* pe = nullptr;
    if (core::json::FindArrayTopLevel(b, e, "pages", &pb, &pe)) {
        std::vector<core::json::Span> ps;
        core::json::SplitObjects(pb, pe, ps);
        for (const core::json::Span& s : ps) {
            FontPage page;
            page.group  = core::json::ReadStringTopLevel(s.first, s.second, "group");
            page.file   = core::json::ReadStringTopLevel(s.first, s.second, "file");
            page.width  = core::json::ReadIntTopLevel(s.first, s.second, "width", 0);
            page.height = core::json::ReadIntTopLevel(s.first, s.second, "height", 0);
            pages_.push_back(std::move(page));
        }
    }
    if (pages_.empty()) {
        FontPage page;
        page.group = "base";
        page.width = width_;
        page.height = height_;
        pages_.push_back(std::move(page));
    }
    std::set<std::string> groupNames;
    for (const FontPage& p : pages_) {
        if (p.group != "base") groupNames.insert(p.group);
    }

    const char* fb = nullptr;
    const char* fe = nullptr;
    if (!core::json::FindArrayTopLevel(b, e, "faces", &fb, &fe)) {
        return FontError::BadJson;
    }
    std::vector<core::json::Span> items;
    core::json::SplitObjects(fb, fe, items);
    if (items.empty()) return FontError::NoFaces;

    faces_.reserve(items.size());
    for (const core::json::Span& s : items) {
        FontFace face;
        face.key     = core::json::ReadStringTopLevel(s.first, s.second, "key");
        face.path    = core::json::ReadStringTopLevel(s.first, s.second, "path");
        face.size    = core::json::ReadIntTopLevel(s.first, s.second, "size", 0);
        face.ascent  = static_cast<float>(
            core::json::ReadNumberTopLevel(s.first, s.second, "ascent", 0.0));
        face.descent = static_cast<float>(
            core::json::ReadNumberTopLevel(s.first, s.second, "descent", 0.0));
        face.height  = static_cast<float>(
            core::json::ReadNumberTopLevel(s.first, s.second, "height", 0.0));
        face.firstChar = firstChar;
        const char* mb = nullptr;
        const char* me = nullptr;
        if (core::json::FindArrayTopLevel(s.first, s.second, "line_metrics", &mb, &me)) {
            std::vector<double> flat;
            core::json::ReadNumberArray(mb, me, flat);
            for (std::size_t i = 0; i + 1 < flat.size(); i += 2) {
                face.lineMetrics.push_back({static_cast<float>(flat[i]), static_cast<float>(flat[i + 1])});
            }
        }
        // An export from before the line metrics: every line as tall as the face.
        if (face.lineMetrics.empty()) face.lineMetrics.push_back({face.ascent, face.descent});

        // Dense and pre-sized, so a character maps to a slot by subtraction
        // rather than a search. Absent characters keep `present == false`.
        face.glyphs.assign(static_cast<std::size_t>(lastChar - firstChar + 1),
                           Glyph{});
        for (int c = firstChar; c <= lastChar; ++c) {
            face.glyphs[static_cast<std::size_t>(c - firstChar)].ch = c;
        }
        for (const Glyph& g : ParseGlyphArray(s.first, s.second, "glyphs")) {
            const int slot = g.ch - firstChar;
            if (slot < 0 || slot >= static_cast<int>(face.glyphs.size())) continue;
            face.glyphs[static_cast<std::size_t>(slot)] = g;
        }
        for (const Glyph& g : ParseGlyphArray(s.first, s.second, "extra")) {
            if (g.present) face.extra[g.ch] = g;
        }
        const char* gb = nullptr;
        const char* ge = nullptr;
        if (!groupNames.empty() &&
            core::json::FindObject(s.first, s.second, "groups", &gb, &ge)) {
            for (const std::string& name : groupNames) {
                for (const Glyph& g : ParseGlyphArray(gb, ge, name.c_str())) {
                    if (g.present) face.groups[name][g.ch] = g;
                }
            }
        }
        faces_.push_back(std::move(face));
    }

    // A glyph reaching past its page would sample whatever follows it in
    // memory. Caught here rather than as garbage pixels on a device.
    const auto inside = [this](const Glyph& g) {
        if (g.font < 0) return false;
        if (!g.HasPixels()) return true;
        if (g.page < 0 || g.page >= static_cast<int>(pages_.size())) return false;
        const FontPage& p = pages_[static_cast<std::size_t>(g.page)];
        if (p.width <= 0 || p.height <= 0) return true;   // metrics-only export
        return g.x >= 0 && g.y >= 0 && g.x + g.w <= p.width && g.y + g.h <= p.height;
    };
    for (const FontFace& f : faces_) {
        const auto ok = [&](const Glyph& g) {
            return inside(g) && g.font < static_cast<int>(f.lineMetrics.size());
        };
        for (const Glyph& g : f.glyphs) {
            if (!ok(g)) return FontError::Inconsistent;
        }
        for (const auto& [ch, g] : f.extra) {
            if (!ok(g)) return FontError::Inconsistent;
        }
        for (const auto& [name, set] : f.groups) {
            for (const auto& [ch, g] : set) {
                if (!ok(g)) return FontError::Inconsistent;
            }
        }
    }

    if (!pixelsPath) return FontError::None;

    // Page 0 from the path given (the entry point reads it for the loading
    // screen); the other base pages from beside the metrics.
    std::vector<char> raw;
    if (!ReadWholeFile(pixelsPath, raw)) return FontError::PixelsMissing;
    FontPage& first = pages_[0];
    first.pixels.assign(raw.begin(), raw.end());
    const std::size_t expect = static_cast<std::size_t>(first.width) *
                               static_cast<std::size_t>(first.height) *
                               static_cast<std::size_t>(kFontAtlasChannels);
    if (first.pixels.size() < expect) return FontError::PixelsTruncated;
    for (std::size_t i = 1; i < pages_.size(); ++i) {
        if (pages_[i].group != "base") continue;
        if (!ReadPage(pages_[i])) return FontError::PixelsMissing;
    }
    return FontError::None;
}

bool FontAtlas::LoadGroup(const std::string& group) {
    if (!group.empty() && group == activeGroup_) return true;
    UnloadGroup();
    if (group.empty() || group == "base") return false;
    bool any = false;
    for (FontPage& p : pages_) {
        if (p.group != group) continue;
        any = true;
        if (!ReadPage(p)) {
            for (FontPage& q : pages_) {
                if (q.group == group) std::vector<uint8_t>().swap(q.pixels);
            }
            return false;
        }
    }
    if (!any) return false;
    activeGroup_ = group;
    for (FontFace& f : faces_) {
        const auto it = f.groups.find(group);
        f.active = it == f.groups.end() ? nullptr : &it->second;
    }
    return true;
}

void FontAtlas::UnloadGroup() {
    if (activeGroup_.empty()) return;
    for (FontPage& p : pages_) {
        if (p.group == activeGroup_) std::vector<uint8_t>().swap(p.pixels);
    }
    for (FontFace& f : faces_) f.active = nullptr;
    activeGroup_.clear();
}

const Glyph* FontAtlas::FindOrReport(const FontFace& f, int ch) const {
    const Glyph* g = ch >= 0 ? f.Find(ch) : nullptr;
    if (!g && ch >= 32 && reported_.insert(ch).second && log_) {
        char buf[48];
        std::snprintf(buf, sizeof buf, "fonts: no glyph for U+%04X in ", static_cast<unsigned>(ch));
        log_(std::string(buf) + f.key);
    }
    return g;
}

int FontAtlas::Find(const std::string& key) const {
    for (std::size_t i = 0; i < faces_.size(); ++i) {
        if (faces_[i].key == key) return static_cast<int>(i);
    }
    return -1;
}

int FontAtlas::LineCount(std::string_view text) const {
    int lines = 0;
    ForEachLabelLine(text, [&lines](std::string_view) { ++lines; });
    return lines;
}

float FontAtlas::LineWidth(const FontFace& f, std::string_view line) const {
    float w = 0.0f;
    for (std::size_t i = 0; i < line.size();) {
        const Glyph* g = FindOrReport(f, NextCodePoint(line, i));
        if (g) w += g->advance;
    }
    return w;
}

std::string FontAtlas::TrimToWidth(int face, std::string_view text, float width) const {
    if (face < 0 || face >= static_cast<int>(faces_.size()) || width <= 0.0f) return std::string(text);
    const FontFace& f = faces_[static_cast<std::size_t>(face)];
    std::string out;
    out.reserve(text.size());
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find('\n', start);
        const std::string_view line =
            text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        float w = 0.0f;
        std::size_t kept = 0;
        for (std::size_t i = 0; i < line.size();) {
            const Glyph* g = FindOrReport(f, NextCodePoint(line, i));
            if (g) w += g->advance;
            if (w > width) break;
            kept = i;
        }
        out.append(line.substr(0, kept));
        if (end == std::string_view::npos) break;
        out.push_back('\n');
        start = end + 1;
    }
    return out;
}

float FontAtlas::MeasureWidth(int face, std::string_view text) const {
    if (face < 0 || face >= static_cast<int>(faces_.size())) return 0.0f;
    const FontFace& f = faces_[static_cast<std::size_t>(face)];

    float widest = 0.0f;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find('\n', start);
        const float w = LineWidth(f, text.substr(start, end == std::string_view::npos
                                                            ? std::string_view::npos
                                                            : end - start));
        if (w > widest) widest = w;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return widest;
}

float FontAtlas::MeasureHeight(int face, std::string_view text, int lineSpacing) const {
    if (face < 0 || face >= static_cast<int>(faces_.size())) return 0.0f;
    const FontFace& f = faces_[static_cast<std::size_t>(face)];
    float total = 0.0f;
    ForEachLabelLine(text, [&](std::string_view line) {
        const FontFace::LineMetric m = LineMetricOf(f, line);
        total += std::ceil(m.ascent + m.descent) + static_cast<float>(lineSpacing);
    });
    return total;
}

// text_server_adv.cpp _shape_substr: a line's ascent and descent are the
// largest of the fonts its glyphs come from.
FontFace::LineMetric LineMetricOf(const FontFace& f, std::string_view line) {
    const FontFace::LineMetric own = f.lineMetrics.empty() ? FontFace::LineMetric{f.ascent, f.descent} : f.lineMetrics[0];
    FontFace::LineMetric m;
    bool any = false;
    for (std::size_t i = 0; i < line.size();) {
        const Glyph* g = f.Find(NextCodePoint(line, i));
        if (!g) continue;
        const bool fallback = g->font > 0 && g->font < static_cast<int>(f.lineMetrics.size());
        const FontFace::LineMetric& r = fallback ? f.lineMetrics[static_cast<std::size_t>(g->font)] : own;
        if (r.ascent > m.ascent) m.ascent = r.ascent;
        if (r.descent > m.descent) m.descent = r.descent;
        any = true;
    }
    return any ? m : own;
}

// label.cpp NOTIFICATION_DRAW: each line is as tall as its own fonts, lines
// sit line_spacing apart, and the block's offsets are whole pixels. Godot also
// stops drawing lines that overflow the rect; the exported rect is the Label's
// final size, which Godot grows to fit its text, so nothing overflows here.
void FontAtlas::Layout(int face, std::string_view text,
                       float rx, float ry, float rw, float rh,
                       AlignH ah, AlignV av,
                       std::vector<TextQuad>& out, int lineSpacing) const {
    if (face < 0 || face >= static_cast<int>(faces_.size())) return;
    if (pages_.empty()) return;
    const FontFace& f = faces_[static_cast<std::size_t>(face)];
    if (text.empty()) return;

    const float spacing = static_cast<float>(lineSpacing);
    // Top-aligned text (most of it, every frame) needs neither.
    const int lines = av == AlignV::Fill ? LineCount(text) : 0;
    const float total = av == AlignV::Top ? 0.0f : MeasureHeight(face, text, lineSpacing);
    int vbegin = 0;
    int vsep = 0;
    switch (av) {
        case AlignV::Top:    break;
        case AlignV::Centre: vbegin = static_cast<int>((rh - (total - spacing)) / 2.0f); break;
        case AlignV::Bottom: vbegin = static_cast<int>(rh - (total - spacing));          break;
        case AlignV::Fill:
            if (lines > 1) vsep = static_cast<int>((rh - (total - spacing)) / static_cast<float>(lines - 1));
            break;
    }

    float y = ry + static_cast<float>(vbegin);
    // Views, not copies: this runs for every label, every frame.
    ForEachLabelLine(text, [&](std::string_view line) {
        const FontFace::LineMetric m = LineMetricOf(f, line);
        y += m.ascent;   // the line's baseline: glyph offsets hang off it

        // _shaped_text_get_size rounds the width up.
        const float lw = std::ceil(LineWidth(f, line));
        float penX = rx;
        switch (ah) {
            case AlignH::Left:   break;
            case AlignH::Centre: penX += static_cast<float>(static_cast<int>(rw - lw) / 2); break;
            case AlignH::Right:  penX += static_cast<float>(static_cast<int>(rw - lw));     break;
            case AlignH::Fill:   break;
        }

        for (std::size_t i = 0; i < line.size();) {
            const Glyph* g = FindOrReport(f, NextCodePoint(line, i));
            if (!g) continue;              // absent: leave a gap, do not shift
            if (g->HasPixels() && g->page >= 0 && g->page < static_cast<int>(pages_.size())) {
                const FontPage& page = pages_[static_cast<std::size_t>(g->page)];
                if (page.width > 0 && page.height > 0) {
                    const float invW = 1.0f / static_cast<float>(page.width);
                    const float invH = 1.0f / static_cast<float>(page.height);
                    TextQuad q;
                    q.page = g->page;
                    q.x = penX + g->ox;
                    q.y = y + g->oy;
                    q.w = static_cast<float>(g->w);
                    q.h = static_cast<float>(g->h);
                    q.u0 = static_cast<float>(g->x) * invW;
                    q.v0 = static_cast<float>(g->y) * invH;
                    q.u1 = static_cast<float>(g->x + g->w) * invW;
                    q.v1 = static_cast<float>(g->y + g->h) * invH;
                    out.push_back(q);
                }
            }
            penX += g->advance;
        }

        y += m.descent + static_cast<float>(vsep) + spacing;
    });
}

}  // namespace assets
