// The baked .room format.
//
// The round trip must be exact: the Vita hands these bytes straight to
// glBufferData, so anything lost here is geometry that is wrong on a device
// with no debugger attached.
#include <gtest/gtest.h>

#include "assets/room_file.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>

using namespace assets;

namespace {

std::string TempPath(const char* name) {
    return std::string(BSHT_BINARY_DIR) + "/" + name;
}

// A room with two batches of distinguishable content.
Room MakeRoom() {
    std::vector<RoomBatch> batches;

    RoomBatch a;
    a.materialIndex = 3;
    a.mesh.positions = {0,0,0,  1,2,3,  4,5,6};
    a.mesh.normals   = {0,1,0,  0,0,1,  1,0,0};
    a.mesh.uvs       = {0.0f,0.0f, 0.5f,0.25f, 1.0f,1.0f};
    a.mesh.colours   = {10,20,30,255,  40,50,60,255,  70,80,90,255};
    a.mesh.indices   = {0,1,2};
    a.boundsMin[0] = 0.0f; a.boundsMin[1] = 0.0f; a.boundsMin[2] = 0.0f;
    a.boundsMax[0] = 4.0f; a.boundsMax[1] = 5.0f; a.boundsMax[2] = 6.0f;
    batches.push_back(std::move(a));

    RoomBatch b;
    b.materialIndex = -1;
    b.mesh.positions = {9,9,9,  8,8,8,  7,7,7,  6,6,6};
    b.mesh.normals   = {0,0,1,  0,0,1,  0,0,1,  0,0,1};
    b.mesh.uvs       = {0,0, 1,0, 1,1, 0,1};
    b.mesh.colours   = {1,2,3,255, 4,5,6,255, 7,8,9,255, 11,12,13,255};
    b.mesh.indices   = {0,1,2, 0,2,3};
    b.boundsMin[0] = 6.0f; b.boundsMin[1] = 6.0f; b.boundsMin[2] = 6.0f;
    b.boundsMax[0] = 9.0f; b.boundsMax[1] = 9.0f; b.boundsMax[2] = 9.0f;
    batches.push_back(std::move(b));

    Room r;
    r.AdoptBatches(std::move(batches));
    return r;
}

}  // namespace

TEST(RoomFile, RoundTripsEveryFieldExactly) {
    const std::string path = TempPath("roundtrip.room");
    const Room src = MakeRoom();
    ASSERT_TRUE(WriteRoomFile(path.c_str(), src));

    Room dst;
    ASSERT_EQ(ReadRoomFile(path.c_str(), dst), RoomFileError::None);
    ASSERT_EQ(dst.Batches().size(), src.Batches().size());

    for (std::size_t i = 0; i < src.Batches().size(); ++i) {
        const RoomBatch& a = src.Batches()[i];
        const RoomBatch& b = dst.Batches()[i];
        EXPECT_EQ(b.materialIndex, a.materialIndex) << "batch " << i;
        EXPECT_EQ(b.mesh.positions, a.mesh.positions) << "batch " << i;
        EXPECT_EQ(b.mesh.normals, a.mesh.normals) << "batch " << i;
        EXPECT_EQ(b.mesh.uvs, a.mesh.uvs) << "batch " << i;
        EXPECT_EQ(b.mesh.colours, a.mesh.colours) << "batch " << i;
        EXPECT_EQ(b.mesh.indices, a.mesh.indices) << "batch " << i;
        for (int k = 0; k < 3; ++k) {
            EXPECT_FLOAT_EQ(b.boundsMin[k], a.boundsMin[k]) << "batch " << i;
            EXPECT_FLOAT_EQ(b.boundsMax[k], a.boundsMax[k]) << "batch " << i;
        }
    }
}

TEST(RoomFile, ReportsAMissingFile) {
    Room r;
    EXPECT_EQ(ReadRoomFile(TempPath("does_not_exist.room").c_str(), r),
              RoomFileError::FileNotFound);
    EXPECT_EQ(ReadRoomFile(nullptr, r), RoomFileError::FileNotFound);
}

TEST(RoomFile, RejectsBadMagic) {
    const std::string path = TempPath("badmagic.room");
    ASSERT_TRUE(WriteRoomFile(path.c_str(), MakeRoom()));
    // Corrupt the magic in place.
    std::FILE* f = std::fopen(path.c_str(), "r+b");
    ASSERT_NE(f, nullptr);
    std::fwrite("XXXX", 1, 4, f);
    std::fclose(f);

    Room r;
    EXPECT_EQ(ReadRoomFile(path.c_str(), r), RoomFileError::BadMagic);
    EXPECT_TRUE(r.Batches().empty());
}

TEST(RoomFile, RejectsAStaleVersionRatherThanReinterpretingIt) {
    // A v1 file read as v2 produces geometry that is subtly wrong, which is
    // near-impossible to diagnose on a device with no debugger.
    const std::string path = TempPath("badversion.room");
    ASSERT_TRUE(WriteRoomFile(path.c_str(), MakeRoom()));
    std::FILE* f = std::fopen(path.c_str(), "r+b");
    ASSERT_NE(f, nullptr);
    std::fseek(f, static_cast<long>(RoomFileOffsets::kVersion), SEEK_SET);
    const uint32_t bogus = kRoomFileVersion + 99;
    std::fwrite(&bogus, sizeof(bogus), 1, f);
    std::fclose(f);

    Room r;
    EXPECT_EQ(ReadRoomFile(path.c_str(), r), RoomFileError::BadVersion);
}

TEST(RoomFile, RejectsATruncatedFile) {
    const std::string path = TempPath("truncated.room");
    ASSERT_TRUE(WriteRoomFile(path.c_str(), MakeRoom()));

    // Re-write it shorter, keeping the header intact.
    std::FILE* in = std::fopen(path.c_str(), "rb");
    ASSERT_NE(in, nullptr);
    std::fseek(in, 0, SEEK_END);
    const long full = std::ftell(in);
    std::fseek(in, 0, SEEK_SET);
    std::vector<char> buf(static_cast<std::size_t>(full));
    ASSERT_EQ(std::fread(buf.data(), 1, buf.size(), in), buf.size());
    std::fclose(in);

    std::FILE* out = std::fopen(path.c_str(), "wb");
    ASSERT_NE(out, nullptr);
    std::fwrite(buf.data(), 1, buf.size() / 2, out);
    std::fclose(out);

    Room r;
    EXPECT_EQ(ReadRoomFile(path.c_str(), r), RoomFileError::Truncated);
}

TEST(RoomFile, RejectsHeaderTotalsThatDisagreeWithTheBatches) {
    // The totals are redundant on purpose: a disagreement means the writer did
    // not understand the format and the file must not be trusted.
    const std::string path = TempPath("inconsistent.room");
    ASSERT_TRUE(WriteRoomFile(path.c_str(), MakeRoom()));
    std::FILE* f = std::fopen(path.c_str(), "r+b");
    ASSERT_NE(f, nullptr);
    std::fseek(f, static_cast<long>(RoomFileOffsets::kTotalVertices), SEEK_SET);
    const uint32_t lie = 9999;
    std::fwrite(&lie, sizeof(lie), 1, f);
    std::fclose(f);

    Room r;
    EXPECT_EQ(ReadRoomFile(path.c_str(), r), RoomFileError::Inconsistent);
}

TEST(RoomFile, RefusesToWriteAnEmptyRoom) {
    Room empty;
    EXPECT_FALSE(WriteRoomFile(TempPath("empty.room").c_str(), empty));
    EXPECT_FALSE(WriteRoomFile(nullptr, MakeRoom()));
}

TEST(RoomFile, MeasureMatchesTheBytesActuallyWritten) {
    const std::string path = TempPath("measured.room");
    const Room r = MakeRoom();
    const RoomFileStats s = MeasureRoom(r);
    ASSERT_TRUE(WriteRoomFile(path.c_str(), r));

    std::FILE* f = std::fopen(path.c_str(), "rb");
    ASSERT_NE(f, nullptr);
    std::fseek(f, 0, SEEK_END);
    const long actual = std::ftell(f);
    std::fclose(f);

    EXPECT_EQ(static_cast<uint64_t>(actual), s.bytes)
        << "MeasureRoom disagrees with the writer; the bake report would lie";
    EXPECT_EQ(s.batches, 2u);
    EXPECT_EQ(s.vertices, 7u);
    EXPECT_EQ(s.indices, 9u);
}

// The loading bar follows the room read, which is the one long blocking call
// of the boot (43 MB on the device). Progress is the file position, so it must
// rise, never fall, and finish at exactly 1.
TEST(RoomFile, ReadReportsMonotonicProgressEndingAtOne) {
    const std::string path = TempPath("progress.room");
    ASSERT_TRUE(WriteRoomFile(path.c_str(), MakeRoom()));

    std::vector<float> seen;
    Room dst;
    ASSERT_EQ(ReadRoomFile(path.c_str(), dst, [&seen](float p) { seen.push_back(p); }),
              RoomFileError::None);
    ASSERT_GE(seen.size(), 2u);
    for (std::size_t i = 1; i < seen.size(); ++i) EXPECT_GE(seen[i], seen[i - 1]) << i;
    EXPECT_GE(seen.front(), 0.0f);
    EXPECT_EQ(seen.back(), 1.0f);
    std::remove(path.c_str());
}

// Surface pages are DXT1, DXT5 or RGBA8, and the format travels with each
// entry so the renderer uploads it the right way.
TEST(RoomFile, DxtAndRgbaPagesRoundTripWithTheirFormat) {
    const std::string path = TempPath("pages.room");
    Room src = MakeRoom();
    std::vector<uint8_t> dxt(8 * 8 / 2);
    for (std::size_t i = 0; i < dxt.size(); ++i) dxt[i] = static_cast<uint8_t>(i * 7);
    const int page = src.Textures().AddEncoded("page", 0, dxt.data(), dxt.size(), 8, 8,
                                               TextureFormat::Dxt1);
    ASSERT_EQ(page, 0);
    const uint8_t rgba[4 * 4] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    ASSERT_EQ(src.Textures().Add("page", 1, rgba, 2, 2), 1);
    // A DXT1 entry of the wrong size is refused, like a mis-sized RGBA8 one.
    EXPECT_EQ(src.Textures().AddEncoded("bad", 0, dxt.data(), dxt.size() - 1, 8, 8,
                                        TextureFormat::Dxt1), -1);
    ASSERT_TRUE(WriteRoomFile(path.c_str(), src));

    Room dst;
    ASSERT_EQ(ReadRoomFile(path.c_str(), dst), RoomFileError::None);
    ASSERT_EQ(dst.Textures().Size(), 2u);
    const BakedTexture& t0 = dst.Textures().Textures()[0];
    EXPECT_EQ(t0.format, TextureFormat::Dxt1);
    EXPECT_EQ(t0.width, 8);
    ASSERT_EQ(t0.bytes, dxt.size());
    EXPECT_TRUE(std::equal(dxt.begin(), dxt.end(), dst.Textures().PixelsFor(0)));
    EXPECT_EQ(dst.Textures().Textures()[1].format, TextureFormat::Rgba8);
}

// Once the pages are on the GPU the CPU copy is dead weight (up to the 33 MB
// page budget) on a device with ~69 MB of RAM free. Releasing it keeps the
// entries (the census still counts them) and drops the bytes.
TEST(TexturePool, ReleasePixelsKeepsEntriesAndFreesBytes) {
    TexturePool pool;
    const uint8_t rgba[16] = {};
    ASSERT_EQ(pool.Add("page", 0, rgba, 2, 2), 0);
    pool.ReleasePixels();
    EXPECT_EQ(pool.Size(), 1u);
    EXPECT_EQ(pool.ByteSize(), 0u);
    EXPECT_EQ(pool.PixelsFor(0), nullptr);
}

// Blended surface pages keep smooth alpha as DXT5: 16 bytes a 4x4 block, a
// quarter of RGBA8 (the muzzle-flash cone alone needs most of a 1024 page).
TEST(TexturePool, Dxt5IsOneBytePerTexel) {
    TexturePool pool;
    std::vector<uint8_t> blocks(8 * 8);
    EXPECT_EQ(pool.AddEncoded("page", 0, blocks.data(), blocks.size(), 8, 8, TextureFormat::Dxt5), 0);
    EXPECT_EQ(pool.AddEncoded("page", 1, blocks.data(), blocks.size() / 2, 8, 8, TextureFormat::Dxt5), -1);
}

// A surface a script re-materials (a shell live or blank, a blinker on or off)
// carries one page per material, keyed by the material's albedo and emission:
// what the script's override holds at runtime.
TEST(RoomFile, PropMaterialVariantsRoundTrip) {
    const std::string path = TempPath("variants.room");
    Room src = MakeRoom();
    src.MutableNodes().Add("shell", -1, core::Mat4{}, true);
    std::vector<RoomProp> props(1);
    RoomProp& p = props[0];
    p.nodePath = "shell";
    p.node = 0;
    p.surface = 1;
    p.texture = 0;
    p.mesh.positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    p.mesh.normals = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    p.mesh.indices = {0, 1, 2};
    PropVariant live;
    live.albedo[0] = 1.0f; live.albedo[1] = 0.282353f; live.albedo[2] = 0.298039f; live.albedo[3] = 1.0f;
    live.texture = 1;
    PropVariant glow;
    glow.emission[0] = 1.0f;
    glow.texture = 0;
    glow.textureKey = TextureKeyHash("inline_textures/misc_display_rounds_endless_png.png");
    p.variants = {live, glow};
    src.AdoptProps(std::move(props));
    ASSERT_TRUE(WriteRoomFile(path.c_str(), src));
    std::FILE* f = std::fopen(path.c_str(), "rb");
    ASSERT_NE(f, nullptr);
    std::fseek(f, 0, SEEK_END);
    const long written = std::ftell(f);
    std::fclose(f);
    EXPECT_EQ(static_cast<uint64_t>(written), MeasureRoom(src).bytes);

    Room dst;
    ASSERT_EQ(ReadRoomFile(path.c_str(), dst), RoomFileError::None);
    ASSERT_EQ(dst.Props().size(), 1u);
    const std::vector<PropVariant>& v = dst.Props()[0].variants;
    ASSERT_EQ(v.size(), 2u);
    EXPECT_FLOAT_EQ(v[0].albedo[1], 0.282353f);
    EXPECT_EQ(v[0].texture, 1);
    EXPECT_FLOAT_EQ(v[1].emission[0], 1.0f);
    EXPECT_EQ(v[1].texture, 0);
    EXPECT_EQ(v[0].textureKey, 0u);
    EXPECT_EQ(v[1].textureKey, TextureKeyHash("inline_textures/misc_display_rounds_endless_png.png"));
}

// The renderer finds a variant by the override material's own numbers; a
// material no variant was baked for finds none, and the page at load is kept.
TEST(RoomProp, VariantTextureMatchesTheOverrideMaterial) {
    RoomProp p;
    p.texture = 3;
    PropVariant blank;
    blank.albedo[0] = 0.298039f; blank.albedo[1] = 0.509804f; blank.albedo[2] = 1.0f; blank.albedo[3] = 1.0f;
    blank.texture = 7;
    p.variants = {blank};
    const float blankAlbedo[4] = {0.298039f, 0.509804f, 1.0f, 1.0f};
    const float none[3] = {0.0f, 0.0f, 0.0f};
    EXPECT_EQ(p.VariantTexture(blankAlbedo, none, 0), 7);
    const float liveAlbedo[4] = {1.0f, 0.282353f, 0.298039f, 1.0f};
    EXPECT_EQ(p.VariantTexture(liveAlbedo, none, 0), -1);
    const float glow[3] = {1.0f, 0.0f, 0.0f};
    EXPECT_EQ(p.VariantTexture(blankAlbedo, glow, 0), -1);
}

// v10: each prop's bake pivot, and the light probe grid, so a moved prop can be
// scaled by probe(now) / probe(baked).
TEST(RoomFile, ProbeGridAndBakePivotsRoundTrip) {
    const std::string path = TempPath("probes.room");
    Room src = MakeRoom();
    src.MutableNodes().Add("gun", -1, core::Mat4{}, true);
    std::vector<RoomProp> props(1);
    RoomProp& p = props[0];
    p.nodePath = "gun";
    p.node = 0;
    p.mesh.positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    p.mesh.normals = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    p.mesh.indices = {0, 1, 2};
    p.hasBakePivot = true;
    p.bakePivot[0] = -3.5f;
    p.bakePivot[1] = 1.25f;
    p.bakePivot[2] = 8.0f;
    src.AdoptProps(std::move(props));
    ProbeGrid grid;
    grid.origin[0] = -10.0f;
    grid.origin[1] = -1.0f;
    grid.origin[2] = 0.5f;
    grid.step = 0.5f;
    grid.dims[0] = 3;
    grid.dims[1] = 2;
    grid.dims[2] = 1;
    for (int i = 0; i < 18; ++i) grid.rgb.push_back(0.1f * static_cast<float>(i));
    src.SetProbes(grid);
    ASSERT_TRUE(WriteRoomFile(path.c_str(), src));
    std::FILE* f = std::fopen(path.c_str(), "rb");
    ASSERT_NE(f, nullptr);
    std::fseek(f, 0, SEEK_END);
    const long written = std::ftell(f);
    std::fclose(f);
    EXPECT_EQ(static_cast<uint64_t>(written), MeasureRoom(src).bytes);

    Room dst;
    ASSERT_EQ(ReadRoomFile(path.c_str(), dst), RoomFileError::None);
    ASSERT_EQ(dst.Props().size(), 1u);
    EXPECT_TRUE(dst.Props()[0].hasBakePivot);
    EXPECT_FLOAT_EQ(dst.Props()[0].bakePivot[0], -3.5f);
    EXPECT_FLOAT_EQ(dst.Props()[0].bakePivot[2], 8.0f);
    const ProbeGrid& g = dst.Probes();
    EXPECT_EQ(g.dims[0], 3);
    EXPECT_EQ(g.dims[1], 2);
    EXPECT_EQ(g.dims[2], 1);
    EXPECT_FLOAT_EQ(g.origin[0], -10.0f);
    EXPECT_FLOAT_EQ(g.step, 0.5f);
    ASSERT_EQ(g.rgb.size(), 18u);
    EXPECT_FLOAT_EQ(g.rgb[17], 1.7f);
}

// EndlessMode swaps only the rounds display's texture (albedo and emission
// stay), and CrtIcon swaps active/inactive icons the same way, so a variant is
// keyed by its texture as well.
TEST(RoomProp, VariantTextureMatchesTheOverrideTexture) {
    RoomProp p;
    p.texture = 3;
    PropVariant endless;
    endless.emission[1] = 1.0f;
    endless.textureKey = TextureKeyHash("inline_textures/misc_display_rounds_endless_png.png");
    endless.texture = 9;
    p.variants = {endless};
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    const float green[3] = {0.0f, 1.0f, 0.0f};
    EXPECT_EQ(p.VariantTexture(white, green, TextureKeyHash("inline_textures/misc_display_rounds_endless_png.png")), 9);
    EXPECT_EQ(p.VariantTexture(white, green, TextureKeyHash("inline_textures/misc_display_rounds.png")), -1);
    EXPECT_EQ(TextureKeyHash(""), 0u);
    EXPECT_NE(TextureKeyHash("a"), TextureKeyHash("b"));
}

// The CRT icons fade in by animating albedo ALPHA (anim_icon fade) while the
// active or inactive variant is on: alpha is drawn as alpha, not matched.
TEST(RoomProp, VariantTextureIgnoresAlbedoAlpha) {
    RoomProp p;
    PropVariant active;
    active.albedo[3] = 0.0f;
    active.textureKey = TextureKeyHash("inline_textures/misc_crt_icon_friends2.png");
    active.texture = 4;
    p.variants = {active};
    const float faded[4] = {1.0f, 1.0f, 1.0f, 0.6f};
    const float none[3] = {0.0f, 0.0f, 0.0f};
    EXPECT_EQ(p.VariantTexture(faded, none, TextureKeyHash("inline_textures/misc_crt_icon_friends2.png")), 4);
}

// A prop baked at another Environment grade than the scene's (the car, at the
// ending's) carries it (v13); the renderer grades it relative to that grade.
TEST(RoomFile, APropsBakeGradeRoundTrips) {
    const std::string path = TempPath("grade.room");
    Room src = MakeRoom();
    src.MutableNodes().Add("car", -1, core::Mat4{}, true);
    src.MutableNodes().Add("table", -1, core::Mat4{}, true);
    std::vector<RoomProp> props(2);
    for (int i = 0; i < 2; ++i) {
        props[i].nodePath = i == 0 ? "car" : "table";
        props[i].node = i;
        props[i].mesh.positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        props[i].mesh.normals = {0, 0, 1, 0, 0, 1, 0, 0, 1};
        props[i].mesh.indices = {0, 1, 2};
    }
    props[0].bakeBrightness = 5.68f;
    props[0].bakeContrast = 1.3f;
    src.AdoptProps(std::move(props));
    ASSERT_TRUE(WriteRoomFile(path.c_str(), src));
    Room dst;
    ASSERT_EQ(ReadRoomFile(path.c_str(), dst), RoomFileError::None);
    ASSERT_EQ(dst.Props().size(), 2u);
    EXPECT_TRUE(dst.Props()[0].HasOwnGrade());
    EXPECT_FLOAT_EQ(dst.Props()[0].bakeBrightness, 5.68f);
    EXPECT_FLOAT_EQ(dst.Props()[0].bakeContrast, 1.3f);
    EXPECT_FALSE(dst.Props()[1].HasOwnGrade()) << "0: the scene's grade";
}

// A bake grade needs both factors: brightness 0 with a contrast would divide by
// zero in the runtime's re-grade (NaN colours).
TEST(RoomProp, AnOwnGradeNeedsBothFactors) {
    RoomProp p;
    EXPECT_FALSE(p.HasOwnGrade());
    p.bakeContrast = 1.3f;
    EXPECT_FALSE(p.HasOwnGrade()) << "brightness 0";
    p.bakeBrightness = 5.68f;
    EXPECT_TRUE(p.HasOwnGrade());
}

// Each texture page is read into a buffer of its own. One ~25-32 MB buffer
// cannot find a hole in a fragmented heap on a second trip into the table, so
// newlib extends its arena by the whole block, toward the Vita's fixed 128 MB.
TEST(RoomFile, EachTextureIsReadIntoItsOwnBuffer) {
    Room src = MakeRoom();
    const uint8_t dxt1[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint8_t rgba[16] = {9, 9, 9, 255, 8, 8, 8, 255, 7, 7, 7, 255, 6, 6, 6, 255};
    ASSERT_EQ(src.Textures().AddEncoded("a.glb", 0, dxt1, sizeof dxt1, 4, 4, TextureFormat::Dxt1), 0);
    ASSERT_EQ(src.Textures().AddEncoded("b.glb", 1, rgba, sizeof rgba, 2, 2, TextureFormat::Rgba8), 1);
    const std::string path = TempPath("chunked_textures.room");
    ASSERT_TRUE(WriteRoomFile(path.c_str(), src));

    Room dst;
    ASSERT_EQ(ReadRoomFile(path.c_str(), dst), RoomFileError::None);

    EXPECT_TRUE(dst.Textures().Chunked());
    EXPECT_TRUE(dst.Textures().Pixels().empty()) << "no single pool buffer";
    EXPECT_EQ(dst.Textures().ByteSize(), sizeof dxt1 + sizeof rgba);
    ASSERT_NE(dst.Textures().PixelsFor(0), nullptr);
    ASSERT_NE(dst.Textures().PixelsFor(1), nullptr);
    EXPECT_TRUE(std::equal(dxt1, dxt1 + sizeof dxt1, dst.Textures().PixelsFor(0)));
    EXPECT_TRUE(std::equal(rgba, rgba + sizeof rgba, dst.Textures().PixelsFor(1)));
    EXPECT_EQ(dst.Batches().size(), src.Batches().size()) << "the batches after the pixels still read";
    EXPECT_FALSE(WriteRoomFile(TempPath("rewritten.room").c_str(), dst)) << "a read-back pool is not written";
}
