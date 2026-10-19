#include "render/screenshot.h"

#include "platform/gl_compat.h"

#include <cstdio>
#include <vector>

namespace render {

bool SaveScreenshotPpm(const std::string& path, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    const std::size_t w = static_cast<std::size_t>(width);
    std::vector<unsigned char> rgba(w * static_cast<std::size_t>(height) * 4);
    glFinish();
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", width, height);
    std::vector<unsigned char> row(w * 3);
    // GL's origin is bottom-left; PPM is top-down.
    for (int y = height - 1; y >= 0; --y) {
        const unsigned char* src = rgba.data() + static_cast<std::size_t>(y) * w * 4;
        for (std::size_t x = 0; x < w; ++x) {
            row[x * 3 + 0] = src[x * 4 + 0];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 2];
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    return std::fclose(f) == 0;
}

}  // namespace render
