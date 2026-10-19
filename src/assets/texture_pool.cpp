#include "assets/texture_pool.h"

namespace assets {

int TexturePool::Add(const std::string& sourceGlb, int materialIndex,
                     const uint8_t* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return -1;

    const std::size_t need =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;

    BakedTexture t;
    t.sourceGlb     = sourceGlb;
    t.materialIndex = materialIndex;
    t.width         = width;
    t.height        = height;
    t.offset        = static_cast<uint32_t>(pixels_.size());
    t.bytes         = static_cast<uint32_t>(need);
    if (!t.Valid()) return -1;

    pixels_.insert(pixels_.end(), rgba, rgba + need);
    textures_.push_back(std::move(t));

    const int index = static_cast<int>(textures_.size()) - 1;
    keys_.push_back(Key{sourceGlb, materialIndex, index});
    return index;
}

int TexturePool::AddEncoded(const std::string& sourceGlb, int materialIndex, const uint8_t* data,
                            std::size_t bytes, int width, int height, TextureFormat format) {
    if (!data) return -1;
    BakedTexture t;
    t.sourceGlb     = sourceGlb;
    t.materialIndex = materialIndex;
    t.width         = width;
    t.height        = height;
    t.offset        = static_cast<uint32_t>(pixels_.size());
    t.bytes         = static_cast<uint32_t>(bytes);
    t.format        = format;
    if (!t.Valid()) return -1;
    pixels_.insert(pixels_.end(), data, data + bytes);
    textures_.push_back(std::move(t));
    const int index = static_cast<int>(textures_.size()) - 1;
    keys_.push_back(Key{sourceGlb, materialIndex, index});
    return index;
}

void TexturePool::Alias(const std::string& sourceGlb, int materialIndex,
                        int texture) {
    if (texture < 0 || texture >= static_cast<int>(textures_.size())) return;
    if (Find(sourceGlb, materialIndex) >= 0) return;
    keys_.push_back(Key{sourceGlb, materialIndex, texture});
}

int TexturePool::Find(const std::string& sourceGlb, int materialIndex) const {
    for (const Key& k : keys_) {
        if (k.material == materialIndex && k.glb == sourceGlb) return k.texture;
    }
    return -1;
}

std::size_t TexturePool::ByteSize() const {
    std::size_t bytes = pixels_.size();
    for (const std::vector<uint8_t>& c : chunks_) bytes += c.size();
    return bytes;
}

const uint8_t* TexturePool::PixelsFor(int index) const {
    if (index < 0 || index >= static_cast<int>(textures_.size())) return nullptr;
    const BakedTexture& t = textures_[static_cast<std::size_t>(index)];
    if (Chunked()) {
        const std::vector<uint8_t>& c = chunks_[static_cast<std::size_t>(index)];
        return c.size() == t.bytes && !c.empty() ? c.data() : nullptr;
    }
    const std::size_t end =
        static_cast<std::size_t>(t.offset) + static_cast<std::size_t>(t.bytes);
    if (end > pixels_.size()) return nullptr;
    return pixels_.data() + t.offset;
}

void TexturePool::Reset(std::vector<BakedTexture> textures,
                        std::vector<uint8_t> pixels) {
    textures_ = std::move(textures);
    pixels_ = std::move(pixels);
    chunks_.clear();
    RebuildKeys();
}

void TexturePool::ResetChunked(std::vector<BakedTexture> textures, std::vector<std::vector<uint8_t>> chunks) {
    textures_ = std::move(textures);
    pixels_.clear();
    chunks_ = std::move(chunks);
    RebuildKeys();
}

// Rebuild the lookup from the entries themselves, so a pool that came off
// disk answers Find() exactly as the one that was baked.
void TexturePool::RebuildKeys() {
    keys_.clear();
    for (std::size_t i = 0; i < textures_.size(); ++i) {
        keys_.push_back(Key{textures_[i].sourceGlb, textures_[i].materialIndex,
                            static_cast<int>(i)});
    }
}

bool TexturePool::Consistent() const {
    if (Chunked() && chunks_.size() != textures_.size()) return false;
    for (std::size_t i = 0; i < textures_.size(); ++i) {
        const BakedTexture& t = textures_[i];
        if (!t.Valid()) return false;
        if (Chunked()) {
            if (chunks_[i].size() != t.bytes) return false;
            continue;
        }
        const std::size_t end =
            static_cast<std::size_t>(t.offset) + static_cast<std::size_t>(t.bytes);
        if (end > pixels_.size()) return false;
    }
    return true;
}

}  // namespace assets
