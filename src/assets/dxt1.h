// DXT1 (BC1) blocks: the baked surface pages' on-device format.
//
// The Vita samples DXT1 natively (it is among its 48 extensions) and desktop GL
// 2.1 exposes GL_EXT_texture_compression_s3tc, so a surface page costs 4 bits a
// texel in VRAM and in the fetch: 1/8 of RGBA8, which keeps the fill-bound
// fragment stage cheap.
//
// This file decodes blocks (the bake's round-trip tests, a debug PNG of each
// page) and makes trivial ones. The real encoder is stb_dxt, in bake_room
// only: nothing here compresses an image.
//
// Block layout: two little-endian RGB565 endpoints c0, c1, then 16 2-bit indices,
// row by row, low bits first. c0 > c1 selects four colours (c0, c1, 2/3, 1/3);
// otherwise three plus transparent black.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace assets {

using Dxt1Block = std::array<uint8_t, 8>;

// Every texel the RGB565 colour nearest (r, g, b).
Dxt1Block EncodeDxt1Solid(uint8_t r, uint8_t g, uint8_t b);

// 16 texels of RGBA8, row-major within the 4x4 block.
void DecodeDxt1Block(const uint8_t block[8], uint8_t outRgba[16 * 4]);


}  // namespace assets
