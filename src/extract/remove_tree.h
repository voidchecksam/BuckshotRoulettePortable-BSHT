#pragma once

#include <filesystem>
#include <string>

namespace extract {

// Deletes a file or a folder and everything in it. Unlike
// std::filesystem::remove_all it retries a file another program holds for a
// few seconds (antivirus scanners open new files), clears read-only flags, and
// names the file it could not delete. Links are removed, never followed.
bool RemoveTree(const std::filesystem::path& path, std::string& error);

}  // namespace extract
