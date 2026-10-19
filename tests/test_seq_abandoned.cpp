// The concurrency model: an un-awaited GDScript call becomes Scheduler::Spawn
// and must outlive the caller that spawned it.
//
// ShotgunShooting.Shoot relies on this three times: MainSlowDownRoutine,
// ShootingDealerEjection and FinalizeShooting are all called without await,
// and FinalizeShooting is what eventually ends the turn. If an abandoned task
// died with its caller, the turn would never end.
//
// Every sequence here is a free function taking parameters by value or by
// pointer, never a lambda: captures live in the closure object, which dies at
// the end of the full expression and leaves the coroutine frame dangling (see
// the top of tests/test_seq.cpp).
#include "seq/delay.h"
#include "seq/scheduler.h"
#include "seq/task.h"

#include <gtest/gtest.h>

namespace {

seq::Task Abandoned(int* reached) {
    co_await seq::Delay(1.0);
    *reached = 1;
}

seq::Task CallerThatSpawnsAndReturns(seq::Scheduler* sched, int* reached, int* callerDone) {
    sched->Spawn(Abandoned(reached));
    // The caller returns immediately, as ShotgunShooting.Shoot does after
    // spawning ShootingDealerEjection.
    *callerDone = 1;
    co_return;
}

TEST(SeqAbandoned, SpawnedTaskOutlivesTheCallerThatSpawnedIt) {
    seq::Scheduler sched;
    int reached = 0;
    int callerDone = 0;

    sched.Spawn(CallerThatSpawnsAndReturns(&sched, &reached, &callerDone));
    EXPECT_EQ(callerDone, 1);   // the caller is already finished
    EXPECT_EQ(reached, 0);      // the abandoned task has not run yet

    for (int i = 0; i < 120; ++i) sched.Tick(1.0 / 60.0);

    EXPECT_EQ(reached, 1);      // ... and it still completed
}

}  // namespace
