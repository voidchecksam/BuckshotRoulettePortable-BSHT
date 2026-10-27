#include "platform/shader_cache_check.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// A cache file as vitaGL writes it: the matrix uniform table, the attribute
// bindings when `bindings`, then the program, padded to 4 bytes.
std::vector<uint8_t> CacheFile(uint32_t matrices, bool bindings, uint32_t programSize) {
    std::vector<uint8_t> f(4 + 4 * matrices);
    std::memcpy(f.data(), &matrices, 4);
    if (bindings) f.resize(f.size() + platform::kShaderCacheBindingBytes, 0);
    const std::size_t at = f.size();
    f.resize(at + ((programSize + 3u) & ~3u), 0);
    std::memcpy(&f[at], "GXP", 4);
    std::memcpy(&f[at + 8], &programSize, 4);
    return f;
}

}  // namespace

TEST(ShaderCacheCheck, AWholeFileWithOrWithoutBindingsPasses) {
    for (bool bindings : {true, false}) {
        for (uint32_t m : {0u, 2u}) {
            const std::vector<uint8_t> f = CacheFile(m, bindings, 607);
            EXPECT_TRUE(platform::CachedShaderIsWhole(f.data(), f.size())) << bindings << " " << m;
        }
    }
}

// An empty file is what a boot killed mid-compile, or a compile that failed
// (no libshacccg yet), leaves behind; vitaGL then loads it and crashes.
TEST(ShaderCacheCheck, EmptyCutOrForeignFilesFail) {
    EXPECT_FALSE(platform::CachedShaderIsWhole(nullptr, 0));
    const std::vector<uint8_t> whole = CacheFile(0, true, 607);
    EXPECT_FALSE(platform::CachedShaderIsWhole(whole.data(), 3));
    EXPECT_FALSE(platform::CachedShaderIsWhole(whole.data(), whole.size() - 8));   // cut short
    EXPECT_FALSE(platform::CachedShaderIsWhole(whole.data(), 4 + platform::kShaderCacheBindingBytes));

    std::vector<uint8_t> noMagic = whole;
    noMagic[4 + platform::kShaderCacheBindingBytes] = 'X';
    EXPECT_FALSE(platform::CachedShaderIsWhole(noMagic.data(), noMagic.size()));

    std::vector<uint8_t> hugeTable = whole;
    const uint32_t many = 0xFFFFFFFFu;
    std::memcpy(hugeTable.data(), &many, 4);
    EXPECT_FALSE(platform::CachedShaderIsWhole(hugeTable.data(), hugeTable.size()));

    std::vector<uint8_t> padded = CacheFile(0, true, 607);
    padded.resize(padded.size() + 8, 0);   // more bytes than the program
    EXPECT_FALSE(platform::CachedShaderIsWhole(padded.data(), padded.size()));
}
