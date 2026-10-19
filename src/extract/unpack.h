// Unpacks a pack into a folder Godot can run as a project, and sets that
// project up so it stays private: no Steam, no extensions, user data in the
// work folder.
#pragma once

#include "extract/pack_reader.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace extract {

// "res://a/b" -> "a/b". Nothing that could leave the folder: no "..", no ".",
// no empty segments, no absolute or drive paths, no backslashes.
std::optional<std::string> SafeRelativePath(std::string_view resPath);

struct UnpackStats {
    size_t files = 0;
    uint64_t bytes = 0;
};

bool Unpack(const std::string& packPath, const PackInfo& pack, const std::filesystem::path& gameDir,
            UnpackStats& stats, std::string& error);

// The first line of every override.cfg the tool writes, so a leftover from a
// killed run can be told from one a developer wrote.
inline constexpr const char* kOverrideMark = "; written by bsht-extract";

// `withSteamStub` is false for a project that loads GodotSteam itself.
void WriteOverride(const std::filesystem::path& gameDir, bool withSteamStub);
bool OverrideIsOurs(const std::filesystem::path& gameDir);
void DisableExtensions(const std::filesystem::path& gameDir);
bool CopyExporters(const std::filesystem::path& from, const std::filesystem::path& gameDir, std::string& error);

// --project: our override.cfg for the run (private user folder, Steam off, the
// Steam stub when GodotSteam is not loaded), then the project's own back. One
// carrying our mark (the reference game's, or a killed run's) is restored after
// the run; one without it is a developer's: not Owned(), nothing is touched.
class ProjectOverride {
public:
    explicit ProjectOverride(const std::filesystem::path& project);
    ~ProjectOverride();
    ProjectOverride(const ProjectOverride&) = delete;
    ProjectOverride& operator=(const ProjectOverride&) = delete;
    bool Owned() const { return owned_; }

private:
    std::filesystem::path file_;
    std::string saved_;   // the marked file that was there, put back after
    bool owned_ = false;
};

// Swaps game scripts that misbehave headless for ours from `exporters`, where
// the game has them. `replaced` lists the game paths swapped.
bool ReplaceScripts(const std::filesystem::path& exporters, const std::filesystem::path& gameDir,
                    std::vector<std::string>& replaced, std::string& error);

}  // namespace extract
