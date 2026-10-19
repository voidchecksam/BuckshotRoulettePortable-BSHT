#pragma once

#include <filesystem>

namespace extract {

// The running program's own file, to start its bake subcommands.
std::filesystem::path SelfPath();

}  // namespace extract
