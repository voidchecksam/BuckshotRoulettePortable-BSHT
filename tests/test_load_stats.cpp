// core::LoadStats: what platform::Audio's stream loader did, for the Vita
// report. The loader's printf never reaches bsht_report.txt, so without this a
// device run cannot confirm that stream starts left the frame thread.
#include "core/load_stats.h"

#include <gtest/gtest.h>

#include <string>

TEST(LoadStats, AFreshRecordSaysNothingLoaded) {
    core::LoadStats s;
    EXPECT_EQ(s.Format(), "streams: 0 loaded, 0 dropped, 0 failed");
}

TEST(LoadStats, LoadsKeepTheLastAndThePeakTime) {
    core::LoadStats s;
    s.Loaded(12.4);
    s.Loaded(840.0);
    s.Loaded(30.0);
    EXPECT_EQ(s.Format(), "streams: 3 loaded, 0 dropped, 0 failed, last 30 ms, peak 840 ms off the frame");
}

TEST(LoadStats, DropsAndFailuresAreCountedAndTheLastFailureNamed) {
    core::LoadStats s;
    s.Loaded(5.0);
    s.Dropped();
    s.Failed("app0:data/a.ogg");
    s.Failed("app0:data/b.ogg");
    EXPECT_EQ(s.Format(),
              "streams: 1 loaded, 1 dropped, 2 failed, last 5 ms, peak 5 ms off the frame, last failure "
              "app0:data/b.ogg");
}
