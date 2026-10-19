// Checks made before anything is written or run. Each problem is one
// sentence that tells the player what to do.
#pragma once

#include "extract/godot_run.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace extract {

struct PreflightFacts {
    std::filesystem::path out;          // absolute
    std::filesystem::path gameFolder;   // the folder holding the game file; empty with --project
    uint64_t freeBytes = 0;             // on the out folder's drive
    HostOs os = HostOs::Windows;        // the pipeline sets ThisOs()
    std::string godotVersion;           // first line of `godot --version`
    std::vector<std::string> missingExporters;
    bool foreignGameFolder = false;     // <out>/game exists and is not ours
    bool foreignWorkFolder = false;     // <out>/.work exists and is not ours
};

inline constexpr uint64_t kNeededBytes = 3ull << 30;
// Windows' path limit (MAX_PATH less the terminator), and the longest path the
// tool itself opens under "<out>/.work/" without the long-path prefix: the bake
// reads out/extracted/inline_textures/<texture>.png, up to 155 characters in
// v1.2 and v2.2. The unpacked game runs longer but is written with the prefix,
// and Godot adds the prefix itself.
inline constexpr size_t kWindowsPathLimit = 259;
inline constexpr size_t kLongestWorkPath = 160;

std::vector<std::string> PreflightProblems(const PreflightFacts& f);
// `child` is `parent` or under it. Links are resolved where the path exists,
// and Windows paths compare without case.
bool IsInside(const std::filesystem::path& child, const std::filesystem::path& parent);
// A game/ with our manifest, a .work/ with our mark, or nothing at all.
bool MadeByUs(const std::filesystem::path& folder);
// Trims spaces and stray quotes, as cmd.exe leaves them.
std::string CleanArgument(std::string a);

// <out>/.bsht-extract.lock, held for the whole run. The lock is the open
// file, which the system closes however the process ends, so a killed run
// never blocks the next one.
class OutputLock {
public:
    explicit OutputLock(const std::filesystem::path& out);
    ~OutputLock();
    OutputLock(const OutputLock&) = delete;
    OutputLock& operator=(const OutputLock&) = delete;
    bool Held() const { return held_; }
    bool Busy() const { return busy_; }   // another run holds it
    const std::string& Error() const { return error_; }

private:
    std::filesystem::path file_;
#if defined(_WIN32)
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
    bool held_ = false;
    bool busy_ = false;
    std::string error_;
};

}  // namespace extract
