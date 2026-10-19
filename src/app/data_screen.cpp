#include "app/data_screen.h"

#include <algorithm>

// third_party/font8x8/font8x8_basic.c
extern "C" char font8x8_basic[128][8];

namespace app {
namespace {

constexpr int kGlyph = 8;
constexpr int kColumns = kDataAtlasWidth / kGlyph;

// Printable ASCII draws as itself, anything else as '?'.
unsigned char Printable(char c) {
    const auto u = static_cast<unsigned char>(c);
    return (u >= 32 && u < 127) ? u : static_cast<unsigned char>('?');
}

// A line wider than the panel goes on over more rows, broken after the last
// path separator or space that fits (a folder path is the usual long line),
// or at the edge when nothing does.
std::vector<std::string> Wrap(const std::string& line, std::size_t columns) {
    std::vector<std::string> rows;
    std::string rest = line;
    while (columns > 0 && rest.size() > columns) {
        std::size_t cut = rest.find_last_of("/\\ ", columns - 1);
        cut = (cut == std::string::npos || cut == 0) ? columns : cut + 1;
        rows.push_back(rest.substr(0, cut));
        rest.erase(0, cut);
    }
    rows.push_back(rest);
    return rows;
}

}  // namespace

std::vector<uint8_t> DataScreenAtlas() {
    std::vector<uint8_t> rgba(static_cast<std::size_t>(kDataAtlasWidth * kDataAtlasHeight * 4), 0);
    for (int c = 0; c < 128; ++c) {
        const int cx = (c % kColumns) * kGlyph;
        const int cy = (c / kColumns) * kGlyph;
        for (int row = 0; row < kGlyph; ++row) {
            const auto bits = static_cast<unsigned char>(font8x8_basic[c][row]);
            for (int col = 0; col < kGlyph; ++col) {
                if (!((bits >> col) & 1)) continue;   // bit 0 is the leftmost pixel
                uint8_t* px = &rgba[static_cast<std::size_t>(((cy + row) * kDataAtlasWidth + cx + col) * 4)];
                px[0] = px[1] = px[2] = px[3] = 255;
            }
        }
    }
    return rgba;
}

std::vector<GlyphQuad> LayoutDataScreen(const std::vector<std::string>& given, int width, int height, int scale) {
    const float size = static_cast<float>(kGlyph * scale);
    std::vector<std::string> lines;
    for (const std::string& line : given) {
        for (std::string& row : Wrap(line, static_cast<std::size_t>(static_cast<float>(width) / size))) {
            lines.push_back(std::move(row));
        }
    }
    const float lineStep = size * 1.5f;
    const float blockHeight = size + lineStep * static_cast<float>(lines.empty() ? 0 : lines.size() - 1);
    float y = static_cast<float>(height) * 0.5f - blockHeight * 0.5f;
    const float u = static_cast<float>(kGlyph) / kDataAtlasWidth;
    const float v = static_cast<float>(kGlyph) / kDataAtlasHeight;

    std::vector<GlyphQuad> out;
    for (const std::string& line : lines) {
        const float lineWidth = size * static_cast<float>(line.size());
        float x = std::max(0.0f, static_cast<float>(width) * 0.5f - lineWidth * 0.5f);
        for (const char ch : line) {
            if (x + size > static_cast<float>(width)) break;   // a line too long is cut at the edge
            const unsigned char c = Printable(ch);
            if (c != ' ') {
                const float u0 = static_cast<float>(c % kColumns) * u;
                const float v0 = static_cast<float>(c / kColumns) * v;
                out.push_back({x, y, size, size, u0, v0, u0 + u, v0 + v});
            }
            x += size;
        }
        y += lineStep;
    }
    return out;
}

std::vector<std::string> DataScreenLines(const GameDataCheck& check, const std::string& exitHint) {
    std::vector<std::string> lines = check.lines;
    lines.push_back("");
    lines.push_back(exitHint);
    return lines;
}

}  // namespace app
