// The data folder the port reads: which files, under which names, and a
// manifest of what was written.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace extract {

struct ShippedFile {
    std::string from;   // relative to the out root: "extracted/..." or "baked/..."
    std::string to;     // the name in game/, never with a space
};

// Fills `error` with the first required file that is missing.
std::vector<ShippedFile> ShippedFiles(const std::filesystem::path& outRoot, std::string& error);
// Builds the new data in the work folder (outRoot's parent unless `workDir` is
// given) and swaps it in for `gameOut`; a failed swap leaves the old folder in place.
bool Package(const std::filesystem::path& outRoot, const std::filesystem::path& gameOut,
             const std::string& gameVersion, std::string& error,
             const std::filesystem::path& workDir = {});

}  // namespace extract
