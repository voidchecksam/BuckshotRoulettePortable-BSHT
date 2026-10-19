#include "app/debug_switch.h"

#include <cstdio>
#include <cstdlib>

namespace app {

DebugSwitch ReadDebugSwitch(const char* name, const std::string& dir) {
    DebugSwitch s;
    if (std::getenv(name) != nullptr) {
        s.on = true;
        s.source = "environment";
        return s;
    }
    // fopen, not std::filesystem: newlib on the Vita has only the C library.
    const std::string path = dir + "/" + name;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
        s.on = true;
        s.source = path;
    }
    return s;
}

}  // namespace app
