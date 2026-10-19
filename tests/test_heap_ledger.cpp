// core/heap_ledger: live and peak heap bytes, fed by the executables' operator
// new/delete (platform/alloc_count.cpp). The Vita's newlib heap is a fixed
// 128 MB block that no other report line shows, and running out of it during a
// scene change is a std::bad_alloc.
#include "core/heap_ledger.h"

#include <gtest/gtest.h>

namespace {

TEST(HeapLedger, LiveFollowsAllocationsAndFrees) {
    core::HeapLedger ledger;

    ledger.OnAlloc(100);
    ledger.OnAlloc(50);
    ledger.OnFree(100);

    EXPECT_EQ(ledger.Live(), 50u);
}

TEST(HeapLedger, PeakIsTheHighestLiveSinceTheLastReset) {
    core::HeapLedger ledger;
    ledger.OnAlloc(100);
    ledger.OnAlloc(50);
    ledger.OnFree(100);
    EXPECT_EQ(ledger.Peak(), 150u);

    ledger.ResetPeak();
    EXPECT_EQ(ledger.Peak(), 50u);

    ledger.OnAlloc(30);
    EXPECT_EQ(ledger.Peak(), 80u);
}

}  // namespace

// The size header costs bytes in the same heap, so the ledger counts live
// blocks and states that cost.
namespace {

TEST(HeapLedger, CountsLiveBlocksAndTheirHeaderBytes) {
    core::HeapLedger ledger(/*headerBytes=*/8);
    ledger.OnAlloc(100);
    ledger.OnAlloc(50);
    ledger.OnAlloc(10);
    ledger.OnFree(50);

    EXPECT_EQ(ledger.Blocks(), 2u);
    EXPECT_EQ(ledger.HeaderBytes(), 16u);
}

}  // namespace
