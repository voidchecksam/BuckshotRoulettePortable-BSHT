#include "app/device_features.h"

#include <array>

namespace app {

namespace {

constexpr std::array<DeviceFeatureInfo, 2> kFeatures{{
    {DeviceFeature::PropBatching, "prop batching", "BSHT_NO_PROP_BATCHING", "BSHT_FORCE_PROP_BATCHING",
     /*provenOnDevice=*/false, /*provenInVita3K=*/true},
    {DeviceFeature::GpuSkinning, "GPU skinning", "BSHT_NO_GPU_SKINNING", "BSHT_FORCE_GPU_SKINNING",
     /*provenOnDevice=*/false, /*provenInVita3K=*/true},
}};

}  // namespace

std::span<const DeviceFeatureInfo> DeviceFeatures() { return kFeatures; }

const DeviceFeatureInfo& Info(DeviceFeature f) {
    for (const DeviceFeatureInfo& i : kFeatures) {
        if (i.id == f) return i;
    }
    return kFeatures.front();   // unreachable: every enumerator has a row
}

bool OnByDefaultOnDevice(DeviceFeature f) { return Info(f).provenOnDevice; }

FeatureVerdict DecideFeature(DeviceFeature f, bool onByDefault, bool offSwitch, bool forceSwitch) {
    const DeviceFeatureInfo& i = Info(f);
    if (offSwitch) return {false, std::string("turned off (switch ") + i.offSwitch + ")"};
    if (forceSwitch) return {true, std::string("forced on (switch ") + i.forceSwitch + ")"};
    if (onByDefault) return {true, "on"};
    return {false, "off by default: not proven on the device (app/device_features.h)"};
}

}  // namespace app
