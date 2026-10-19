// assets::Room::ReleaseGeometry: once a room is on the GPU, its CPU vertices
// go. The table's geometry is tens of MB of heap; the renderer copies what it
// still needs (a skinned prop's bind mesh), and the Vita's newlib heap is a
// fixed 128 MB.
#include "assets/room.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace {

assets::Mesh Triangle(const char* name, int material) {
    assets::Mesh m;
    m.name = name;
    m.positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    m.normals = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    m.uvs = {0, 0, 1, 0, 0, 1};
    m.colours = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
    m.indices = {0, 1, 2};
    m.materialIndex = material;
    return m;
}

TEST(RoomRelease, VerticesGoButEveryPropAndBatchStays) {
    assets::Room room;
    std::vector<assets::RoomProp> props(1);
    props[0].mesh = Triangle("gun", 3);
    room.AdoptProps(std::move(props));
    std::vector<assets::RoomBatch> batches(1);
    batches[0].mesh = Triangle("felt", 1);
    room.AdoptBatches(std::move(batches));

    room.ReleaseGeometry();

    ASSERT_EQ(room.Props().size(), 1u);
    ASSERT_EQ(room.Batches().size(), 1u);
    const assets::Mesh& gun = room.Props()[0].mesh;
    EXPECT_EQ(gun.positions.capacity(), 0u);
    EXPECT_EQ(gun.normals.capacity(), 0u);
    EXPECT_EQ(gun.uvs.capacity(), 0u);
    EXPECT_EQ(gun.colours.capacity(), 0u);
    EXPECT_EQ(gun.indices.capacity(), 0u);
    EXPECT_EQ(gun.name, "gun");
    EXPECT_EQ(gun.materialIndex, 3);
    EXPECT_EQ(room.Batches()[0].mesh.positions.capacity(), 0u);
    EXPECT_EQ(room.Batches()[0].mesh.materialIndex, 1);
}

}  // namespace
