#include "extract/remove_tree.h"

#include "extract/long_path.h"

#include <chrono>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace extract {

namespace {

constexpr int kAttempts = 30;
constexpr auto kPause = std::chrono::milliseconds(100);

// Without the long-path prefix, for messages.
std::string Utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    const std::string out(s.begin(), s.end());
    return out.rfind("\\\\?\\", 0) == 0 ? out.substr(4) : out;
}

// A junction or symbolic link. libstdc++ on Windows reports a junction as a
// plain directory, so the attribute is read directly.
bool IsLink(const fs::path& p) {
#if defined(_WIN32)
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code ec;
    return fs::is_symlink(fs::symlink_status(p, ec));
#endif
}

// One entry whose children are already gone.
bool RemoveOne(const fs::path& p, std::string& error) {
    std::error_code ec;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        if (fs::remove(p, ec)) return true;
        if (!ec) return true;   // it was not there
        if (attempt == 0) {
            // Windows will not delete a read-only file.
            std::error_code ignored;
            fs::permissions(p, fs::perms::owner_write, fs::perm_options::add | fs::perm_options::nofollow, ignored);
            continue;
        }
        std::this_thread::sleep_for(kPause);
    }
    error = "cannot delete " + Utf8(p) + ": " + ec.message() + " (is it open in another program?)";
    return false;
}

}  // namespace

bool RemoveTree(const fs::path& tree, std::string& error) {
    const fs::path path = LongPath(tree);   // the unpacked game runs past 260 characters
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(path, ec);
    if (st.type() == fs::file_type::not_found) return true;
    if (ec) {
        error = "cannot read " + Utf8(path) + ": " + ec.message();
        return false;
    }
    // A link is removed as itself, never followed into what it points at.
    if (st.type() == fs::file_type::directory && !IsLink(path)) {
        std::vector<fs::path> children;
        for (fs::directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) children.push_back(it->path());
        if (ec) {
            error = "cannot list " + Utf8(path) + ": " + ec.message();
            return false;
        }
        for (const fs::path& c : children) {
            if (!RemoveTree(c, error)) return false;
        }
    }
    return RemoveOne(path, error);
}

}  // namespace extract
