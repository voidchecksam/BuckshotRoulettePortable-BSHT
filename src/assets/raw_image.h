// The device's raw image file: u32 width, u32 height (little endian), then
// width * height RGBA8, top row first. Baked on the PC so the device does no
// decoding: textures/smoke.png (tools/godot/export_particle_texture.gd) and the port's
// menu logo (tools/bake_menu_logo.py). Headless.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

struct RawImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

// The largest side a raw image may claim: a corrupt header is refused before
// anything is allocated.
inline constexpr int kRawImageMaxSide = 2048;

// False (and `out` left empty) when the file is missing, truncated or claims
// a size outside 1..kRawImageMaxSide.
bool ReadRawRgba(const std::string& path, RawImage& out);

}  // namespace assets
