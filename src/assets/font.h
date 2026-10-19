// The UI font atlas and text layout.
//
// The glyphs are Godot's. The port links no font library: every face is
// rasterised once by Godot, the rasteriser the original game draws with, and
// shipped as LUMINANCE_ALPHA pages with per-glyph metrics. stb_truetype would
// be nearly identical, and nearly is the problem: hinting and advance rounding
// differ, so text would land a pixel out at every size.
//
// Pages come in groups. "base" (ASCII, Latin accents, Cyrillic) is always
// resident; a CJK locale's glyphs live in a group named for it (JA, KO, ZHS,
// ZHT) whose pages are read only while that locale is chosen. A glyph knows
// its page; the renderers switch texture on a page change.
//
// Text is UTF-8. A code point with no glyph leaves a gap and is logged once.
//
// Layout (line breaking, alignment, glyph metrics to quads) lives here rather
// than in the renderer because it is arithmetic worth testing without a
// window: a string that lays out to nothing looks the same as a font that
// failed to load.
//
// Headless: no GL, no SDL.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <string_view>
#include <vector>

namespace assets {

enum class FontError {
    None = 0,
    FileNotFound,
    BadJson,
    NoFaces,
    PixelsMissing,
    PixelsTruncated,
    Inconsistent,
};

const char* FontErrorText(FontError e);

// Bytes per pixel in the atlas: luminance (always 255) then coverage.
inline constexpr int kFontAtlasChannels = 2;

struct Glyph {
    int   ch      = 0;
    bool  present = false;

    // Position and size in the atlas, in pixels.
    int   x = 0, y = 0, w = 0, h = 0;

    // Offset from the pen position to the glyph's top-left. `oy` is NEGATIVE
    // for anything above the baseline, which is nearly everything.
    float ox = 0.0f, oy = 0.0f;

    // How far the pen moves. A space has a real advance and no pixels.
    float advance = 0.0f;

    // Which atlas page holds the pixels (FontAtlas::Pages()).
    int page = 0;

    // Which of the face's line metrics its font has: 0 is the face's own
    // font, more is the fallback that supplied it.
    int font = 0;

    bool HasPixels() const { return present && w > 0 && h > 0; }
};

struct FontFace {
    std::string key;      // "res://fonts/fake receipt.otf @ 23", the join key
    std::string path;
    int   size    = 0;
    float ascent  = 0.0f;
    float descent = 0.0f;
    float height  = 0.0f;

    // A Label line is as tall as the fonts its glyphs come from
    // (text_server_adv.cpp _shape_substr). Row 0 is the face's own font, the
    // rest the fallbacks a Glyph::font names. ascent/descent/height above are
    // Font.get_*, the tallest of every fallback, which no Label line uses.
    struct LineMetric {
        float ascent  = 0.0f;
        float descent = 0.0f;
    };
    std::vector<LineMetric> lineMetrics;

    int firstChar = 32;
    std::vector<Glyph> glyphs;   // dense, indexed by ch - firstChar
    // Base glyphs outside the dense range (accents, Cyrillic), by code point.
    std::unordered_map<int, Glyph> extra;
    // Per CJK group, by code point. Consulted only for the atlas's active group.
    std::map<std::string, std::unordered_map<int, Glyph>> groups;
    // Set by FontAtlas::LoadGroup; null when no group is loaded.
    const std::unordered_map<int, Glyph>* active = nullptr;

    // Null when the face has no glyph for the code point (in the dense range,
    // the extra set or the loaded group). Callers skip it rather than
    // substituting, so a missing glyph leaves a gap instead of silently
    // shifting the rest of the line.
    const Glyph* Find(int ch) const;
};

// One LA8 page of the atlas. `pixels` is empty while the page is not resident.
struct FontPage {
    std::string group;
    std::string file;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

// The next code point of UTF-8 `text` at `i`, advancing `i`. A malformed or
// truncated sequence yields -1 and skips one byte.
int NextCodePoint(std::string_view text, std::size_t& i);

// Where a glyph goes on screen and where to read it from. Design-space pixels
// and normalised UVs, so this feeds the sprite batch unchanged.
struct TextQuad {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    int page = 0;
};

// A Label's lines as Godot breaks them (label.cpp _shape, label_3d.cpp:
// BREAK_MANDATORY | BREAK_TRIM_EDGE_SPACES, text_server.cpp
// shaped_text_get_line_breaks): split at newlines, each line without its
// newline and, when a newline ends it, without trailing spaces. A text that
// ends in a newline has no line after it; "" is one empty line.
template <class Fn>
void ForEachLabelLine(std::string_view text, Fn&& fn) {
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            if (start < text.size() || start == 0) fn(text.substr(start));
            return;
        }
        std::string_view line = text.substr(start, end - start);
        while (!line.empty() && line.back() == ' ') line.remove_suffix(1);
        fn(line);
        start = end + 1;
    }
}

// A Label line's ascent and descent: the largest of the fonts its glyphs come
// from (text_server_adv.cpp _shape_substr). An empty line is a blank line,
// which keeps its newline, the face's own glyph.
FontFace::LineMetric LineMetricOf(const FontFace& f, std::string_view line);

// The default theme's Label line_spacing (default_theme.cpp). A Label can
// set its own; the layout export records it.
inline constexpr int kDefaultLineSpacing = 3;

// Mirrors Godot's HorizontalAlignment / VerticalAlignment so the exported
// integers mean here what they mean there.
enum class AlignH : int { Left = 0, Centre = 1, Right = 2, Fill = 3 };
enum class AlignV : int { Top = 0, Centre = 1, Bottom = 2, Fill = 3 };

class FontAtlas {
public:
    // `pixelsPath` may be null to load metrics only, which is all the layout
    // tests need.
    FontError Load(const char* jsonPath, const char* pixelsPath);
    // How the last Load read its metrics file ("<path>: read N of M bytes").
    const std::string& LoadNote() const { return loadNote_; }

    const std::vector<FontFace>& Faces() const { return faces_; }

    // -1 when absent. The key is what export_ui.gd writes on every Label.
    int Find(const std::string& key) const;

    // Page 0: what the loading screen draws with before anything else loads.
    int AtlasWidth()  const { return pages_.empty() ? width_ : pages_[0].width; }
    int AtlasHeight() const { return pages_.empty() ? height_ : pages_[0].height; }
    const std::vector<uint8_t>& Pixels() const;

    const std::vector<FontPage>& Pages() const { return pages_; }

    // Reads the pages of a CJK group and makes its glyphs findable; unloads
    // any other group first. False (nothing loaded) when the group has no
    // pages or a page file is missing.
    bool LoadGroup(const std::string& group);
    // Releases the active group's pages; its glyphs are no longer found.
    void UnloadGroup();
    // Frees every page's CPU pixels (after they reached the GPU). Metrics stay.
    void ReleasePixels();
    const std::string& ActiveGroup() const { return activeGroup_; }

    // Where "no glyph for U+XXXX" is reported, once per code point.
    void SetLog(std::function<void(const std::string&)> log) { log_ = std::move(log); }

    // Lays `text` out inside `rect` and appends the quads. Existing contents
    // are kept, so one buffer can collect a whole screen's text.
    //
    // Newlines break lines. There is no word wrapping: every Label in this
    // game hard-wraps its own text, and inventing wrap points would disagree
    // with the authored line breaks.
    void Layout(int face, std::string_view text,
                float rx, float ry, float rw, float rh,
                AlignH ah, AlignV av, std::vector<TextQuad>& out,
                int lineSpacing = kDefaultLineSpacing) const;

    // Width of the longest line, in pixels. Exposed because a caller often
    // needs it before deciding where to put the text.
    float MeasureWidth(int face, std::string_view text) const;

    // Godot's Label TRIM_CHAR overrun: each line keeps the longest prefix whose
    // advance fits `width`. A width of 0 or less trims nothing, as in Godot.
    std::string TrimToWidth(int face, std::string_view text, float width) const;

    // A Label's minimum height (label.cpp _update_visible): every line plus
    // its spacing.
    float MeasureHeight(int face, std::string_view text, int lineSpacing = kDefaultLineSpacing) const;

    int LineCount(std::string_view text) const;

private:
    float LineWidth(const FontFace& f, std::string_view line) const;
    const Glyph* FindOrReport(const FontFace& f, int ch) const;
    bool ReadPage(FontPage& page) const;

    std::vector<FontFace> faces_;
    std::vector<FontPage> pages_;
    std::string directory_;       // the metrics file's, where page files live
    std::string activeGroup_;
    std::string loadNote_;
    int width_  = 0;
    int height_ = 0;
    std::function<void(const std::string&)> log_;
    mutable std::set<int> reported_;
};

}  // namespace assets
