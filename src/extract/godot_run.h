// Starts Godot, or this program's own bake subcommands, as a child process
// and waits for it. Arguments and paths are UTF-8; output goes to a log file.
#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace extract {

enum class HostOs { Windows, Linux, MacOs };
HostOs ThisOs();
std::string UserDataVariable(HostOs os, const std::filesystem::path& userData);

struct ChildRun {
    std::filesystem::path program;
    std::vector<std::string> args;
    std::filesystem::path workingDir;
    std::vector<std::string> extraEnv;   // "NAME=value", added to ours
    std::filesystem::path log;           // stdout and stderr, truncated first
    std::chrono::milliseconds timeout{std::chrono::minutes(45)};
};

struct ChildResult {
    bool started = false;
    int exitCode = -1;
    bool timedOut = false;
    bool stopped = false;   // by RequestStop (Ctrl+C)
    std::string error;
};

ChildResult RunChild(const ChildRun& run);

// Ctrl+C sets this; a running child is stopped and RunChild returns.
void RequestStop();
bool StopRequested();
void ClearStop();

std::vector<std::string> GodotArgs(const std::filesystem::path& project, const std::string& script,
                                   const std::vector<std::string>& userArgs);

// The first line `godot --version` prints ("4.1.4.stable.official.fe0e8e557"),
// or "" if it does not run. Its output passes through a log in `scratchDir`,
// which is removed again.
std::string GodotVersion(const std::filesystem::path& godot, const std::filesystem::path& scratchDir);

// The parent's "NAME=value" entries without the names `extra` sets, then
// `extra`. Windows compares names ignoring case.
std::vector<std::string> MergeEnvironment(const std::vector<std::string>& parent,
                                          const std::vector<std::string>& extra, bool ignoreCase);

}  // namespace extract
