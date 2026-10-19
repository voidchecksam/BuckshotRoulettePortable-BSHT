// A sequence belongs to the object whose method started it, as a GDScript
// function state belongs to its instance. Freeing that object drops the
// sequence: it never resumes against freed state. Sequences are free functions
// taking parameters (see test_seq.cpp).
#include <gtest/gtest.h>

#include "seq/delay.h"
#include "seq/scheduler.h"

#include <vector>

using namespace seq;

namespace {

Task MarkAfter(double seconds, int* mark, int value) {
    co_await Delay(seconds);
    *mark = value;
}

Task Child(double seconds, int* mark) {
    co_await Delay(seconds);
    *mark += 1;
}

Task ParentAwaitingChild(int* mark) {
    co_await Child(0.1, mark);
    co_await Delay(0.1);
    *mark += 10;
}

// Cancels another object's sequences from inside a running sequence, in the
// same tick the other object's timer also expires.
Task CancelOther(Scheduler* s, const void* other) {
    co_await Delay(0.1);
    s->CancelOwner(other);
}

TEST(SeqOwner, CancellingAnOwnerDropsItsPendingSequence) {
    Scheduler s;
    int a = 0, b = 0;
    int ownerA = 0, ownerB = 0;
    s.Spawn(MarkAfter(0.1, &a, 1), &ownerA);
    s.Spawn(MarkAfter(0.1, &b, 2), &ownerB);
    s.CancelOwner(&ownerA);
    for (int i = 0; i < 10; ++i) s.Tick(1.0 / 60.0);
    EXPECT_EQ(a, 0) << "the cancelled owner's sequence resumed";
    EXPECT_EQ(b, 2) << "another owner's sequence must still run";
    EXPECT_EQ(s.LiveTaskCount(), 0u);
}

TEST(SeqOwner, TheOwnerFollowsTheSequenceThroughAnAwaitedChild) {
    Scheduler s;
    int mark = 0;
    int owner = 0;
    s.Spawn(ParentAwaitingChild(&mark), &owner);
    for (int i = 0; i < 8; ++i) s.Tick(1.0 / 60.0);   // the child's delay has fired
    ASSERT_EQ(mark, 1);
    s.CancelOwner(&owner);                              // now pending in the parent's own delay
    for (int i = 0; i < 20; ++i) s.Tick(1.0 / 60.0);
    EXPECT_EQ(mark, 1);
    EXPECT_EQ(s.LiveTaskCount(), 0u);
}

TEST(SeqOwner, ACancelDuringATickSkipsThatOwnersExpiredTimers) {
    Scheduler s;
    int victim = 0;
    int ownerVictim = 0, ownerKiller = 0;
    s.Spawn(CancelOther(&s, &ownerVictim), &ownerKiller);   // spawned first: most overdue ties resolve by order
    s.Spawn(MarkAfter(0.1, &victim, 7), &ownerVictim);
    for (int i = 0; i < 10; ++i) s.Tick(1.0 / 60.0);
    EXPECT_EQ(victim, 0);
    EXPECT_EQ(s.LiveTaskCount(), 0u);
}

// The freed object's own method frees it (queue_free inside its coroutine,
// then another await): the new wait must not outlive the reaped frame.
Task CancelSelfThenWait(Scheduler* s, const void* self, int* mark) {
    co_await Delay(0.05);
    s->CancelOwner(self);
    co_await Delay(0.05);
    *mark = 99;
}

TEST(SeqOwner, ASequenceThatCancelsItsOwnOwnerNeverResumesAgain) {
    Scheduler s;
    int mark = 0;
    int owner = 0;
    s.Spawn(CancelSelfThenWait(&s, &owner, &mark), &owner);
    for (int i = 0; i < 20; ++i) s.Tick(1.0 / 60.0);
    EXPECT_EQ(mark, 0);
    EXPECT_EQ(s.PendingCount(), 0u);
    EXPECT_EQ(s.LiveTaskCount(), 0u);
}

TEST(SeqOwner, AnUnownedSequenceIsNeverCancelledByOwner) {
    Scheduler s;
    int mark = 0;
    s.Spawn(MarkAfter(0.1, &mark, 3));
    s.CancelOwner(nullptr);
    for (int i = 0; i < 10; ++i) s.Tick(1.0 / 60.0);
    EXPECT_EQ(mark, 3);
}

}  // namespace

#include "godot/node.h"
#include "godot/scene_tree.h"

#include <memory>

namespace {

seq::Task TreeMarkAfter(godot::SceneTree* tree, double seconds, int* mark) {
    co_await tree->create_timer(seconds);
    *mark = 1;
}

// Godot drops a function state whose instance is freed: queue_free on a node
// (or on its parent) means its awaiting methods never run again.
TEST(SceneTreeOwner, FreeingANodeOrItsParentDropsItsSequences) {
    godot::SceneTree tree;
    // A fresh tree has no root: the first node added becomes it.
    godot::Node* root = tree.add_node(std::make_unique<godot::Node>("root"), nullptr);
    godot::Node* parent = tree.add_node(std::make_unique<godot::Node>("parent"), root);
    godot::Node* child = tree.add_node(std::make_unique<godot::Node>("child"), parent);
    godot::Node* other = tree.add_node(std::make_unique<godot::Node>("other"), root);
    int childMark = 0, otherMark = 0;
    tree.spawn(TreeMarkAfter(&tree, 0.1, &childMark), child);
    tree.spawn(TreeMarkAfter(&tree, 0.1, &otherMark), other);
    parent->queue_free();
    for (int i = 0; i < 20; ++i) tree.process_frame(1.0 / 60.0);
    EXPECT_EQ(childMark, 0);
    EXPECT_EQ(otherMark, 1);
}

}  // namespace
