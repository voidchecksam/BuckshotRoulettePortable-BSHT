#include "assets/raw_image.h"

#include <cstdio>
#include <utility>

namespace assets {

bool ReadRawRgba(const std::string& path, RawImage& out) {
    out = RawImage{};
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint32_t wh[2] = {0, 0};
    const bool sized = std::fread(wh, sizeof(uint32_t), 2, f) == 2 && wh[0] > 0 && wh[1] > 0 &&
                       wh[0] <= static_cast<uint32_t>(kRawImageMaxSide) &&
                       wh[1] <= static_cast<uint32_t>(kRawImageMaxSide);
    RawImage img;
    bool ok = sized;
    if (ok) {
        img.width = static_cast<int>(wh[0]);
        img.height = static_cast<int>(wh[1]);
        img.rgba.resize(static_cast<std::size_t>(wh[0]) * wh[1] * 4);
        ok = std::fread(img.rgba.data(), 1, img.rgba.size(), f) == img.rgba.size();
    }
    std::fclose(f);
    if (ok) out = std::move(img);
    return ok;
}

}  // namespace assets
