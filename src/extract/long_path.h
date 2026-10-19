// Windows refuses paths past 260 characters unless they are absolute and carry
// the "\\?\" prefix. Godot adds it itself (FileAccessWindows::fix_path); the
// tool adds it where it writes or deletes the unpacked game, whose paths in
// v2.2 run past that limit. Elsewhere the path is returned as it is.
#pragma once

#include <filesystem>
#include <string>

namespace extract {

inline std::filesystem::path LongPath(const std::filesystem::path& p) {
#if defined(_WIN32)
    if (p.wstring().rfind(L"\\\\?\\", 0) == 0) return p;
    std::error_code ec;
    const std::wstring s = std::filesystem::absolute(p, ec).make_preferred().wstring();
    if (ec) return p;
    if (s.rfind(L"\\\\", 0) == 0) return std::filesystem::path(L"\\\\?\\UNC\\" + s.substr(2));   // a network share
    return std::filesystem::path(L"\\\\?\\" + s);
#else
    return p;
#endif
}

}  // namespace extract
