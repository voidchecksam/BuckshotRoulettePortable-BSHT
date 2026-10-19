#include "extract/preflight.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#include <cwchar>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace extract {

namespace {

std::string Utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// Windows paths ignore case; elsewhere they do not.
bool SamePart(const fs::path& a, const fs::path& b) {
#if defined(_WIN32)
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
#else
    return a == b;
#endif
}

// Two existing folders that are the same one, however they are named: a
// junction, a subst drive, a short name or other case reach the same file ID.
bool SameFolder(const fs::path& a, const fs::path& b) {
#if defined(_WIN32)
    auto id = [](const fs::path& p, BY_HANDLE_FILE_INFORMATION& info) {
        HANDLE h = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        const bool ok = GetFileInformationByHandle(h, &info) != 0;
        CloseHandle(h);
        return ok;
    };
    BY_HANDLE_FILE_INFORMATION x{}, y{};
    return id(a, x) && id(b, y) && x.dwVolumeSerialNumber == y.dwVolumeSerialNumber &&
           x.nFileIndexHigh == y.nFileIndexHigh && x.nFileIndexLow == y.nFileIndexLow;
#else
    std::error_code ec;
    return fs::equivalent(a, b, ec);
#endif
}

// `child` or one of its existing ancestors is `parent` itself.
bool ReachesFolder(const fs::path& child, const fs::path& parent) {
    std::error_code ec;
    fs::path c = fs::absolute(child, ec);
    if (ec) return false;
    for (; !c.empty(); c = c.parent_path()) {
        if (fs::exists(c, ec) && SameFolder(c, parent)) return true;
        if (c == c.parent_path()) break;   // the root
    }
    return false;
}

}  // namespace

bool IsInside(const fs::path& child, const fs::path& parent) {
    if (parent.empty()) return false;
    if (ReachesFolder(child, parent)) return true;
    std::error_code ec;
    fs::path c = fs::weakly_canonical(child, ec);
    if (ec) c = child;
    fs::path p = fs::weakly_canonical(parent, ec);
    if (ec) p = parent;
    c = c.lexically_normal();
    p = p.lexically_normal();
    auto ci = c.begin();
    for (auto pi = p.begin(); pi != p.end(); ++pi, ++ci) {
        if (pi->empty()) continue;   // a trailing separator
        if (ci == c.end() || !SamePart(*ci, *pi)) return false;
    }
    return true;
}

std::vector<std::string> PreflightProblems(const PreflightFacts& f) {
    std::vector<std::string> p;
    if (IsInside(f.out, f.gameFolder)) {
        p.push_back("Choose an output folder outside the game's folder; this tool never writes beside the game.");
    }
    if (f.freeBytes < kNeededBytes) {
        p.push_back("The output drive needs about 3 GB free; it has " + std::to_string(f.freeBytes >> 30) +
                    " GB free.");
    }
#if defined(_WIN32)
    const size_t outLength = f.out.wstring().size();   // Windows counts UTF-16 units
#else
    const size_t outLength = Utf8(f.out).size();
#endif
    const size_t longest = outLength + std::string("/.work/").size() + kLongestWorkPath;
    if (f.os == HostOs::Windows && longest > kWindowsPathLimit) {
        p.push_back("The output path is too long for Windows; choose a shorter one, such as C:\\bsht-data.");
    }
    if (f.godotVersion.rfind("4.1.4.", 0) != 0) {
        p.push_back("Godot 4.1.4 is needed next to this tool (in godot/); found \"" + f.godotVersion + "\".");
    }
    for (const std::string& m : f.missingExporters) {
        p.push_back("The tool's exporters/ folder is missing " + m + "; unpack the release again.");
    }
    if (f.foreignGameFolder) {
        p.push_back("There is already a folder named game in " + Utf8(f.out) +
                    " that this tool did not make; choose another output folder.");
    }
    if (f.foreignWorkFolder) {
        p.push_back("There is already a folder named .work in " + Utf8(f.out) +
                    " that this tool did not make; choose another output folder.");
    }
    return p;
}

bool MadeByUs(const fs::path& folder) {
    std::error_code ec;
    if (!fs::exists(folder, ec)) return true;
    if (fs::exists(folder / ".bsht-extract", ec)) return true;   // the work folder's mark
    std::ifstream m(folder / "manifest.json", std::ios::binary);
    std::stringstream s;
    s << m.rdbuf();
    return s.str().find("\"tool\": \"bsht-extract") != std::string::npos;
}

std::string CleanArgument(std::string a) {
    auto strip = [](char c) { return c == ' ' || c == '\t' || c == '"' || c == '\r' || c == '\n'; };
    while (!a.empty() && strip(a.back())) a.pop_back();
    size_t start = 0;
    while (start < a.size() && strip(a[start])) ++start;
    return a.substr(start);
}

// "x" opens only a file that does not exist yet, so two runs cannot both hold it.
OutputLock::OutputLock(const fs::path& out) : file_(out / ".bsht-extract.lock") {
#if defined(_WIN32)
    // No sharing, and deleted when the handle closes.
    HANDLE h = CreateFileW(file_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        busy_ = e == ERROR_SHARING_VIOLATION;
        if (!busy_) error_ = "cannot create " + Utf8(file_) + " (Windows error " + std::to_string(e) + ")";
        return;
    }
    handle_ = h;
#else
    fd_ = open(file_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd_ < 0) {
        error_ = "cannot create " + Utf8(file_) + ": " + std::strerror(errno);
        return;
    }
    if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        busy_ = errno == EWOULDBLOCK;
        if (!busy_) error_ = "cannot lock " + Utf8(file_) + ": " + std::strerror(errno);
        close(fd_);
        fd_ = -1;
        return;
    }
#endif
    held_ = true;
}

OutputLock::~OutputLock() {
    if (!held_) return;
#if defined(_WIN32)
    CloseHandle(handle_);
#else
    unlink(file_.c_str());
    close(fd_);
#endif
}

}  // namespace extract
