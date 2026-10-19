// A diagnostic switch the device can set. On the desktop it is an environment
// variable; getenv is empty on the Vita, so a file of the same name in the
// save directory turns it on as well: ux0:data/bsht/BSHT_NO_PROP_BATCHING,
// made with VitaShell, draws every prop the unbatched way on the next boot.
// GameApp logs each switch that is on and where it came from.
//
//   const app::DebugSwitch s = app::ReadDebugSwitch("BSHT_NO_GPU_SKINNING", saveDir);
//   if (s.on) Log("switch BSHT_NO_GPU_SKINNING on (" + s.source + ")");
#pragma once

#include <string>

namespace app {

struct DebugSwitch {
    bool on = false;
    std::string source;   // "environment", or the file's path
};

DebugSwitch ReadDebugSwitch(const char* name, const std::string& dir);

}  // namespace app
