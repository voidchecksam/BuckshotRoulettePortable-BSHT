// The back buffer to a binary PPM, on both platforms. "It ran without crashing"
// is not evidence anything correct appeared on screen; the pixels are.
//
// Reads GL_RGBA / GL_UNSIGNED_BYTE: vitaGL's fast path for its display surfaces
// (external/src/vitaGL/source/framebuffers.c, glReadPixels), and 4-byte rows
// need no pack-alignment call, which vitaGL's glPixelStorei rejects with a
// sticky GL_INVALID_ENUM. glFinish first: vitaGL reads the surface's memory.
#pragma once

#include <string>

namespace render {

bool SaveScreenshotPpm(const std::string& path, int width, int height);

}  // namespace render
