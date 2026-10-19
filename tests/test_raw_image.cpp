// assets::ReadRawRgba: the device's raw image file, u32 width, u32 height
// (little endian), then width * height RGBA8. The smoke texture and the menu
// logo are both written in it (tools/godot/export_particle_texture.gd,
// tools/bake_menu_logo.py), so one reader serves both.
#include "assets/raw_image.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string TempPath(const char* name) { return testing::TempDir() + name; }

void WriteFile(const std::string& path, uint32_t w, uint32_t h, std::size_t pixelBytes) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    const uint32_t wh[2] = {w, h};
    std::fwrite(wh, sizeof(uint32_t), 2, f);
    std::vector<uint8_t> px(pixelBytes);
    for (std::size_t i = 0; i < px.size(); ++i) px[i] = static_cast<uint8_t>(i * 7);
    std::fwrite(px.data(), 1, px.size(), f);
    std::fclose(f);
}

}  // namespace

TEST(RawImage, ReadsBackWhatTheBakeWrote) {
    const std::string path = TempPath("raw_ok.rgba");
    WriteFile(path, 3, 2, 3 * 2 * 4);
    assets::RawImage img;
    ASSERT_TRUE(assets::ReadRawRgba(path, img));
    EXPECT_EQ(img.width, 3);
    EXPECT_EQ(img.height, 2);
    ASSERT_EQ(img.rgba.size(), 24u);
    EXPECT_EQ(img.rgba[5], 35);
}

TEST(RawImage, ATruncatedFileIsRefused) {
    const std::string path = TempPath("raw_short.rgba");
    WriteFile(path, 3, 2, 10);
    assets::RawImage img;
    EXPECT_FALSE(assets::ReadRawRgba(path, img));
    EXPECT_TRUE(img.rgba.empty());
}

TEST(RawImage, AnImplausibleSizeIsRefusedBeforeAllocating) {
    const std::string path = TempPath("raw_huge.rgba");
    WriteFile(path, 1u << 20, 1u << 20, 16);
    assets::RawImage img;
    EXPECT_FALSE(assets::ReadRawRgba(path, img));
}

TEST(RawImage, AMissingFileIsRefused) {
    assets::RawImage img;
    EXPECT_FALSE(assets::ReadRawRgba(TempPath("raw_absent.rgba"), img));
}
