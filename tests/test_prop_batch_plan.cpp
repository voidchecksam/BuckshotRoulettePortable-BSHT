// Prop batching, the headless half: which props share a chunk, and the index
// list a chunk draws for the members that joined it this frame.
#include "render/prop_batch_plan.h"

#include "assets/room.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

using render::BatchProp;
using render::BuildChunks;
using render::PropChunk;

TEST(PropBatchPlan, PropsOnOnePageShareAChunkInUploadOrder) {
    const std::vector<BatchProp> props{{5, 100, true}, {5, 200, true}, {5, 50, true}};
    const std::vector<PropChunk> chunks = BuildChunks(props, 24, 65535);
    ASSERT_EQ(chunks.size(), 1u);
    EXPECT_EQ(chunks[0].page, 5);
    EXPECT_EQ(chunks[0].props, (std::vector<int>{0, 1, 2}));
    EXPECT_EQ(chunks[0].vertices, 350u);
    EXPECT_EQ(chunks[0].firstVertex, (std::vector<uint32_t>{0, 100, 300}));
}

TEST(PropBatchPlan, PagesNeverShareAChunk) {
    const std::vector<BatchProp> props{{5, 10, true}, {7, 10, true}, {5, 10, true}};
    const std::vector<PropChunk> chunks = BuildChunks(props, 24, 65535);
    ASSERT_EQ(chunks.size(), 2u);
    EXPECT_EQ(chunks[0].props, (std::vector<int>{0, 2}));
    EXPECT_EQ(chunks[1].props, (std::vector<int>{1}));
}

TEST(PropBatchPlan, AChunkHoldsAtMostItsSlotsAndItsVertexLimit) {
    std::vector<BatchProp> props(5, BatchProp{1, 10, true});
    EXPECT_EQ(BuildChunks(props, 2, 65535).size(), 3u) << "5 props, 2 slots a chunk";
    const std::vector<BatchProp> big{{1, 40000, true}, {1, 30000, true}};
    EXPECT_EQ(BuildChunks(big, 24, 65535).size(), 2u) << "16-bit indices: 70000 vertices do not fit";
}

TEST(PropBatchPlan, PropsThatCannotBatchStayOut) {
    const std::vector<BatchProp> props{{5, 10, false}, {5, 10, true}, {-1, 10, true}};
    const std::vector<PropChunk> chunks = BuildChunks(props, 24, 65535);
    ASSERT_EQ(chunks.size(), 1u) << "unbatchable, and untextured (page -1), props draw alone";
    EXPECT_EQ(chunks[0].props, (std::vector<int>{1}));
}

// A chunk's index list is the joined members' ranges, in slot order.
TEST(PropBatchPlan, AChunkDrawsOnlyItsJoinedMembers) {
    const std::vector<uint16_t> all{0, 1, 2, 3, 4, 5, 6, 7, 8};   // three props, 3 indices each
    const std::vector<render::IndexRange> ranges{{0, 3}, {3, 3}, {6, 3}};
    std::vector<uint16_t> out;
    render::BuildIndexList(all, ranges, 0b101u, out);
    EXPECT_EQ(out, (std::vector<uint16_t>{0, 1, 2, 6, 7, 8}));
    render::BuildIndexList(all, ranges, 0u, out);
    EXPECT_TRUE(out.empty());
}

}  // namespace

// Which props a chunk may take. The palette carries one offset per program, so
// a prop baked at its own grade (the car and the highway, at the ending's) must
// draw alone: the fence, batched, would take the scene's grade over its own.
TEST(PropBatchPlan, OnlyPlainStillPropsCanBatch) {
    assets::RoomProp plain;
    plain.nodePath = "tabletop parent/shell";
    plain.mesh.positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    plain.mesh.indices = {0, 1, 2};
    EXPECT_TRUE(render::PropCanBatch(plain));

    assets::RoomProp ownGrade = plain;
    ownGrade.bakeBrightness = 5.68f;
    ownGrade.bakeContrast = 1.3f;
    EXPECT_FALSE(render::PropCanBatch(ownGrade));

    assets::RoomProp template_ = plain;
    template_.templateKey = "instances/shell.tscn";
    EXPECT_FALSE(render::PropCanBatch(template_));

    assets::RoomProp car = plain;
    car.nodePath = "player vehicle parent/vehicle";
    EXPECT_FALSE(render::PropCanBatch(car)) << "the moving light's scope draws with the MOVE program";

    assets::RoomProp skinned = plain;
    skinned.mesh.joints = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    skinned.mesh.weights = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    EXPECT_FALSE(render::PropCanBatch(skinned));

    assets::RoomProp empty = plain;
    empty.mesh.indices.clear();
    EXPECT_FALSE(render::PropCanBatch(empty));

    assets::RoomProp swaps = plain;
    swaps.variants.emplace_back();
    EXPECT_FALSE(render::PropCanBatch(swaps)) << "a shell's colour may skip the scene's grade";
}
