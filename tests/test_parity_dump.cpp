// The parity dump's sample comparison.
//
// The interaction samples record what the scripts decided, and most of that is
// a name, not a number: which control has focus, what the ray hit, the active
// branch's alias, the animation playing, the item description on screen. A
// sample therefore carries a string beside its numbers, compared exactly:
// "godot focused the gun bracket, the port focused nothing" is the useful
// failure, and 1e-4 on a number cannot say it.
#include "parity/parity_dump.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

parity::Dump WithSample(const std::string& id, std::vector<double> v, std::string s) {
    parity::Dump d;
    d.scenario = "grab_shotgun_b1r1";
    parity::Sample sample;
    sample.id = id;
    sample.v = std::move(v);
    sample.s = std::move(s);
    d.samples.push_back(std::move(sample));
    return d;
}

}  // namespace

TEST(ParityDump, EqualSamplesWithStringsCompareEqual) {
    const parity::Dump reference = WithSample("f10|focus", {1.0}, "Camera/dialogue UI/m button_shotgun");
    const parity::Dump actual = WithSample("f10|focus", {1.0}, "Camera/dialogue UI/m button_shotgun");
    EXPECT_TRUE(parity::Compare(reference, actual, 25).empty());
}

TEST(ParityDump, ADifferentStringIsADifference) {
    const parity::Dump reference = WithSample("f10|focus", {1.0}, "Camera/dialogue UI/m button_shotgun");
    const parity::Dump actual = WithSample("f10|focus", {1.0}, "");
    const std::vector<std::string> diffs = parity::Compare(reference, actual, 25);
    ASSERT_EQ(diffs.size(), 1u);
    EXPECT_NE(diffs[0].find("f10|focus"), std::string::npos);
    EXPECT_NE(diffs[0].find("m button_shotgun"), std::string::npos) << "the diff names both sides";
}

// The numbers still decide, to their own tolerance.
TEST(ParityDump, NumbersStillCompareWithTolerance) {
    const parity::Dump reference = WithSample("f10|ray", {480.0, 270.0}, "shotgun");
    EXPECT_TRUE(parity::Compare(reference, WithSample("f10|ray", {480.00001, 270.0}, "shotgun"), 25).empty());
    EXPECT_FALSE(parity::Compare(reference, WithSample("f10|ray", {481.0, 270.0}, "shotgun"), 25).empty());
}
