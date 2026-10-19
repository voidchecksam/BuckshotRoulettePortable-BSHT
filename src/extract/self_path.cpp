#include "extract/self_path.h"

#if defined(_WIN32)
#include <windows.h>
#include <iterator>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#endif

namespace extract {

std::filesystem::path SelfPath() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return std::filesystem::path(std::wstring(buf, n));
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) != 0) return {};
    return std::filesystem::canonical(buf);
#else
    return std::filesystem::read_symlink("/proc/self/exe");
#endif
}

}  // namespace extract
