// app/scenario_boot: the Vita build replays a parity scenario when the save
// directory holds a scenario.json, which is how Vita3K runs reach the table
// without playing the intro by hand.
#include "app/scenario_boot.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

namespace fs = std::filesystem;

// Inside the build tree, never the system temp dir (nothing goes on C:).
fs::path FreshDir(const char* name) {
    const fs::path dir = fs::current_path() / "scenario_boot_test" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

TEST(ScenarioBoot, ReadsTheScenarioFileBesideTheSaves) {
    const fs::path dir = FreshDir("present");
    std::ofstream(dir / "scenario.json") << R"({"name": "frames_x", "scene": "main"})";

    const std::optional<std::string> s = app::FindDeviceScenario(dir.string());

    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(*s, R"({"name": "frames_x", "scene": "main"})");
}

TEST(ScenarioBoot, NoFileMeansAPlayersBoot) {
    const fs::path dir = FreshDir("absent");

    EXPECT_FALSE(app::FindDeviceScenario(dir.string()).has_value());
}

TEST(ScenarioBoot, AnEmptyFileIsNotAScenario) {
    const fs::path dir = FreshDir("empty");
    std::ofstream(dir / "scenario.json");

    EXPECT_FALSE(app::FindDeviceScenario(dir.string()).has_value());
}

TEST(ScenarioBoot, ShotsAreNamedAsTheDesktopNamesThem) {
    EXPECT_EQ(app::ScenarioShotPath("ux0:data/bsht", "frames_use_cigarettes_b2r1", 1480),
              "ux0:data/bsht/port_frames_use_cigarettes_b2r1_1480.ppm");
}

}  // namespace
