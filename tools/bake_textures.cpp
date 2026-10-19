#include "bake_textures.h"

#include "assets/glb.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <vector>

// The ONLY place an image decoder is linked. Host tool; never reaches the Vita.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace tools {

namespace {

bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); return false; }
    out.resize(static_cast<std::size_t>(n));
    const std::size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

// Box-filter downscale. A point sample would alias badly on the brick and tile
// work that most of this room is made of, and a box filter is a handful of adds
// per output pixel in a tool that runs once.
void Downscale(const uint8_t* src, int sw, int sh,
               std::vector<uint8_t>& dst, int dw, int dh) {
    dst.assign(static_cast<std::size_t>(dw) * static_cast<std::size_t>(dh) * 4u, 0);
    for (int y = 0; y < dh; ++y) {
        const int y0 = y * sh / dh;
        int y1 = (y + 1) * sh / dh;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dw; ++x) {
            const int x0 = x * sw / dw;
            int x1 = (x + 1) * sw / dw;
            if (x1 <= x0) x1 = x0 + 1;

            unsigned acc[4] = {0, 0, 0, 0};
            unsigned n = 0;
            for (int sy = y0; sy < y1 && sy < sh; ++sy) {
                for (int sx = x0; sx < x1 && sx < sw; ++sx) {
                    const std::size_t si =
                        (static_cast<std::size_t>(sy) * static_cast<std::size_t>(sw) +
                         static_cast<std::size_t>(sx)) * 4u;
                    for (int c = 0; c < 4; ++c) acc[c] += src[si + static_cast<std::size_t>(c)];
                    ++n;
                }
            }
            if (n == 0) n = 1;
            const std::size_t di =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(dw) +
                 static_cast<std::size_t>(x)) * 4u;
            for (int c = 0; c < 4; ++c) {
                dst[di + static_cast<std::size_t>(c)] =
                    static_cast<uint8_t>(acc[c] / n);
            }
        }
    }
}

// A flat texture for a material that has a baseColorFactor and no image. 11 of
// the 123 materials are like this; without it they would render white while
// everything around them was correct, which reads as a broken texture rather
// than an absent one.
void SolidColour(const float rgba[4], std::vector<uint8_t>& out) {
    out.assign(4, 0);
    for (int c = 0; c < 4; ++c) {
        float v = rgba[c];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        out[static_cast<std::size_t>(c)] = static_cast<uint8_t>(v * 255.0f + 0.5f);
    }
}

// Decodes an encoded PNG/JPEG, fits it to `edge` and adds it to the pool under
// (key, material). The pool index, or -1.
int AddDecoded(assets::TexturePool& pool, const std::string& key, int material,
               const uint8_t* data, std::size_t size, int edge) {
    int w = 0;
    int h = 0;
    int comp = 0;
    stbi_uc* px = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        return -1;
    }

    int dw = w;
    int dh = h;
    if (w > edge || h > edge) {
        // Preserve aspect: a stretched texture is worse than a small one.
        const float f = static_cast<float>(edge) / static_cast<float>(w > h ? w : h);
        dw = static_cast<int>(static_cast<float>(w) * f);
        dh = static_cast<int>(static_cast<float>(h) * f);
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
    }

    int index = -1;
    if (dw == w && dh == h) {
        index = pool.Add(key, material, px, w, h);
    } else {
        std::vector<uint8_t> small;
        Downscale(px, w, h, small, dw, dh);
        index = pool.Add(key, material, small.data(), dw, dh);
    }
    stbi_image_free(px);
    return index;
}

// An inline mesh's texture key: a PNG under the exports folder, not a .glb.
bool IsInlineTextureKey(const std::string& key) {
    return key.rfind("inline_textures/", 0) == 0;
}

}  // namespace

TextureBakeStats BakeRoomTextures(const std::string& modelDir,
                                  const std::string& extractedDir, assets::Room& room,
                                  assets::TexturePool& pool, int edge) {
    TextureBakeStats stats;
    if (edge < 1) edge = 1;

    // Only the (glb, material) pairs the room actually draws. Decoding the
    // whole library would spend time and bytes on geometry outside the play
    // area that will never be uploaded.
    std::map<std::string, std::set<int>> wanted;
    std::set<std::string> inlineWanted;
    const auto want = [&](const std::string& key, int material) {
        if (key.empty() || material < 0) return;
        if (IsInlineTextureKey(key)) {
            inlineWanted.insert(key);
        } else {
            wanted[key].insert(material);
        }
    };
    for (const assets::RoomBatch& b : room.Batches()) want(b.sourceGlb, b.materialIndex);
    for (const assets::RoomProp& p : room.Props()) want(p.sourceGlb, p.materialIndex);

    // Identical image bytes appear in many files. Keyed by (size, first and
    // last bytes) rather than a hash: cheap, and a false match would have to
    // agree on length and both ends, which is checked with a full compare.
    struct Cached {
        std::vector<uint8_t> encoded;
        int texture = -1;
    };
    std::vector<Cached> cache;

    for (const auto& entry : wanted) {
        const std::string& glbName = entry.first;
        const std::string path = modelDir + "/" + glbName;

        // The buffer must outlive the Model: MaterialImage points INTO it. This
        // is why LoadGlbFromMemory is used and not LoadGlb, which reads the
        // file into a buffer of its own that dies on return.
        std::vector<uint8_t> bytes;
        if (!ReadFile(path, bytes)) {
            std::printf("  texture: cannot read %s\n", path.c_str());
            stats.failed += static_cast<int>(entry.second.size());
            continue;
        }

        assets::Model model;
        const assets::GlbError e =
            assets::LoadGlbFromMemory(bytes.data(), bytes.size(), model);
        if (e != assets::GlbError::None) {
            std::printf("  texture: %s: %s\n", glbName.c_str(),
                        assets::GlbErrorText(e));
            stats.failed += static_cast<int>(entry.second.size());
            continue;
        }

        for (int mat : entry.second) {
            ++stats.materialsSeen;
            if (mat >= static_cast<int>(model.materials.size())) {
                ++stats.failed;
                continue;
            }
            const assets::MaterialImage& mi =
                model.materials[static_cast<std::size_t>(mat)];

            if (!mi.HasImage()) {
                std::vector<uint8_t> solid;
                SolidColour(mi.baseColour, solid);
                if (pool.Add(glbName, mat, solid.data(), 1, 1) >= 0) {
                    ++stats.colourOnly;
                } else {
                    ++stats.failed;
                }
                continue;
            }

            // Already decoded under another material?
            int shared = -1;
            for (const Cached& c : cache) {
                if (c.encoded.size() != mi.size) continue;
                if (std::memcmp(c.encoded.data(), mi.data, mi.size) != 0) continue;
                shared = c.texture;
                break;
            }
            if (shared >= 0) {
                pool.Alias(glbName, mat, shared);
                ++stats.shared;
                continue;
            }

            const int index = AddDecoded(pool, glbName, mat, mi.data, mi.size, edge);
            if (index < 0) { ++stats.failed; continue; }
            ++stats.decoded;

            Cached c;
            c.encoded.assign(mi.data, mi.data + mi.size);
            c.texture = index;
            cache.push_back(std::move(c));
        }
    }

    // Inline meshes' textures: the PNGs export_inline_meshes.gd saved, 14 of
    // them from textures embedded in main.tscn (the lightning icon is one).
    for (const std::string& key : inlineWanted) {
        ++stats.materialsSeen;
        std::vector<uint8_t> bytes;
        const std::string path = extractedDir + "/" + key;
        if (!ReadFile(path, bytes)) {
            std::printf("  texture: cannot read %s\n", path.c_str());
            ++stats.failed;
            continue;
        }
        if (AddDecoded(pool, key, 0, bytes.data(), bytes.size(), edge) < 0) {
            std::printf("  texture: cannot decode %s\n", path.c_str());
            ++stats.failed;
            continue;
        }
        ++stats.decoded;
    }

    for (assets::RoomBatch& b : room.Batches()) {
        b.texture = pool.Find(b.sourceGlb, b.materialIndex);
        if (b.texture >= 0) ++stats.batchesTextured;
    }
    for (assets::RoomProp& p : room.Props()) {
        p.texture = pool.Find(p.sourceGlb, p.materialIndex);
        if (p.texture >= 0) ++stats.propsTextured;
    }
    return stats;
}

int AddInlineTexture(const std::string& extractedDir, const std::string& key,
                     assets::TexturePool& pool, int edge) {
    if (!IsInlineTextureKey(key)) return -1;
    const int have = pool.Find(key, 0);
    if (have >= 0) return have;
    std::vector<uint8_t> bytes;
    if (!ReadFile(extractedDir + "/" + key, bytes)) return -1;
    return AddDecoded(pool, key, 0, bytes.data(), bytes.size(), edge);
}

}  // namespace tools
