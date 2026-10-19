// The room's textures: in a shipped room, the baked surface pages (DXT1, or
// DXT5 where a page has alpha) and their variants; during a build, the decoded
// albedo the pages are made from.
//
// One texture per (source .glb, material): batches merge per material, so a
// texture is one bind per draw that was already going to happen. No atlas.
//
// No codec lives here; this is a container. PNG is decoded once, offline, by
// the baker (a host tool that may link stb_image). The device freads bytes
// already in their GPU format.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

// How an entry's bytes are laid out. RGBA8: 4 bytes a texel. DXT1 (BC1): 8
// bytes a 4x4 block, the opaque surface pages. DXT5 (BC3): 16 bytes a block,
// smooth alpha, the blended ones. The Vita samples both natively (S3TC is
// among its 48 extensions).
enum class TextureFormat : uint32_t { Rgba8 = 0, Dxt1 = 1, Dxt5 = 2 };

struct BakedTexture {
    // The (source .glb, material index) this was decoded for. Kept so a bake
    // can be audited and so two materials that resolve to the same image can be
    // seen to share one entry.
    std::string sourceGlb;
    int         materialIndex = -1;

    int      width  = 0;
    int      height = 0;
    uint32_t offset = 0;      // byte index into the shared pool
    uint32_t bytes  = 0;
    TextureFormat format = TextureFormat::Rgba8;

    bool Valid() const {
        if (width <= 0 || height <= 0) return false;
        const uint32_t texels = static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
        if (format == TextureFormat::Dxt1) {
            return width % 4 == 0 && height % 4 == 0 && bytes == texels / 2u;
        }
        if (format == TextureFormat::Dxt5) {
            return width % 4 == 0 && height % 4 == 0 && bytes == texels;
        }
        return bytes == texels * 4u;
    }
};

class TexturePool {
public:
    // Adds one RGBA8 image. Returns its index, or -1 if the inputs disagree
    // with each other: a mis-sized texture stored anyway would show up as
    // garbage pixels much later.
    int Add(const std::string& sourceGlb, int materialIndex,
            const uint8_t* rgba, int width, int height);

    // Adds one image already in `format` (`bytes` long). -1 when the size does
    // not match the format, exactly as Add refuses a mis-sized RGBA8 image.
    int AddEncoded(const std::string& sourceGlb, int materialIndex, const uint8_t* data,
                   std::size_t bytes, int width, int height, TextureFormat format);

    // Index for a (glb, material) pair, or -1. This is the join the renderer
    // uses to find a batch's texture.
    int Find(const std::string& sourceGlb, int materialIndex) const;

    // Records that (glb, material) uses an existing entry. Two materials in
    // different files often reference identical image bytes; sharing one entry
    // is most of why this pool is small.
    void Alias(const std::string& sourceGlb, int materialIndex, int texture);

    const std::vector<BakedTexture>& Textures() const { return textures_; }
    // The shared pool a bake builds (Add / AddEncoded) and WriteRoomFile writes.
    // Empty for a pool read from a room file: see Chunked().
    const std::vector<uint8_t>&      Pixels()   const { return pixels_; }

    std::size_t Size() const { return textures_.size(); }
    std::size_t ByteSize() const;

    // True for a pool read from a room file: each texture's bytes in a buffer of
    // its own (ResetChunked). One 25-32 MB pool buffer cannot find a hole in a
    // fragmented heap on a second trip into the table, and the Vita's heap is a
    // fixed 128 MB.
    bool Chunked() const { return !chunks_.empty(); }

    // First pixel of `index`, or null when the index or the pool is bad.
    const uint8_t* PixelsFor(int index) const;

    // Restores a pool wholesale from one buffer (entries' offsets index it).
    void Reset(std::vector<BakedTexture> textures, std::vector<uint8_t> pixels);
    // The file reader's form: chunks[i] holds texture i's bytes; offsets unused.
    void ResetChunked(std::vector<BakedTexture> textures, std::vector<std::vector<uint8_t>> chunks);

    // Every entry's byte range must sit inside the pool and match its size.
    bool Consistent() const;

    // Frees the pixel bytes and keeps the entries, once they are on the GPU.
    // PixelsFor returns null afterwards and ByteSize is 0.
    void ReleasePixels() {
        std::vector<uint8_t>().swap(pixels_);
        std::vector<std::vector<uint8_t>>().swap(chunks_);
    }

private:
    struct Key {
        std::string glb;
        int material = -1;
        int texture  = -1;
    };

    std::vector<BakedTexture> textures_;
    std::vector<uint8_t>      pixels_;
    std::vector<std::vector<uint8_t>> chunks_;
    std::vector<Key>          keys_;

    void RebuildKeys();
};

}  // namespace assets
