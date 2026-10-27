// The page builder end to end on a tiny room: one lit quad becomes one page
// whose texel is exactly the texel colour of that point.
#include "bake_surfaces.h"

#include "assets/light_model.h"
#include "assets/scene_fog.h"
#include "assets/probe_grid.h"

#include <gtest/gtest.h>

#include <map>

#include <array>

using namespace assets;

namespace {

// A 1x1 quad at y = 0, facing up, one merged batch with one source.
Room QuadRoom() {
    RoomBatch b;
    b.mesh.positions = {-0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, 0, 0.5f, -0.5f, 0, 0.5f};
    b.mesh.normals = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    b.mesh.indices = {0, 2, 1, 0, 3, 2};
    b.triSource = {0, 0};
    Room room;
    std::vector<RoomBatch> batches;
    batches.push_back(std::move(b));
    room.AdoptBatches(std::move(batches));
    room.AdoptSources({{"quad", 0}});
    return room;
}

}  // namespace

TEST(BakeSurfaces, ThePageBudgetIs33Megabytes) {
    EXPECT_EQ(tools::kPageBudgetMegabytes, 33.0f);
    EXPECT_EQ(tools::SurfaceBakeInput{}.maxMegabytes, tools::kPageBudgetMegabytes);
}

TEST(BakeSurfaces, OneLitQuadIsOnePageOfItsTexelColour) {
    const Room room = QuadRoom();
    tools::SurfaceBakeInput in;
    BakedLight l;
    l.position[1] = 2.0f;
    l.energy = 2.0f;
    l.range = 10.0f;
    l.attenuation = 1.0f;
    in.lights = {l};
    in.sourceMaterial = {SurfaceMaterial{}};
    TexturePool pool;
    in.albedo = &pool;
    in.batchTexture = {-1};
    in.pageSize = 64;
    in.threads = 1;

    tools::SurfaceBakeResult out;
    ASSERT_TRUE(tools::BakeSurfaces(room, in, out)) << out.error;
    ASSERT_EQ(out.pages.size(), 1u);
    EXPECT_FALSE(out.pages[0].alpha);
    EXPECT_EQ(out.pages[0].encoded.size(), 64u * 64u / 2u);   // DXT1
    ASSERT_EQ(out.batches.size(), 1u);
    EXPECT_EQ(out.batches[0].page, 0);
    EXPECT_EQ(out.batches[0].mesh.TriangleCount(), 2);
    ASSERT_EQ(out.batches[0].mesh.uv2.size(), out.batches[0].mesh.positions.size() / 3 * 2);
    for (float c : out.batches[0].mesh.uv2) {
        EXPECT_GE(c, 0.0f);
        EXPECT_LE(c, 1.0f);
    }
    // The texel under the quad's centre: StaticLight there is the lamp straight
    // above (energy 2 * attenuation at 2 units), V toward home (off-axis, so
    // Burley's view term applies). Recompute it, and grade.
    const Mesh& m = out.batches[0].mesh;
    float uvCentre[2] = {0, 0};
    for (std::size_t v = 0; v < m.uv2.size() / 2; ++v) {
        uvCentre[0] += m.uv2[v * 2] / static_cast<float>(m.uv2.size() / 2);
        uvCentre[1] += m.uv2[v * 2 + 1] / static_cast<float>(m.uv2.size() / 2);
    }
    const int x = static_cast<int>(uvCentre[0] * 64.0f), y = static_cast<int>(uvCentre[1] * 64.0f);
    const uint8_t* texel = &out.pages[0].rgba[(static_cast<std::size_t>(y) * 64 + x) * 4];
    LightBvh none;
    none.Build({});
    const float pos[3] = {0, 0, 0}, up[3] = {0, 1, 0}, white[3] = {1, 1, 1};
    float light[3], expected[3];
    StaticLight(in.lights, none, pos, up, kHomeSocket, 1.0f, light);
    TexelColour(white, SurfaceMaterial{}, light, light::Grade{}, expected);
    for (int c = 0; c < 3; ++c) {
        // One posterize step of tolerance: the texel averages its subsamples'
        // light over a few centimetres of the quad.
        EXPECT_NEAR(texel[c] / 255.0f, expected[c], 1.0f / 7.0f + 1e-3f) << "channel " << c;
    }
    EXPECT_GT(texel[0], 0);   // lit, not the empty page
}

// Props are lit at their bake pose (bake_room poses each where its reveal
// animation leaves it), but another prop sits at ITS rest pose, which may not
// be where it is when both are seen. So a prop takes shadows from the static
// room only, never from another prop; the room takes props' shadows.
TEST(BakeSurfaces, APropIsShadowedByTheRoomButNotByAnotherProp) {
    Room room = QuadRoom();                // the batch: a quad at y = 0
    std::vector<RoomProp> props(3);
    for (int i = 0; i < 3; ++i) {
        props[i].mesh.positions = {-0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, 0, 0.5f, -0.5f, 0, 0.5f};
        props[i].mesh.normals = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
        props[i].mesh.indices = {0, 2, 1, 0, 3, 2};
        props[i].nodePath = i == 0 ? "lower" : (i == 1 ? "upper" : "under the room");
        props[i].visibleAtLoad = true;
    }
    props[0].bakeWorld.m[13] = 0.5f;       // lower prop at y = 0.5
    props[1].bakeWorld.m[13] = 1.0f;       // upper prop at y = 1.0, between it and the lamp
    props[2].bakeWorld.m[13] = -0.5f;      // under the room's quad, which hides the lamp
    room.AdoptProps(std::move(props));

    tools::SurfaceBakeInput in;
    BakedLight l;
    l.position[1] = 2.0f;
    l.energy = 2.0f;
    l.range = 10.0f;
    l.attenuation = 1.0f;
    l.shadow = true;
    in.lights = {l};
    in.sourceMaterial = {SurfaceMaterial{}};
    in.propMaterial = {SurfaceMaterial{}, SurfaceMaterial{}, SurfaceMaterial{}};
    in.propAlpha = {0, 0, 0};
    in.propTexture = {-1, -1, -1};
    TexturePool pool;
    in.albedo = &pool;
    in.batchTexture = {-1};
    in.pageSize = 64;
    in.threads = 1;
    tools::SurfaceBakeResult out;
    ASSERT_TRUE(tools::BakeSurfaces(room, in, out)) << out.error;

    // Mean texel brightness of one prop's chart on its page.
    auto meanOf = [&](const std::string& path) {
        for (const RoomProp& p : out.props) {
            if (p.nodePath != path) continue;
            const tools::SurfacePage& pg = out.pages[static_cast<std::size_t>(p.page)];
            float cx = 0, cy = 0;
            const std::size_t n = p.mesh.uv2.size() / 2;
            for (std::size_t v = 0; v < n; ++v) { cx += p.mesh.uv2[v * 2] / n; cy += p.mesh.uv2[v * 2 + 1] / n; }
            const int x = static_cast<int>(cx * pg.size), y = static_cast<int>(cy * pg.size);
            return pg.rgba[(static_cast<std::size_t>(y) * pg.size + x) * 4];
        }
        return uint8_t{0};
    };
    EXPECT_GT(meanOf("lower"), 0);   // lit, though "upper" sits between it and the lamp
    EXPECT_GT(meanOf("upper"), 0);
    EXPECT_EQ(meanOf("under the room"), 0);   // the static room does shadow a prop
}

// A prop lit at its reveal pose (the item compartments raised) still shadows
// the room from where it rests: where it is for most of the game, and where
// Godot has it whenever the room around it is seen.
TEST(BakeSurfaces, APropShadowsTheRoomFromItsRestPoseNotItsBakePose) {
    Room room = QuadRoom();                // the batch: a quad at y = 0
    std::vector<RoomProp> props(1);
    props[0].mesh.positions = {-0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, 0, 0.5f, -0.5f, 0, 0.5f};
    props[0].mesh.normals = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    props[0].mesh.indices = {0, 2, 1, 0, 3, 2};
    props[0].nodePath = "raised";
    props[0].visibleAtLoad = true;
    props[0].bakeWorld.m[13] = 1.0f;       // lit raised, between the room and the lamp
    props[0].castWorld.m[13] = -1.0f;      // rests below the room: casts nothing on it
    props[0].hasCastWorld = true;
    room.AdoptProps(std::move(props));

    tools::SurfaceBakeInput in;
    BakedLight l;
    l.position[1] = 2.0f;
    l.energy = 2.0f;
    l.range = 10.0f;
    l.attenuation = 1.0f;
    l.shadow = true;
    in.lights = {l};
    in.sourceMaterial = {SurfaceMaterial{}};
    in.propMaterial = {SurfaceMaterial{}};
    in.propAlpha = {0};
    in.propTexture = {-1};
    TexturePool pool;
    in.albedo = &pool;
    in.batchTexture = {-1};
    in.pageSize = 64;
    in.threads = 1;
    tools::SurfaceBakeResult out;
    ASSERT_TRUE(tools::BakeSurfaces(room, in, out)) << out.error;
    ASSERT_EQ(out.batches.size(), 1u);
    const Mesh& m = out.batches[0].mesh;
    float cx = 0, cy = 0;
    const std::size_t n = m.uv2.size() / 2;
    for (std::size_t v = 0; v < n; ++v) { cx += m.uv2[v * 2] / n; cy += m.uv2[v * 2 + 1] / n; }
    const tools::SurfacePage& pg = out.pages[static_cast<std::size_t>(out.batches[0].page)];
    const int x = static_cast<int>(cx * pg.size), y = static_cast<int>(cy * pg.size);
    EXPECT_GT(pg.rgba[(static_cast<std::size_t>(y) * pg.size + x) * 4], 0);   // lit: nothing above it
}

// A texel is lit where it is SEEN: a moved prop is baked at its rest pose and
// at its reveal pose, and each texel keeps the brighter. The shell compartment
// flips over: its felt faces up at rest, its slotted top once revealed.
TEST(BakeSurfaces, ATexelKeepsTheBrighterOfItsRestAndRevealPoses) {
    Room room = QuadRoom();
    std::vector<RoomProp> props(1);
    props[0].mesh.positions = {-0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, 0, 0.5f, -0.5f, 0, 0.5f};
    props[0].mesh.normals = {0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1, 0};   // faces DOWN at rest
    props[0].mesh.indices = {0, 1, 2, 0, 2, 3};
    props[0].nodePath = "flipper";
    props[0].visibleAtLoad = true;
    props[0].bakeWorld.m[13] = 1.0f;           // rest: y = 1, facing away from the lamp
    props[0].altWorld = props[0].bakeWorld;    // reveal: flipped over (rotate pi about x)
    props[0].altWorld.m[5] = -1.0f;
    props[0].altWorld.m[10] = -1.0f;
    props[0].hasAltWorld = true;
    room.AdoptProps(std::move(props));

    tools::SurfaceBakeInput in;
    BakedLight l;
    l.position[1] = 3.0f;
    l.energy = 2.0f;
    l.range = 10.0f;
    l.attenuation = 1.0f;
    in.lights = {l};
    in.sourceMaterial = {SurfaceMaterial{}};
    in.propMaterial = {SurfaceMaterial{}};
    in.propAlpha = {0};
    in.propTexture = {-1};
    TexturePool pool;
    in.albedo = &pool;
    in.batchTexture = {-1};
    in.pageSize = 64;
    in.threads = 1;
    tools::SurfaceBakeResult out;
    ASSERT_TRUE(tools::BakeSurfaces(room, in, out)) << out.error;
    ASSERT_EQ(out.props.size(), 1u);
    const Mesh& m = out.props[0].mesh;
    float cx = 0, cy = 0;
    const std::size_t n = m.uv2.size() / 2;
    for (std::size_t v = 0; v < n; ++v) { cx += m.uv2[v * 2] / n; cy += m.uv2[v * 2 + 1] / n; }
    const tools::SurfacePage& pg = out.pages[static_cast<std::size_t>(out.props[0].page)];
    const int x = static_cast<int>(cx * pg.size), y = static_cast<int>(cy * pg.size);
    EXPECT_GT(pg.rgba[(static_cast<std::size_t>(y) * pg.size + x) * 4], 0);   // lit by its revealed pose
}

// Some props are seen in more than two poses: the signature machine's lid is
// open while the player signs and shut as it leaves. A texel takes the
// brightest of all of them, so a face only an extra pose turns to the lamp is
// still lit.
TEST(BakeSurfaces, ATexelKeepsTheBrightestOfItsExtraPosesToo) {
    Room room = QuadRoom();
    std::vector<RoomProp> props(1);
    props[0].mesh.positions = {-0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, 0, 0.5f, -0.5f, 0, 0.5f};
    props[0].mesh.normals = {0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1, 0};   // faces DOWN
    props[0].mesh.indices = {0, 1, 2, 0, 2, 3};
    props[0].nodePath = "lid";
    props[0].visibleAtLoad = true;
    props[0].bakeWorld.m[13] = 1.0f;           // seen facing away from the lamp
    props[0].altWorld = props[0].bakeWorld;    // and at rest, beside it, facing away too
    props[0].altWorld.m[12] = 2.0f;
    props[0].hasAltWorld = true;
    core::Mat4 flipped = props[0].bakeWorld;   // the extra pose: turned over, facing up
    flipped.m[5] = -1.0f;
    flipped.m[10] = -1.0f;
    props[0].extraPoses = {flipped};
    room.AdoptProps(std::move(props));

    tools::SurfaceBakeInput in;
    BakedLight l;
    l.position[1] = 3.0f;
    l.energy = 2.0f;
    l.range = 10.0f;
    l.attenuation = 1.0f;
    in.lights = {l};
    in.sourceMaterial = {SurfaceMaterial{}};
    in.propMaterial = {SurfaceMaterial{}};
    in.propAlpha = {0};
    in.propTexture = {-1};
    TexturePool pool;
    in.albedo = &pool;
    in.batchTexture = {-1};
    in.pageSize = 64;
    in.threads = 1;
    tools::SurfaceBakeResult out;
    ASSERT_TRUE(tools::BakeSurfaces(room, in, out)) << out.error;
    ASSERT_EQ(out.props.size(), 1u);
    const Mesh& m = out.props[0].mesh;
    float cx = 0, cy = 0;
    const std::size_t n = m.uv2.size() / 2;
    for (std::size_t v = 0; v < n; ++v) { cx += m.uv2[v * 2] / n; cy += m.uv2[v * 2 + 1] / n; }
    const tools::SurfacePage& pg = out.pages[static_cast<std::size_t>(out.props[0].page)];
    const int x = static_cast<int>(cx * pg.size), y = static_cast<int>(cy * pg.size);
    EXPECT_GT(pg.rgba[(static_cast<std::size_t>(y) * pg.size + x) * 4], 0);   // lit by the extra pose
}

// A surface a script re-materials is baked once per material, onto pages of
// its own sharing its UV2, so the override picks a page and the colour stays
// Godot's own. (A blank shell is blue albedo under an amber lamp: grey, where
// tinting the load colour blue gives a saturated blue.)
TEST(BakeSurfaces, AVariantSurfaceGetsOnePagePerMaterialAtTheSameUv) {
    Room room = QuadRoom();
    std::vector<RoomProp> props(1);
    props[0].mesh.positions = {-0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, 0, 0.5f, -0.5f, 0, 0.5f};
    props[0].mesh.normals = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    props[0].mesh.indices = {0, 2, 1, 0, 3, 2};
    props[0].nodePath = "shell";
    props[0].visibleAtLoad = true;
    props[0].bakeWorld.m[13] = 1.0f;
    room.AdoptProps(std::move(props));

    tools::SurfaceBakeInput in;
    BakedLight l;
    l.position[1] = 3.0f;
    l.energy = 2.0f;
    l.range = 10.0f;
    l.attenuation = 1.0f;
    in.lights = {l};
    in.sourceMaterial = {SurfaceMaterial{}};
    in.propMaterial = {SurfaceMaterial{}};
    in.propAlpha = {0};
    in.propTexture = {-1};
    tools::PropVariantInput red, blue;
    red.material.albedo[1] = red.material.albedo[2] = 0.0f;
    red.albedo[1] = red.albedo[2] = 0.0f;
    blue.material.albedo[0] = blue.material.albedo[1] = 0.0f;
    blue.albedo[0] = blue.albedo[1] = 0.0f;
    blue.textureKey = 77u;
    in.propVariants = {{red, blue}};
    TexturePool pool;
    in.albedo = &pool;
    in.batchTexture = {-1};
    in.pageSize = 64;
    in.threads = 1;
    tools::SurfaceBakeResult out;
    ASSERT_TRUE(tools::BakeSurfaces(room, in, out)) << out.error;

    ASSERT_EQ(out.props.size(), 1u);
    const RoomProp& p = out.props[0];
    ASSERT_EQ(p.variants.size(), 2u);
    EXPECT_FLOAT_EQ(p.variants[0].albedo[1], 0.0f);   // the keys travel
    EXPECT_FLOAT_EQ(p.variants[1].albedo[2], 1.0f);
    EXPECT_EQ(p.variants[0].textureKey, 0u);
    EXPECT_EQ(p.variants[1].textureKey, 77u);
    ASSERT_GE(p.variants[0].page, 0);
    ASSERT_GE(p.variants[1].page, 0);
    EXPECT_NE(p.variants[0].page, p.page);
    EXPECT_NE(p.variants[1].page, p.page);
    EXPECT_NE(p.variants[0].page, p.variants[1].page);
    auto texel = [&](int page) {
        const tools::SurfacePage& pg = out.pages[static_cast<std::size_t>(page)];
        float cx = 0, cy = 0;
        const std::size_t n = p.mesh.uv2.size() / 2;
        for (std::size_t v = 0; v < n; ++v) { cx += p.mesh.uv2[v * 2] / n; cy += p.mesh.uv2[v * 2 + 1] / n; }
        const std::size_t t = static_cast<std::size_t>(cy * pg.size) * pg.size + static_cast<std::size_t>(cx * pg.size);
        return std::array<int, 3>{pg.rgba[t * 4], pg.rgba[t * 4 + 1], pg.rgba[t * 4 + 2]};
    };
    const auto base = texel(p.page), r = texel(p.variants[0].page), b = texel(p.variants[1].page);
    EXPECT_GT(base[0], 0);
    EXPECT_GT(r[0], r[2]);
    EXPECT_GT(b[2], b[0]);
}

// The shells' live and blank colours are tinted back in after the grade: the
// room's grade keeps a quarter of the saturation, so under the amber lamp a
// blank shell reads grey, a live one pale pink, and both go white when the
// screen brightens (adrenaline, the revival).
TEST(BakeSurfaces, ATintedVariantKeepsItsHueThroughTheGrade) {
    Room room = QuadRoom();
    std::vector<RoomProp> props(1);
    props[0].mesh.positions = {-0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, 0, 0.5f, -0.5f, 0, 0.5f};
    props[0].mesh.normals = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    props[0].mesh.indices = {0, 2, 1, 0, 3, 2};
    props[0].nodePath = "shell";
    props[0].visibleAtLoad = true;
    props[0].bakeWorld.m[13] = 1.0f;
    room.AdoptProps(std::move(props));

    tools::SurfaceBakeInput in;
    BakedLight l;
    l.position[1] = 3.0f;
    l.colour[0] = 1.0f;   // amber, as the table lamp
    l.colour[1] = 0.45f;
    l.colour[2] = 0.05f;
    l.energy = 2.0f;
    l.range = 10.0f;
    l.attenuation = 1.0f;
    in.lights = {l};
    in.sourceMaterial = {SurfaceMaterial{}};
    in.propMaterial = {SurfaceMaterial{}};
    in.propAlpha = {0};
    in.propTexture = {-1};
    tools::PropVariantInput blank, tinted, live;
    blank.material.albedo[0] = 0.29f;
    blank.material.albedo[1] = 0.5f;
    tinted = blank;
    tinted.tinted = true;
    tinted.tint[0] = 0.29f;
    tinted.tint[1] = 0.5f;
    tinted.tint[2] = 1.0f;
    live.material.albedo[1] = 0.28f;
    live.material.albedo[2] = 0.29f;
    live.tinted = true;
    live.tint[0] = 1.0f;
    live.tint[1] = 0.28f;
    live.tint[2] = 0.29f;
    in.propVariants = {{blank, tinted, live}};
    TexturePool pool;
    in.albedo = &pool;
    in.batchTexture = {-1};
    in.pageSize = 64;
    in.threads = 1;
    tools::SurfaceBakeResult out;
    ASSERT_TRUE(tools::BakeSurfaces(room, in, out)) << out.error;

    const RoomProp& p = out.props[0];
    ASSERT_EQ(p.variants.size(), 3u);
    auto texel = [&](int page) {
        const tools::SurfacePage& pg = out.pages[static_cast<std::size_t>(page)];
        float cx = 0, cy = 0;
        const std::size_t n = p.mesh.uv2.size() / 2;
        for (std::size_t v = 0; v < n; ++v) { cx += p.mesh.uv2[v * 2] / n; cy += p.mesh.uv2[v * 2 + 1] / n; }
        const std::size_t t = static_cast<std::size_t>(cy * pg.size) * pg.size + static_cast<std::size_t>(cx * pg.size);
        return std::array<int, 3>{pg.rgba[t * 4], pg.rgba[t * 4 + 1], pg.rgba[t * 4 + 2]};
    };
    const auto grey = texel(p.variants[0].page), b = texel(p.variants[1].page), r = texel(p.variants[2].page);
    EXPECT_LE(grey[2], grey[0]);        // untinted: the lamp wins, no blue left
    EXPECT_GT(b[2], 2 * b[0]);          // tinted blank: blue
    EXPECT_GT(r[0], 2 * r[2]);          // tinted live: red
    EXPECT_GT(r[0], 2 * r[1]);
}

// Godot perturbs the normal with the material's normal map (the shipped
// DXT5_RA_AS_RG maps have Y = 0, as on the defib cutters). A map of (0.5, 0)
// turns an up-facing quad's normal to -binormal = +z. A lamp level with the
// quad, out along +z, cannot light the plain quad (N.L = 0) and fully lights
// the mapped one.
namespace {

uint8_t CentreTexel(bool mapped, int mapSize = 2, bool flatMip1 = false) {
    Room room = QuadRoom();
    {
        std::vector<RoomBatch> batches = room.Batches();
        Mesh& m = batches[0].mesh;
        m.uvs = {0, 0, 1, 0, 1, 1, 0, 1};
        m.tangents = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
        room.AdoptBatches(std::move(batches));
    }
    tools::SurfaceBakeInput in;
    BakedLight l;
    l.position[2] = 5.0f;
    l.energy = 8.0f;
    l.range = 20.0f;
    l.attenuation = 1.0f;
    in.lights = {l};
    in.sourceMaterial = {SurfaceMaterial{}};
    TexturePool pool;
    in.albedo = &pool;
    in.batchTexture = {-1};
    TexturePool normals;
    std::vector<uint8_t> yZero;
    for (int i = 0; i < mapSize * mapSize; ++i) yZero.insert(yZero.end(), {128, 0, 0, 255});
    const int map = normals.Add("test", 0, yZero.data(), mapSize, mapSize);
    in.normal = &normals;
    in.sourceNormal = {mapped ? map : -1};
    if (flatMip1) {
        std::vector<uint8_t> flat;
        for (int i = 0; i < mapSize * mapSize / 4; ++i) flat.insert(flat.end(), {128, 128, 0, 255});
        in.normalMip1.assign(1, normals.Add("test_mip1", 0, flat.data(), mapSize / 2, mapSize / 2));
    }
    in.pageSize = 64;
    in.threads = 1;
    tools::SurfaceBakeResult out;
    if (!tools::BakeSurfaces(room, in, out)) return 255;
    const Mesh& m = out.batches[0].mesh;
    float uvCentre[2] = {0, 0};
    for (std::size_t v = 0; v < m.uv2.size() / 2; ++v) {
        uvCentre[0] += m.uv2[v * 2] / static_cast<float>(m.uv2.size() / 2);
        uvCentre[1] += m.uv2[v * 2 + 1] / static_cast<float>(m.uv2.size() / 2);
    }
    const int x = static_cast<int>(uvCentre[0] * 64.0f), y = static_cast<int>(uvCentre[1] * 64.0f);
    return out.pages[0].rgba[(static_cast<std::size_t>(y) * 64 + x) * 4];
}

}  // namespace

TEST(BakeSurfaces, TheNormalMapTurnsTheNormalItIsLitBy) {
    EXPECT_EQ(CentreTexel(false), 0);   // grazing: Burley gives nothing
    EXPECT_GT(CentreTexel(true), 100);  // facing the lamp
}

// The probe grid stores, per point, the mean of the static light over the six
// axis normals, with the static room's shadows: all a ratio needs.
TEST(BakeSurfaces, AProbeIsTheMeanStaticLightOverSixNormals) {
    const Room room = QuadRoom();
    tools::SurfaceBakeInput in;
    BakedLight l;
    l.position[1] = 2.0f;
    l.energy = 2.0f;
    l.range = 10.0f;
    l.attenuation = 1.0f;
    in.lights = {l};
    const float lo[3] = {-0.5f, 0.5f, -0.5f}, hi[3] = {0.5f, 1.0f, 0.5f};
    const ProbeGrid g = tools::BakeProbes(room, in, lo, hi, 0.5f, 1u);
    EXPECT_EQ(g.dims[0], 3);
    EXPECT_EQ(g.dims[1], 2);
    EXPECT_EQ(g.dims[2], 3);
    ASSERT_EQ(g.rgb.size(), 18u * 3u);
    LightBvh bvh;
    bvh.Build({-0.5f, 0, -0.5f, 0.5f, 0, 0.5f, 0.5f, 0, -0.5f, -0.5f, 0, -0.5f, -0.5f, 0, 0.5f, 0.5f, 0, 0.5f});
    const float at[3] = {0.0f, 1.0f, 0.0f};   // point (1, 1, 1)
    const float axes[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    float mean = 0.0f;
    for (const auto& n : axes) {
        float c[3];
        StaticLight(in.lights, bvh, at, n, kHomeSocket, 1.0f, c, 1u);
        mean += c[0] / 6.0f;
    }
    EXPECT_GT(mean, 0.0f);
    EXPECT_NEAR(g.rgb[((1 * 2 + 1) * 3 + 1) * 3], mean, 1e-5f);
}

// The animated lights, per vertex, one per channel. A vertex under a light
// gets its scalar at energy 1 (attenuation * Burley N.L * shadow); a channel
// with no light stays 0; a light whose cull mask misses the layers leaves it 0
// too.
TEST(BakeSurfaces, DynLayersHoldEachAnimatedLightPerVertex) {
    Mesh m;
    m.positions = {0, 0, 0, 1, 0, 0, 0, 0, 1};
    m.normals = {0, 1, 0, 0, 1, 0, 0, 1, 0};
    m.indices = {0, 2, 1};
    BakedLight above;
    above.position[1] = 2.0f;
    above.energy = 1.0f;
    above.range = 10.0f;
    above.attenuation = 1.0f;
    BakedLight culled = above;
    culled.cullMask = 0x2u;
    LightBvh none;
    none.Build({});
    tools::BakeDynLayers({above, culled}, none, core::Mat4{}, 1u, m);
    ASSERT_EQ(m.colours.size(), 12u);
    float expected[3];
    const float pos[3] = {0, 0, 0}, up[3] = {0, 1, 0};
    StaticLight({above}, none, pos, up, kHomeSocket, 1.0f, expected, 1u);
    EXPECT_EQ(m.colours[0], light::EncodeDynLayer(expected[0]));
    EXPECT_GT(m.colours[0], 0);
    EXPECT_EQ(m.colours[1], 0);   // culled
    EXPECT_EQ(m.colours[2], 0);   // no third light
    EXPECT_EQ(m.colours[3], 0);
}

// The shipped maps' mip 0 is Y = 0 and mip 1 is not; from `home` a 256-texel
// map on a unit quad is minified past mip 1, so the flat mip 1 is what lights
// it: the lamp level with the quad gives nothing, as for the plain quad.
TEST(BakeSurfaces, TheNormalMapIsSampledAtTheMipHomeSeesItAt) {
    EXPECT_GT(CentreTexel(true, 2, false), 100);   // magnified: mip 0 turns it
    EXPECT_EQ(CentreTexel(true, 256, true), 0);    // minified: mip 1, flat
}

// A scene that is not main is baked with its Environment (the menu: ambient
// colour at energy 1.29, contrast 0.95, saturation 0.36) and its own camera as
// the eye. Godot 4.1.4 adds srgb_to_linear(ambient colour) * energy
// (render_scene_data_rd.cpp:144-149), times albedo and 1 - metallic
// (scene_forward_clustered.glsl:1497, 2200), the same place the static light
// goes, so the bake adds it to each sample's light.
namespace {

uint8_t CentreRed(const tools::SurfaceBakeInput& in) {
    tools::SurfaceBakeResult out;
    if (!tools::BakeSurfaces(QuadRoom(), in, out) || out.pages.empty()) return 0;
    const Mesh& m = out.batches[0].mesh;
    float u = 0, v = 0;
    for (std::size_t i = 0; i < m.uv2.size() / 2; ++i) {
        u += m.uv2[i * 2] / static_cast<float>(m.uv2.size() / 2);
        v += m.uv2[i * 2 + 1] / static_cast<float>(m.uv2.size() / 2);
    }
    const int x = static_cast<int>(u * 64.0f), y = static_cast<int>(v * 64.0f);
    return out.pages[0].rgba[(static_cast<std::size_t>(y) * 64 + x) * 4];
}

tools::SurfaceBakeInput DarkQuadInput(TexturePool& pool) {
    tools::SurfaceBakeInput in;
    in.sourceMaterial = {SurfaceMaterial{}};
    in.albedo = &pool;
    in.batchTexture = {-1};
    in.pageSize = 64;
    in.threads = 1;
    return in;
}

}  // namespace

TEST(BakeSurfaces, TheDefaultProfileIsMains) {
    const tools::SurfaceBakeInput in;
    for (int a = 0; a < 3; ++a) {
        EXPECT_EQ(in.eye[a], kHomeSocket[a]);
        EXPECT_EQ(in.ambient[a], 0.0f);
    }
    EXPECT_EQ(in.grade.contrast, light::Grade{}.contrast);
}

TEST(BakeSurfaces, AmbientLightsASceneWithNoLamps) {
    TexturePool pool;
    tools::SurfaceBakeInput in = DarkQuadInput(pool);
    const uint8_t dark = CentreRed(in);
    for (float& c : in.ambient) c = 0.3f;
    const uint8_t lit = CentreRed(in);
    EXPECT_GT(lit, dark);
    // Exactly the texel colour of albedo x ambient under the grade.
    const float white[3] = {1, 1, 1}, amb[3] = {0.3f, 0.3f, 0.3f};
    float expected[3];
    TexelColour(white, SurfaceMaterial{}, amb, in.grade, expected);
    EXPECT_NEAR(lit / 255.0f, expected[0], 1.0f / 255.0f + 1e-3f);
}

TEST(BakeSurfaces, TheGradeIsTheScenes) {
    TexturePool pool;
    tools::SurfaceBakeInput in = DarkQuadInput(pool);
    for (float& c : in.ambient) c = 0.3f;
    const uint8_t mainGrade = CentreRed(in);
    in.grade.saturation = 0.36f;
    in.grade.contrast = 0.95f;
    const uint8_t menuGrade = CentreRed(in);
    EXPECT_NE(mainGrade, menuGrade);
}

// A prop marked detail (the counter's screens) is planned at its texture's
// density, not the tier cap: a 128-texel texture on a unit quad is 128/u.
TEST(BakeSurfaces, ADetailPropIsPlannedAtItsTexturesDensity) {
    auto plan = [](bool detail) {
        Room room = QuadRoom();
        std::vector<RoomProp> props(1);
        props[0].mesh.positions = {-0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, 0, 0.5f, -0.5f, 0, 0.5f};
        props[0].mesh.normals = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
        props[0].mesh.uvs = {0, 0, 1, 0, 1, 1, 0, 1};
        props[0].mesh.indices = {0, 2, 1, 0, 3, 2};
        props[0].nodePath = "screen";
        room.AdoptProps(std::move(props));
        tools::SurfaceBakeInput in;
        in.sourceMaterial = {SurfaceMaterial{}};
        in.propMaterial = {SurfaceMaterial{}};
        in.propAlpha = {0};
        TexturePool pool;
        std::vector<uint8_t> px(128 * 128 * 4, 255);
        const int tex = pool.Add("screen.glb", 0, px.data(), 128, 128);
        in.albedo = &pool;
        in.batchTexture = {-1};
        in.propTexture = {tex};
        in.propDetail = {static_cast<uint8_t>(detail ? 1 : 0)};
        in.planOnly = true;
        in.threads = 1;
        tools::SurfaceBakeResult out;
        EXPECT_TRUE(tools::BakeSurfaces(room, in, out)) << out.error;
        return out.propTexels[0];
    };
    EXPECT_NEAR(plan(false), 32.0 * 32.0, 1.0);    // the T0 cap
    EXPECT_NEAR(plan(true), 128.0 * 128.0, 1.0);   // the texture's own detail
}

// The material's albedo alpha is drawn at runtime (u_alpha = the live material
// alpha). Baked into the page too, a surface that loads at alpha 0 and fades
// in (the CRT icons, anim_icon fade) would stay invisible. The page keeps the
// texture's alpha only.
TEST(BakeSurfaces, ThePageAlphaIsTheTexturesNotTheMaterials) {
    Room room = QuadRoom();
    std::vector<RoomProp> props(1);
    props[0].mesh.positions = {-0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, 0, 0.5f, -0.5f, 0, 0.5f};
    props[0].mesh.normals = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    props[0].mesh.indices = {0, 2, 1, 0, 3, 2};
    props[0].nodePath = "icon";
    room.AdoptProps(std::move(props));
    tools::SurfaceBakeInput in;
    BakedLight l;
    l.position[1] = 2.0f;
    l.energy = 2.0f;
    l.range = 10.0f;
    l.attenuation = 1.0f;
    in.lights = {l};
    in.sourceMaterial = {SurfaceMaterial{}};
    SurfaceMaterial faded;
    faded.alpha = 0.0f;   // albedo_color.a at load: the icon fades in later
    in.propMaterial = {faded};
    in.propAlpha = {1};
    in.propTexture = {-1};
    TexturePool pool;
    in.albedo = &pool;
    in.batchTexture = {-1};
    in.pageSize = 64;
    in.threads = 1;
    tools::SurfaceBakeResult out;
    ASSERT_TRUE(tools::BakeSurfaces(room, in, out)) << out.error;
    ASSERT_EQ(out.props.size(), 1u);
    const Mesh& m = out.props[0].mesh;
    float cx = 0, cy = 0;
    const std::size_t n = m.uv2.size() / 2;
    for (std::size_t v = 0; v < n; ++v) { cx += m.uv2[v * 2] / n; cy += m.uv2[v * 2 + 1] / n; }
    const tools::SurfacePage& pg = out.pages[static_cast<std::size_t>(out.props[0].page)];
    ASSERT_TRUE(pg.alpha);
    const int x = static_cast<int>(cx * pg.size), y = static_cast<int>(cy * pg.size);
    EXPECT_EQ(pg.rgba[(static_cast<std::size_t>(y) * pg.size + x) * 4 + 3], 255);
}

// Baked fog (assets::FogIsBaked) mixes the linear colour toward the fog before
// the grade. Fully fogged, a texel lit far over 1 reads the fog's graded
// colour, whatever light it had.
TEST(BakeSurfaces, BakedFogMixesTheLinearColourBeforeTheGrade) {
    TexturePool pool;
    tools::SurfaceBakeInput in = DarkQuadInput(pool);
    for (float& c : in.ambient) c = 5.0f;
    const uint8_t bright = CentreRed(in);
    in.fogDensity = 1000.0f;
    for (float& c : in.fog) c = 0.3f;
    const uint8_t fogged = CentreRed(in);
    EXPECT_LT(fogged, bright);
    const float white[3] = {1, 1, 1}, fog[3] = {0.3f, 0.3f, 0.3f};
    float expected[3];
    TexelColour(white, SurfaceMaterial{}, fog, in.grade, expected);
    EXPECT_NEAR(fogged / 255.0f, expected[0], 1.0f / 255.0f + 1e-3f);
}

// In a fog scene every vertex carries its surface's linear luminance in
// a_col.a (assets::EncodeFogLuminance): the runtime FOG program mixes so the
// result is the grade of Godot's linear fog (assets::FogWeight). Without a
// surface record a vertex reads white: its light alone.
TEST(BakeSurfaces, FogLuminanceIsEachVertexsLitSurface) {
    assets::Mesh m;
    m.positions = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 50.0f};   // near the lamp, far from it
    m.normals = {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    m.indices = {0, 1, 1};
    assets::BakedLight lamp;
    lamp.position[1] = 1.0f;
    lamp.energy = 400.0f;
    lamp.range = 10.0f;
    const float ambient[3] = {0.0f, 0.0f, 0.0f};
    tools::BakeFogLuminance({lamp}, core::Mat4{}, 1u, ambient, nullptr, m);
    ASSERT_EQ(m.colours.size(), 8u);
    const float nearX = assets::DecodeFogLuminance(m.colours[3]);
    const float farX = assets::DecodeFogLuminance(m.colours[7]);
    EXPECT_GT(nearX, 10.0f) << "a 400-energy lamp one unit away";
    EXPECT_LT(farX, 0.005f) << "out of the lamp's range";
}

// ...and with one, the albedo texel and material the surface bake shades its
// texels with: a mid-grey texel (sRGB 0.5, linear 0.214) reads 0.214 of white
// under the same light; emission adds to it.
TEST(BakeSurfaces, FogLuminanceTakesItsSurfacesAlbedo) {
    assets::Mesh m;
    m.positions = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    m.normals = {0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    m.indices = {0, 1, 2};
    assets::BakedLight lamp;
    lamp.position[1] = 1.0f;
    lamp.energy = 4.0f;
    lamp.range = 10.0f;
    const float ambient[3] = {0.0f, 0.0f, 0.0f};
    std::vector<tools::VertexSurface> surfaces(3);
    for (float& c : surfaces[1].texel) c = 0.5f;
    for (float& c : surfaces[2].texel) c = 0.0f;
    for (float& c : surfaces[2].material.emission) c = 0.25f;
    tools::BakeFogLuminance({lamp}, core::Mat4{}, 1u, ambient, &surfaces, m);
    const float white = assets::DecodeFogLuminance(m.colours[3]);
    const float grey = assets::DecodeFogLuminance(m.colours[7]);
    const float glow = assets::DecodeFogLuminance(m.colours[11]);
    EXPECT_NEAR(grey / white, assets::light::SrgbToLinear(0.5f), 0.02f);
    EXPECT_NEAR(glow, 0.25f, 0.02f) << "a black texel shows only its emission";
}

// The car is seen only in the ending, under EndingManager's grade (brightness
// 5.68, contrast 1.3, EndingManager.gd:120-121); baked at main's it posterizes
// to black and no runtime rescale brings that back. A prop with its own grade
// is baked at it, on a page of its own, and carries the grade to the runtime
// (which grades it relative to that).
TEST(BakeSurfaces, APropWithItsOwnGradeIsBakedAtItOnItsOwnPage) {
    Room room = QuadRoom();
    std::vector<RoomProp> props(2);
    for (int i = 0; i < 2; ++i) {
        props[i].mesh.positions = {-0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, 0, 0.5f, -0.5f, 0, 0.5f};
        props[i].mesh.normals = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
        props[i].mesh.indices = {0, 2, 1, 0, 3, 2};
        props[i].nodePath = i == 0 ? "table" : "car";
        props[i].visibleAtLoad = true;
        props[i].bakeWorld.m[13] = 0.5f;
    }
    room.AdoptProps(std::move(props));

    tools::SurfaceBakeInput in;
    BakedLight l;
    l.position[1] = 2.0f;
    l.energy = 0.4f;   // dim: the rest grade posterizes it low
    l.range = 10.0f;
    l.attenuation = 1.0f;
    in.lights = {l};
    in.sourceMaterial = {SurfaceMaterial{}};
    in.propMaterial = {SurfaceMaterial{}, SurfaceMaterial{}};
    in.propAlpha = {0, 0};
    in.propTexture = {-1, -1};
    light::Grade ending;
    ending.brightness = 5.68f;
    ending.contrast = 1.3f;
    in.propGrades = {ending};
    in.propGrade = {-1, 0};
    TexturePool pool;
    in.albedo = &pool;
    in.batchTexture = {-1};
    in.pageSize = 64;
    in.threads = 1;
    tools::SurfaceBakeResult out;
    ASSERT_TRUE(tools::BakeSurfaces(room, in, out)) << out.error;

    const RoomProp* table = nullptr;
    const RoomProp* car = nullptr;
    for (const RoomProp& p : out.props) (p.nodePath == "car" ? car : table) = &p;
    ASSERT_NE(table, nullptr);
    ASSERT_NE(car, nullptr);
    EXPECT_NE(table->page, car->page) << "a page is finished at one grade";
    EXPECT_FALSE(table->HasOwnGrade());
    EXPECT_TRUE(car->HasOwnGrade());
    EXPECT_FLOAT_EQ(car->bakeBrightness, 5.68f);
    EXPECT_FLOAT_EQ(car->bakeContrast, 1.3f);

    auto centre = [&](const RoomProp& p) {
        const tools::SurfacePage& pg = out.pages[static_cast<std::size_t>(p.page)];
        float cx = 0, cy = 0;
        const std::size_t n = p.mesh.uv2.size() / 2;
        for (std::size_t v = 0; v < n; ++v) { cx += p.mesh.uv2[v * 2] / n; cy += p.mesh.uv2[v * 2 + 1] / n; }
        const int x = static_cast<int>(cx * pg.size), y = static_cast<int>(cy * pg.size);
        return &pg.rgba[(static_cast<std::size_t>(y) * pg.size + x) * 4];
    };
    LightBvh none;
    none.Build({});
    const float pos[3] = {0, 0.5f, 0}, up[3] = {0, 1, 0}, white[3] = {1, 1, 1};
    float lightLinear[3], atRest[3], atEnding[3];
    StaticLight(in.lights, none, pos, up, kHomeSocket, 1.0f, lightLinear);
    TexelColour(white, SurfaceMaterial{}, lightLinear, light::Grade{}, atRest);
    TexelColour(white, SurfaceMaterial{}, lightLinear, ending, atEnding);
    for (int c = 0; c < 3; ++c) {
        EXPECT_NEAR(centre(*table)[c] / 255.0f, atRest[c], 1.0f / 7.0f + 1e-3f) << "channel " << c;
        EXPECT_NEAR(centre(*car)[c] / 255.0f, atEnding[c], 1.0f / 7.0f + 1e-3f) << "channel " << c;
    }
    EXPECT_GT(centre(*car)[0], centre(*table)[0]);
}

// ...and is planned from where it is seen: the car sits ~170 units from the
// table's camera (the coarsest tier) but under a metre from the ending's.
TEST(BakeSurfaces, APropWithItsOwnGradeTakesItsTierFromItsGradesEye) {
    Room room = QuadRoom();
    std::vector<RoomProp> props(1);
    props[0].mesh.positions = {-0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, 0, 0.5f, -0.5f, 0, 0.5f};
    props[0].mesh.normals = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    props[0].mesh.indices = {0, 2, 1, 0, 3, 2};
    props[0].nodePath = "car";
    props[0].visibleAtLoad = true;
    props[0].bakeWorld.m[12] = 150.0f;   // far from the table's eye
    room.AdoptProps(std::move(props));
    auto plan = [&](bool ownEye) {
        tools::SurfaceBakeInput in;
        in.sourceMaterial = {SurfaceMaterial{}};
        in.propMaterial = {SurfaceMaterial{}};
        in.propAlpha = {0};
        in.propTexture = {-1};
        in.propGrades = {light::Grade{}};
        in.propGrade = {0};
        if (ownEye) in.propGradeEyes = {{150.0f, 1.0f, 0.0f}};
        TexturePool pool;
        in.albedo = &pool;
        in.batchTexture = {-1};
        in.pageSize = 64;
        in.threads = 1;
        tools::SurfaceBakeResult out;
        EXPECT_TRUE(tools::BakeSurfaces(room, in, out)) << out.error;
        return out;
    };
    const tools::SurfaceBakeResult seenFromTable = plan(false);
    const tools::SurfaceBakeResult seenUpClose = plan(true);
    EXPECT_GT(seenFromTable.propTexels[2], 0.0);
    EXPECT_EQ(seenUpClose.propTexels[2], 0.0);
    EXPECT_GT(seenUpClose.propTexels[0], 0.0) << "the top tier, from the ending's camera";
}

// The menu's shell waterfalls slide five units down through two lamps on a
// loop. Baked at one pose (halfway) they read half as bright as Godot's, on
// average and at the peaks. A prop that passes through its light is lit with
// the mean of its poses' light: Godot's average over the fall.
TEST(BakeSurfaces, APropWithMeanPosesIsLitWithTheirAverageLight) {
    Room room = QuadRoom();
    std::vector<RoomProp> props(1);
    props[0].mesh.positions = {-0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, 0, 0.5f, -0.5f, 0, 0.5f};
    props[0].mesh.normals = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    props[0].mesh.indices = {0, 2, 1, 0, 3, 2};
    props[0].nodePath = "shell";
    props[0].visibleAtLoad = true;
    props[0].bakeWorld.m[13] = 1.0f;
    core::Mat4 near = props[0].bakeWorld;    // under the lamp
    core::Mat4 far = props[0].bakeWorld;     // out of its range
    far.m[12] = 50.0f;
    props[0].meanPoses = {near, far};
    room.AdoptProps(std::move(props));

    tools::SurfaceBakeInput in;
    BakedLight l;
    l.position[1] = 3.0f;
    l.energy = 0.6f;
    l.range = 10.0f;
    l.attenuation = 1.0f;
    in.lights = {l};
    in.sourceMaterial = {SurfaceMaterial{}};
    in.propMaterial = {SurfaceMaterial{}};
    in.propAlpha = {0};
    in.propTexture = {-1};
    TexturePool pool;
    in.albedo = &pool;
    in.batchTexture = {-1};
    in.pageSize = 64;
    in.threads = 1;
    tools::SurfaceBakeResult out;
    ASSERT_TRUE(tools::BakeSurfaces(room, in, out)) << out.error;
    ASSERT_EQ(out.props.size(), 1u);
    const Mesh& m = out.props[0].mesh;
    float cx = 0, cy = 0;
    const std::size_t n = m.uv2.size() / 2;
    for (std::size_t v = 0; v < n; ++v) { cx += m.uv2[v * 2] / n; cy += m.uv2[v * 2 + 1] / n; }
    const tools::SurfacePage& pg = out.pages[static_cast<std::size_t>(out.props[0].page)];
    const int x = static_cast<int>(cx * pg.size), y = static_cast<int>(cy * pg.size);
    const float texel = pg.rgba[(static_cast<std::size_t>(y) * pg.size + x) * 4] / 255.0f;

    LightBvh none;
    none.Build({});
    const float pos[3] = {0, 1, 0}, up[3] = {0, 1, 0}, white[3] = {1, 1, 1};
    float lit[3], half[3], fullColour[3], halfColour[3];
    StaticLight(in.lights, none, pos, up, kHomeSocket, 1.0f, lit);
    for (int c = 0; c < 3; ++c) half[c] = lit[c] * 0.5f;
    TexelColour(white, SurfaceMaterial{}, lit, light::Grade{}, fullColour);
    TexelColour(white, SurfaceMaterial{}, half, light::Grade{}, halfColour);
    ASSERT_GT(fullColour[0], halfColour[0] + 1.0f / 7.0f) << "the test needs the two to differ";
    EXPECT_NEAR(texel, halfColour[0], 1.0f / 7.0f + 1e-3f) << "the mean of lit and dark, not the brighter";
}

// A prop lit with the mean of its poses (the menu's falling shells) carries
// the luminance of that mean light, as its texels do.
TEST(BakeSurfaces, FogLuminanceOverPosesIsTheirMeanLight) {
    assets::Mesh m;
    m.positions = {0.0f, 0.0f, 0.0f};
    m.normals = {0.0f, 1.0f, 0.0f};
    m.indices = {0, 0, 0};
    assets::BakedLight lamp;
    lamp.position[1] = 1.0f;
    lamp.energy = 400.0f;
    lamp.range = 10.0f;
    const float ambient[3] = {0.0f, 0.0f, 0.0f};
    core::Mat4 near, far;
    far.m[12] = 50.0f;
    assets::Mesh a = m, b = m, both = m;
    tools::BakeFogLuminance({lamp}, near, 1u, ambient, nullptr, a);
    tools::BakeFogLuminance({lamp}, far, 1u, ambient, nullptr, b);
    tools::BakeFogLuminance({lamp}, std::vector<core::Mat4>{near, far}, 1u, ambient, nullptr, both);
    const float nearX = assets::DecodeFogLuminance(a.colours[3]);
    const float meanX = assets::DecodeFogLuminance(both.colours[3]);
    EXPECT_LT(meanX, nearX);
    EXPECT_GT(meanX, assets::DecodeFogLuminance(b.colours[3]));
    EXPECT_NEAR(meanX, nearX * 0.5f, nearX * 0.1f) << "the mean light (the encoding is coarse)";
}

namespace {

// A 100 x 100 quad on y = 0, two triangles, UVs (and page UVs) running 0..1.
assets::Mesh BigQuad(float half) {
    assets::Mesh m;
    m.positions = {-half, 0, -half, half, 0, -half, half, 0, half, -half, 0, half};
    m.normals = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    m.uvs = {0, 0, 1, 0, 1, 1, 0, 1};
    m.uv2 = m.uvs;
    m.indices = {0, 2, 1, 0, 3, 2};
    return m;
}

float EdgeLength(const assets::Mesh& m, uint16_t a, uint16_t b, float scale) {
    float d = 0.0f;
    for (int k = 0; k < 3; ++k) {
        const float e = (m.positions[a * 3u + k] - m.positions[b * 3u + k]) * scale;
        d += e * e;
    }
    return std::sqrt(d);
}

}  // namespace

// Fog is per vertex, so a triangle must not span much of it. Heaven's floor is
// two triangles 150 units across, and every pixel of it would take the fog of
// its far corners. TessellateForFog splits long edges at shared midpoints
// until none is longer than the limit in the world: crack-free (the boundary
// stays 400 long, every inner edge has two triangles), attributes
// interpolated.
TEST(BakeSurfaces, TessellateForFogBringsEveryEdgeUnderTheLimit) {
    assets::Mesh m = BigQuad(50.0f);
    std::vector<tools::VertexSurface> surfaces(4);
    surfaces[2].texel[0] = 0.0f;
    const tools::FogTessellation r = tools::TessellateForFog(m, &surfaces, core::Mat4{}, 10.0f);
    const int added = r.added;
    EXPECT_GT(added, 0);
    EXPECT_FALSE(r.capped);
    EXPECT_LE(r.longestEdge, 10.0f + 1e-3f);
    const int n = m.VertexCount();
    ASSERT_EQ(surfaces.size(), static_cast<std::size_t>(n));
    ASSERT_EQ(m.normals.size(), static_cast<std::size_t>(n) * 3);
    ASSERT_EQ(m.uvs.size(), static_cast<std::size_t>(n) * 2);
    ASSERT_EQ(m.uv2.size(), static_cast<std::size_t>(n) * 2);
    EXPECT_EQ(m.TriangleCount(), 2 + added);
    std::map<std::pair<uint16_t, uint16_t>, int> uses;
    double area = 0.0;
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const uint16_t v[3] = {m.indices[t], m.indices[t + 1], m.indices[t + 2]};
        for (int e = 0; e < 3; ++e) {
            const uint16_t a = v[e], b = v[(e + 1) % 3];
            EXPECT_LE(EdgeLength(m, a, b, 1.0f), 10.0f + 1e-3f);
            ++uses[{std::min(a, b), std::max(a, b)}];
        }
        const float* p0 = &m.positions[v[0] * 3u];
        const float* p1 = &m.positions[v[1] * 3u];
        const float* p2 = &m.positions[v[2] * 3u];
        area += 0.5 * std::fabs((p1[0] - p0[0]) * (p2[2] - p0[2]) - (p2[0] - p0[0]) * (p1[2] - p0[2]));
    }
    EXPECT_NEAR(area, 10000.0, 1e-2);
    float boundary = 0.0f;
    for (const auto& [edge, count] : uses) {
        EXPECT_LE(count, 2);
        if (count == 1) boundary += EdgeLength(m, edge.first, edge.second, 1.0f);
    }
    EXPECT_NEAR(boundary, 400.0f, 1e-2f) << "a T-junction would leave an inner edge used once";
    for (int v = 0; v < n; ++v) {   // UVs and page UVs stay affine across the quad
        EXPECT_NEAR(m.uvs[v * 2u], (m.positions[v * 3u] + 50.0f) / 100.0f, 1e-4f);
        EXPECT_NEAR(m.uv2[v * 2u + 1], (m.positions[v * 3u + 2] + 50.0f) / 100.0f, 1e-4f);
    }
}

TEST(BakeSurfaces, TessellateForFogLeavesShortEdgesAlone) {
    assets::Mesh m = BigQuad(2.0f);
    const assets::Mesh before = m;
    EXPECT_EQ(tools::TessellateForFog(m, nullptr, core::Mat4{}, 10.0f).added, 0);
    EXPECT_EQ(m.indices, before.indices);
    EXPECT_EQ(m.positions, before.positions);
}

// The limit is in the world: heaven's floor is a unit plane scaled by 75.
TEST(BakeSurfaces, TessellateForFogMeasuresEdgesInTheWorld) {
    assets::Mesh m = BigQuad(0.5f);
    core::Mat4 world;
    world.m[0] = world.m[5] = world.m[10] = 75.0f;
    EXPECT_GT(tools::TessellateForFog(m, nullptr, world, 10.0f).added, 0);
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        for (int e = 0; e < 3; ++e) {
            EXPECT_LE(EdgeLength(m, m.indices[t + e], m.indices[t + (e + 1) % 3], 75.0f), 10.0f + 1e-3f);
        }
    }
}

// A mesh never outgrows its 16-bit indices: refinement stops at the cap, and
// says so, since a capped mesh keeps long edges (a full page batch could keep
// heaven's 150-unit floor).
TEST(BakeSurfaces, TessellateForFogStopsAtSixteenBitIndices) {
    assets::Mesh m = BigQuad(50.0f);
    const tools::FogTessellation r = tools::TessellateForFog(m, nullptr, core::Mat4{}, 0.01f);
    EXPECT_TRUE(r.capped);
    EXPECT_GT(r.longestEdge, 0.01f);
    EXPECT_LE(m.VertexCount(), 65535);
    for (uint16_t i : m.indices) EXPECT_LT(static_cast<int>(i), m.VertexCount());
}

// The uniform quad only ever splits all three edges. Here a triangle has two
// long edges (split 2) and a short neighbour sharing its third (split 0), and
// every piece keeps the winding (signed area), the area and the outline.
TEST(BakeSurfaces, TessellateForFogKeepsTheWindingWhenSplitsMix) {
    assets::Mesh m;
    m.positions = {0, 0, 0, 30, 0, 0, 30, 0, 5, 35, 0, 2.5f};   // A B C D
    m.normals = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    m.indices = {0, 1, 2, 1, 3, 2};                            // ABC, BDC: both turn the same way
    const auto signedArea = [&m](std::size_t t) {
        const float* a = &m.positions[m.indices[t] * 3u];
        const float* b = &m.positions[m.indices[t + 1] * 3u];
        const float* c = &m.positions[m.indices[t + 2] * 3u];
        return 0.5f * ((b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]));
    };
    double before = 0.0;
    for (std::size_t t = 0; t < m.indices.size(); t += 3) before += signedArea(t);
    ASSERT_NE(before, 0.0);
    const float turn = before > 0.0 ? 1.0f : -1.0f;   // the input's winding
    const tools::FogTessellation r = tools::TessellateForFog(m, nullptr, core::Mat4{}, 20.0f);
    EXPECT_GT(r.added, 0);
    double after = 0.0;
    std::map<std::pair<uint16_t, uint16_t>, int> uses;
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        EXPECT_GT(signedArea(t) * turn, 0.0f) << "triangle " << t / 3 << " flipped";
        after += signedArea(t);
        for (int e = 0; e < 3; ++e) {
            const uint16_t a = m.indices[t + e], b = m.indices[t + (e + 1) % 3];
            EXPECT_LE(EdgeLength(m, a, b, 1.0f), 20.0f + 1e-3f);
            ++uses[{std::min(a, b), std::max(a, b)}];
        }
    }
    EXPECT_NEAR(after, before, 1e-3);
    float outline = 0.0f;
    for (const auto& [edge, count] : uses) {
        EXPECT_LE(count, 2);
        if (count == 1) outline += EdgeLength(m, edge.first, edge.second, 1.0f);
    }
    const float ab = 30.0f, bd = std::sqrt(25.0f + 6.25f), dc = bd, ca = std::sqrt(900.0f + 25.0f);
    EXPECT_NEAR(outline, ab + bd + dc + ca, 1e-3f) << "a T-junction would leave an inner edge used once";
}

// A page's batch merges faces of many sources: each vertex records the material
// of the face it was cut from (Batches()[...].triSource) and its texture's texel.
TEST(BakeSurfaces, ABatchRecordsEachFacesOwnMaterialAndTexel) {
    Room room;
    {
        RoomBatch b;
        b.mesh.positions = {-0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, 0, 0.5f, -0.5f, 0, 0.5f};
        b.mesh.normals = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
        b.mesh.uvs = {0, 0, 1, 0, 1, 1, 0, 1};
        b.mesh.indices = {0, 2, 1, 0, 3, 2};
        b.triSource = {0, 1};
        std::vector<RoomBatch> batches;
        batches.push_back(std::move(b));
        room.AdoptBatches(std::move(batches));
        room.AdoptSources({{"left", 0}, {"right", 0}});
    }
    tools::SurfaceBakeInput in;
    SurfaceMaterial left, right;
    left.albedo[0] = 0.2f;
    right.albedo[0] = 0.9f;
    in.sourceMaterial = {left, right};
    TexturePool pool;
    const uint8_t rgba[16] = {128, 64, 32, 255, 128, 64, 32, 255, 128, 64, 32, 255, 128, 64, 32, 255};
    const int tex = pool.Add("quad", 0, rgba, 2, 2);
    ASSERT_GE(tex, 0);
    in.albedo = &pool;
    in.batchTexture = {tex};
    in.pageSize = 64;
    in.threads = 1;
    tools::SurfaceBakeResult out;
    ASSERT_TRUE(tools::BakeSurfaces(room, in, out)) << out.error;
    ASSERT_EQ(out.batchSurfaces.size(), out.batches.size());
    bool sawLeftOnly = false, sawRightOnly = false;
    for (std::size_t b = 0; b < out.batches.size(); ++b) {
        const Mesh& mesh = out.batches[b].mesh;
        ASSERT_EQ(out.batchSurfaces[b].size(), static_cast<std::size_t>(mesh.VertexCount()));
        for (int v = 0; v < mesh.VertexCount(); ++v) {
            const tools::VertexSurface& s = out.batchSurfaces[b][static_cast<std::size_t>(v)];
            EXPECT_NEAR(s.texel[0], 128.0f / 255.0f, 1e-3f);
            EXPECT_NEAR(s.texel[2], 32.0f / 255.0f, 1e-3f);
            const float x = mesh.positions[v * 3u], z = mesh.positions[v * 3u + 2];
            if (x > 0.4f && z < -0.4f) {   // source vertex 1: face 0's alone
                EXPECT_FLOAT_EQ(s.material.albedo[0], 0.2f);
                sawLeftOnly = true;
            }
            if (x < -0.4f && z > 0.4f) {   // source vertex 3: face 1's alone
                EXPECT_FLOAT_EQ(s.material.albedo[0], 0.9f);
                sawRightOnly = true;
            }
        }
    }
    EXPECT_TRUE(sawLeftOnly);
    EXPECT_TRUE(sawRightOnly);
}

// The surface bake records, for every vertex it writes, the albedo texel and
// material it was cut from: the fog luminance (after the bake) needs them, and
// a batch's page merges many materials.
TEST(BakeSurfaces, TheSurfaceBakeRecordsEachOutputVertexsSurface) {
    Room room = QuadRoom();
    std::vector<RoomProp> props(1);
    props[0].mesh.positions = {-0.5f, 0, -0.5f, 0.5f, 0, -0.5f, 0.5f, 0, 0.5f, -0.5f, 0, 0.5f};
    props[0].mesh.normals = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    props[0].mesh.indices = {0, 2, 1, 0, 3, 2};
    props[0].nodePath = "shell";
    props[0].visibleAtLoad = true;
    room.AdoptProps(std::move(props));

    tools::SurfaceBakeInput in;
    in.sourceMaterial = {SurfaceMaterial{}};
    SurfaceMaterial brass;
    brass.albedo[0] = 0.8f;
    brass.emission[1] = 0.1f;
    in.propMaterial = {brass};
    in.propAlpha = {0};
    in.propTexture = {-1};
    TexturePool pool;
    in.albedo = &pool;
    in.batchTexture = {-1};
    in.pageSize = 64;
    in.threads = 1;
    tools::SurfaceBakeResult out;
    ASSERT_TRUE(tools::BakeSurfaces(room, in, out)) << out.error;
    ASSERT_EQ(out.propSurfaces.size(), out.props.size());
    ASSERT_EQ(out.batchSurfaces.size(), out.batches.size());
    for (std::size_t p = 0; p < out.props.size(); ++p) {
        ASSERT_EQ(out.propSurfaces[p].size(), static_cast<std::size_t>(out.props[p].mesh.VertexCount()));
        for (const tools::VertexSurface& v : out.propSurfaces[p]) {
            EXPECT_FLOAT_EQ(v.material.albedo[0], 0.8f);
            EXPECT_FLOAT_EQ(v.material.emission[1], 0.1f);
            EXPECT_FLOAT_EQ(v.texel[0], 1.0f) << "untextured: white";
        }
    }
    for (std::size_t b = 0; b < out.batches.size(); ++b) {
        EXPECT_EQ(out.batchSurfaces[b].size(), static_cast<std::size_t>(out.batches[b].mesh.VertexCount()));
    }
}
