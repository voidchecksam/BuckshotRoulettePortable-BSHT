// Debug tool: what a prop's baked surface page holds.
//
//   dump_prop_page <room file> "<node path substring>" <out prefix>
//
// For every prop whose node path contains the substring, prints its page,
// variants and UV2 bounds, and writes the texels its UV2 covers (base page and
// every variant page) as <out prefix>_<prop>_<n>.ppm, DXT decoded. A page bug
// is only seen by looking at the page.
#include "assets/room.h"
#include "assets/room_file.h"
#include "assets/texture_pool.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

void Rgb565(uint16_t c, uint8_t out[3]) {
    out[0] = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
    out[1] = static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63);
    out[2] = static_cast<uint8_t>((c & 31) * 255 / 31);
}

// One BC1 colour block into a 4x4 RGBA tile (alpha left untouched).
void DecodeColourBlock(const uint8_t* b, uint8_t tile[16][4], bool bc1) {
    const uint16_t c0 = static_cast<uint16_t>(b[0] | (b[1] << 8));
    const uint16_t c1 = static_cast<uint16_t>(b[2] | (b[3] << 8));
    uint8_t pal[4][4] = {};
    Rgb565(c0, pal[0]);
    Rgb565(c1, pal[1]);
    for (int k = 0; k < 3; ++k) {
        if (!bc1 || c0 > c1) {
            pal[2][k] = static_cast<uint8_t>((2 * pal[0][k] + pal[1][k]) / 3);
            pal[3][k] = static_cast<uint8_t>((pal[0][k] + 2 * pal[1][k]) / 3);
        } else {
            pal[2][k] = static_cast<uint8_t>((pal[0][k] + pal[1][k]) / 2);
            pal[3][k] = 0;
        }
    }
    const uint32_t bits = static_cast<uint32_t>(b[4] | (b[5] << 8) | (b[6] << 16) | (b[7] << 24));
    for (int i = 0; i < 16; ++i) {
        const int idx = (bits >> (2 * i)) & 3;
        for (int k = 0; k < 3; ++k) tile[i][k] = pal[idx][k];
    }
}

std::vector<uint8_t> DecodeToRgba(const assets::BakedTexture& t, const uint8_t* px) {
    std::vector<uint8_t> out(static_cast<std::size_t>(t.width) * t.height * 4, 255);
    if (t.format == assets::TextureFormat::Rgba8) {
        std::copy_n(px, out.size(), out.begin());
        return out;
    }
    const bool bc1 = t.format == assets::TextureFormat::Dxt1;
    const int blockBytes = bc1 ? 8 : 16;
    const int bw = (t.width + 3) / 4, bh = (t.height + 3) / 4;
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const uint8_t* blk = px + (static_cast<std::size_t>(by) * bw + bx) * blockBytes;
            uint8_t tile[16][4] = {};
            DecodeColourBlock(bc1 ? blk : blk + 8, tile, bc1);
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x >= t.width || y >= t.height) continue;
                uint8_t* o = &out[(static_cast<std::size_t>(y) * t.width + x) * 4];
                for (int k = 0; k < 3; ++k) o[k] = tile[i][k];
            }
        }
    }
    return out;
}

void WriteCrop(const std::string& path, const assets::BakedTexture& t, const std::vector<uint8_t>& rgba,
               int x0, int y0, int x1, int y1) {
    x0 = std::clamp(x0, 0, t.width - 1);
    x1 = std::clamp(x1, x0, t.width - 1);
    y0 = std::clamp(y0, 0, t.height - 1);
    y1 = std::clamp(y1, y0, t.height - 1);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", x1 - x0 + 1, y1 - y0 + 1);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) std::fwrite(&rgba[(static_cast<std::size_t>(y) * t.width + x) * 4], 1, 3, f);
    }
    std::fclose(f);
    std::printf("  wrote %s (%dx%d)\n", path.c_str(), x1 - x0 + 1, y1 - y0 + 1);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: dump_prop_page <room file> \"<node path substring>\" <out prefix>\n");
        return 2;
    }
    assets::Room room;
    if (assets::ReadRoomFile(argv[1], room) != assets::RoomFileError::None) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    const std::string want = argv[2];
    const auto& pool = room.Textures();
    int found = 0;
    for (std::size_t p = 0; p < room.Props().size(); ++p) {
        const assets::RoomProp& prop = room.Props()[p];
        if (prop.nodePath.find(want) == std::string::npos) continue;
        ++found;
        // The room file keeps one UV set: the page UV (uv2 is bake-only).
        const std::vector<float>& uv2 = prop.mesh.uv2.empty() ? prop.mesh.uvs : prop.mesh.uv2;
        float u0 = 1, v0 = 1, u1 = 0, v1 = 0;
        for (std::size_t i = 0; i + 1 < uv2.size(); i += 2) {
            u0 = std::min(u0, uv2[i]);
            u1 = std::max(u1, uv2[i]);
            v0 = std::min(v0, uv2[i + 1]);
            v1 = std::max(v1, uv2[i + 1]);
        }
        std::printf("prop %lu %s: texture %d, %lu variants, uv2 (%.4f %.4f)-(%.4f %.4f), %lu verts\n",
                    static_cast<unsigned long>(p), prop.nodePath.c_str(), prop.texture,
                    static_cast<unsigned long>(prop.variants.size()), u0, v0, u1, v1,
                    static_cast<unsigned long>(uv2.size() / 2));
        std::vector<int> textures{prop.texture};
        for (const assets::PropVariant& v : prop.variants) {
            std::printf("  variant key %08x -> texture %d\n", v.textureKey, v.texture);
            textures.push_back(v.texture);
        }
        for (std::size_t n = 0; n < textures.size(); ++n) {
            const int ti = textures[n];
            if (ti < 0 || static_cast<std::size_t>(ti) >= pool.Size() || !pool.PixelsFor(ti)) continue;
            const assets::BakedTexture& t = pool.Textures()[static_cast<std::size_t>(ti)];
            std::printf("  texture %d: %dx%d format %u\n", ti, t.width, t.height, static_cast<unsigned>(t.format));
            const std::vector<uint8_t> rgba = DecodeToRgba(t, pool.PixelsFor(ti));
            WriteCrop(std::string(argv[3]) + "_" + std::to_string(p) + "_" + std::to_string(n) + ".ppm", t, rgba,
                      static_cast<int>(u0 * t.width) - 2, static_cast<int>(v0 * t.height) - 2,
                      static_cast<int>(u1 * t.width) + 2, static_cast<int>(v1 * t.height) + 2);
        }
    }
    if (found == 0) std::printf("no prop matches \"%s\"\n", want.c_str());
    return found ? 0 : 1;
}
