// A diagnostic switch the device can set: getenv is empty on the Vita, so a
// file of the switch's name in the save directory turns it on too (the batched
// and skinned programs need an A/B there).
#include "app/debug_switch.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

const std::string kDir = BSHT_BINARY_DIR "/debug_switch_test";

TEST(DebugSwitch, OffWithNeitherTheVariableNorTheFile) {
    const app::DebugSwitch s = app::ReadDebugSwitch("BSHT_TEST_SWITCH_NONE", kDir);
    EXPECT_FALSE(s.on);
    EXPECT_TRUE(s.source.empty());
}

TEST(DebugSwitch, AFileInTheSaveDirectoryTurnsItOn) {
    const std::string path = std::string(BSHT_BINARY_DIR) + "/BSHT_TEST_SWITCH_FILE";
    std::remove(path.c_str());
    EXPECT_FALSE(app::ReadDebugSwitch("BSHT_TEST_SWITCH_FILE", BSHT_BINARY_DIR).on);
    if (FILE* f = std::fopen(path.c_str(), "wb")) std::fclose(f);
    const app::DebugSwitch s = app::ReadDebugSwitch("BSHT_TEST_SWITCH_FILE", BSHT_BINARY_DIR);
    std::remove(path.c_str());
    EXPECT_TRUE(s.on);
    EXPECT_EQ(s.source, path) << "the log names where it came from";
}

TEST(DebugSwitch, TheEnvironmentVariableTurnsItOn) {
    SetEnv("BSHT_TEST_SWITCH_ENV", "1");
    const app::DebugSwitch s = app::ReadDebugSwitch("BSHT_TEST_SWITCH_ENV", kDir);
    SetEnv("BSHT_TEST_SWITCH_ENV", nullptr);
    EXPECT_TRUE(s.on);
    EXPECT_EQ(s.source, "environment");
}

}  // namespace
