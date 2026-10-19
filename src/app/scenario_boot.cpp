#include "app/scenario_boot.h"

#include <fstream>
#include <sstream>

namespace app {

std::optional<std::string> FindDeviceScenario(const std::string& saveDir) {
    std::ifstream in(saveDir + "/scenario.json", std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    if (text.str().empty()) return std::nullopt;
    return text.str();
}

std::string ScenarioShotPath(const std::string& dir, const std::string& name, int at) {
    return dir + "/port_" + name + "_" + std::to_string(at) + ".ppm";
}

}  // namespace app
