// app/device_features: the Vita runs only engine paths proven on the device.
// Prop batching and GPU skinning drew props in the wrong place on a Vita, so
// both are off there by default.
#include "app/device_features.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

namespace {

TEST(DeviceFeatures, UnprovenPathsAreOffOnTheVita) {
    for (const app::DeviceFeatureInfo& f : app::DeviceFeatures()) {
        EXPECT_EQ(app::OnByDefaultOnDevice(f.id), f.provenOnDevice) << f.name;
    }
    EXPECT_FALSE(app::OnByDefaultOnDevice(app::DeviceFeature::PropBatching));
    EXPECT_FALSE(app::OnByDefaultOnDevice(app::DeviceFeature::GpuSkinning));
}

TEST(DeviceFeatures, EveryFeatureHasANameAndItsSwitches) {
    for (const app::DeviceFeatureInfo& f : app::DeviceFeatures()) {
        EXPECT_FALSE(std::string(f.name).empty());
        EXPECT_EQ(std::string(f.offSwitch).rfind("BSHT_NO_", 0), 0u) << f.name;
        EXPECT_EQ(std::string(f.forceSwitch).rfind("BSHT_FORCE_", 0), 0u) << f.name;
    }
}

TEST(DeviceFeatures, OnByDefaultAndNoSwitchMeansOn) {
    const app::FeatureVerdict v = app::DecideFeature(app::DeviceFeature::PropBatching, true, false, false);

    EXPECT_TRUE(v.on);
}

TEST(DeviceFeatures, TheOffSwitchWins) {
    const app::FeatureVerdict v = app::DecideFeature(app::DeviceFeature::PropBatching, true, true, false);

    EXPECT_FALSE(v.on);
    EXPECT_NE(v.why.find("BSHT_NO_PROP_BATCHING"), std::string::npos) << v.why;
}

TEST(DeviceFeatures, OffByDefaultSaysItIsNotProven) {
    const app::FeatureVerdict v = app::DecideFeature(app::DeviceFeature::GpuSkinning, false, false, false);

    EXPECT_FALSE(v.on);
    EXPECT_NE(v.why.find("not proven on the device"), std::string::npos) << v.why;
}

TEST(DeviceFeatures, TheForceSwitchTurnsAnUnprovenPathOnForAnAbRun) {
    const app::FeatureVerdict v = app::DecideFeature(app::DeviceFeature::GpuSkinning, false, false, true);

    EXPECT_TRUE(v.on);
    EXPECT_NE(v.why.find("BSHT_FORCE_GPU_SKINNING"), std::string::npos) << v.why;
}

TEST(DeviceFeatures, OnlyGameAppReadsTheRenderSwitches) {
    std::ifstream in(BSHT_SOURCE_DIR "/src/render/scene_renderer.cpp");
    std::stringstream s;
    s << in.rdbuf();

    ASSERT_FALSE(s.str().empty());
    EXPECT_EQ(s.str().find("ReadDebugSwitch"), std::string::npos);
}

}  // namespace
