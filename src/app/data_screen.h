// The screen shown instead of the game when its data is missing or wrong
// (app/data_check.h). Drawn with font8x8 (public domain) compiled in: the
// game's own fonts are game data. data_screen.cpp is the headless layout,
// data_screen_draw.cpp the GL half.
#pragma once

#include "app/data_check.h"
#include "app/data_layout.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace app {

inline constexpr int kDataAtlasWidth = 128;   // 16 x 8 cells of 8x8, ASCII 0-127
inline constexpr int kDataAtlasHeight = 64;

struct GlyphQuad {
    float x, y, w, h;
    float u0, v0, u1, v1;
};

// White glyphs on clear, RGBA.
std::vector<uint8_t> DataScreenAtlas();

// `lines` centred on a `width` x `height` design screen, 8x8 glyphs at `scale`.
std::vector<GlyphQuad> LayoutDataScreen(const std::vector<std::string>& lines, int width, int height,
                                        int scale);

// The check's message, a blank line, and how to leave (the platform's own
// words: the Vita has START, the desktop a window to close).
std::vector<std::string> DataScreenLines(const GameDataCheck& check, const std::string& exitHint);

// Draws the screen until exitPressed() is true. `capture`, when set, runs once
// after the first frame (the desktop's --loading-shot).
void ShowDataScreen(const GameDataCheck& check, const std::string& exitHint, const std::function<void()>& swap,
                    const std::function<bool()>& exitPressed, const std::function<void()>& capture = {});

// Both entry points, before anything is read: logs the layout and, for a game
// folder, the check; on a failed check shows the screen until exitPressed()
// and returns false.
bool PassDataGate(const DataLayout& layout, const std::string& exitHint,
                  const std::function<void(const std::string&)>& log,
                  const std::function<void()>& swap, const std::function<bool()>& exitPressed,
                  const std::function<void()>& capture = {});

}  // namespace app
