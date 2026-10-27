// Whether a file in vitaGL's shader cache (ux0:data/shader_cache/<title>/v1)
// is whole. vitaGL loads a cached shader without checking it, so a file a
// killed or failed first compile left behind crashes every later boot.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace platform {

// vitaGL's binds_map (source/shared.h), written after the matrix table when a
// shader is compiled at link time, as every GLSL shader here is.
inline constexpr std::size_t kShaderCacheBindingBytes = 780;

// The file is: u32 matrix count, that many u32, the bindings (or none), then
// a SceGxmProgram ("GXP\0", its byte size at offset 8) padded to 4 bytes.
inline bool CachedShaderIsWhole(const uint8_t* data, std::size_t size) {
    if (!data || size < 4) return false;
    uint32_t matrices = 0;
    std::memcpy(&matrices, data, 4);
    if (matrices > 256) return false;
    const std::size_t table = 4 + 4 * static_cast<std::size_t>(matrices);
    for (const std::size_t at : {table + kShaderCacheBindingBytes, table}) {
        if (size < at + 12 || std::memcmp(data + at, "GXP", 4) != 0) continue;
        uint32_t program = 0;
        std::memcpy(&program, data + at + 8, 4);
        const std::size_t rest = size - at;
        return program >= 12 && program <= rest && rest - program < 4;
    }
    return false;
}

}  // namespace platform
