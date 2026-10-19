// Which engine paths the Vita runs by default. A path is on by default on the
// device only once it has run there: prop batching and GPU skinning drew props
// in the wrong place on a Vita while Vita3K drew them right (a GPU fault), so
// both are off there. The desktop keeps every path on.
//
// Each feature has two switches (app/debug_switch.h: an environment variable on
// the desktop, an empty file in ux0:data/bsht on the Vita):
//   BSHT_NO_<X>     off, whatever the default (A/B frames)
//   BSHT_FORCE_<X>  on, whatever the default (trying a fix on the device)
// GameApp decides each feature once, logs the verdict, and hands the renderer a
// bool; nothing else reads these switches.
#pragma once

#include <span>
#include <string>

namespace app {

enum class DeviceFeature { PropBatching, GpuSkinning };

struct DeviceFeatureInfo {
    DeviceFeature id;
    const char* name;          // "prop batching"
    const char* offSwitch;     // "BSHT_NO_PROP_BATCHING"
    const char* forceSwitch;   // "BSHT_FORCE_PROP_BATCHING"
    bool provenOnDevice;       // ran correctly on a Vita (report + frames)
    bool provenInVita3K;       // ran correctly in tools/vita3k (frames)
};

std::span<const DeviceFeatureInfo> DeviceFeatures();
const DeviceFeatureInfo& Info(DeviceFeature f);

// The Vita entry point's default for a feature: on only if proven there.
bool OnByDefaultOnDevice(DeviceFeature f);

struct FeatureVerdict {
    bool on = false;
    std::string why;   // what decided it, for the log line
};

// The feature's default (AppConfig) and its two switches, in that order of
// precedence: the off switch, then the force switch, then the default.
FeatureVerdict DecideFeature(DeviceFeature f, bool onByDefault, bool offSwitch, bool forceSwitch);

}  // namespace app
