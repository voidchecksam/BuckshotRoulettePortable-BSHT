#include "extract/unpack.h"

#include "extract/long_path.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

namespace extract {

namespace {

constexpr size_t kChunk = 1 << 20;

// v2.2's PipelineManager asks Godot for a rendering device, finds none when
// headless and takes the game's OpenGL path: the room hidden for 0.1 s and the
// lights halved. Ours keeps the Vulkan path the reference frames come from.
struct Replacement {
    const char* gamePath;
    const char* ours;
};
constexpr Replacement kReplacements[] = {{"scripts/PipelineManager.gd", "pipeline_manager.gd"}};

fs::path Utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string Utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

}  // namespace

std::optional<std::string> SafeRelativePath(std::string_view res) {
    constexpr std::string_view kPrefix = "res://";
    if (res.substr(0, kPrefix.size()) != kPrefix) return std::nullopt;
    std::string_view rel = res.substr(kPrefix.size());
    if (rel.empty() || rel.find('\\') != std::string_view::npos || rel.find(':') != std::string_view::npos) {
        return std::nullopt;
    }
    size_t start = 0;
    while (start <= rel.size()) {
        const size_t end = std::min(rel.find('/', start), rel.size());
        const std::string_view seg = rel.substr(start, end - start);
        if (seg.empty() || seg == "." || seg == "..") return std::nullopt;
        start = end + 1;
    }
    return std::string(rel);
}

bool Unpack(const std::string& packPath, const PackInfo& pack, const fs::path& gameDir, UnpackStats& stats,
            std::string& error) {
    // Check every path before writing anything.
    for (const PackEntry& e : pack.entries) {
        if (!SafeRelativePath(e.path)) {
            error = "refusing pack entry " + e.path + ": it points outside the folder";
            return false;
        }
    }
    std::ifstream in(Utf8Path(packPath), std::ios::binary);
    if (!in) {
        error = "cannot open " + packPath;
        return false;
    }
    std::vector<char> buf(kChunk);
    const fs::path root = LongPath(gameDir);
    for (const PackEntry& e : pack.entries) {
        const fs::path out = gameDir / Utf8Path(*SafeRelativePath(e.path));   // for messages
        // The prefix turns off Windows' path clean-up, so '/' must become '\'.
        const fs::path target = (root / Utf8Path(*SafeRelativePath(e.path))).make_preferred();
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        std::ofstream o(target, std::ios::binary | std::ios::trunc);
        if (!o) {
            error = "cannot write " + Utf8(out);
            return false;
        }
        in.seekg(static_cast<std::streamoff>(e.offset));
        uint64_t left = e.size;
        while (left > 0) {
            const size_t n = static_cast<size_t>(std::min<uint64_t>(left, kChunk));
            in.read(buf.data(), static_cast<std::streamsize>(n));
            o.write(buf.data(), static_cast<std::streamsize>(n));
            if (!in || !o) {
                error = "unpacking " + e.path + " failed (disk full?)";
                return false;
            }
            left -= n;
        }
        ++stats.files;
        stats.bytes += e.size;
    }
    return true;
}

void WriteOverride(const fs::path& gameDir, bool withSteamStub) {
    std::ofstream f(gameDir / "override.cfg", std::ios::trunc);
    f << kOverrideMark << "\n\n"
         "[application]\n\n"
         "config/use_custom_user_dir=true\n"
         "config/custom_user_dir_name=\"bsht_extract\"\n\n"
         "[autoload]\n\n";
    if (withSteamStub) f << "Steam=\"*res://tools/godot/steam_stub.gd\"\n";
    f << "GlobalSteam=\"*res://tools/godot/steam_off.gd\"\n";
}

bool OverrideIsOurs(const fs::path& gameDir) {
    std::ifstream f(gameDir / "override.cfg", std::ios::binary);
    std::string first;
    if (!f || !std::getline(f, first)) return false;
    if (!first.empty() && first.back() == '\r') first.pop_back();
    return first == kOverrideMark;
}

ProjectOverride::ProjectOverride(const fs::path& project) : file_(project / "override.cfg") {
    std::error_code ec;
    if (fs::exists(file_, ec)) {
        if (!OverrideIsOurs(project)) return;
        std::ifstream f(file_, std::ios::binary);
        saved_.assign(std::istreambuf_iterator<char>(f), {});
    }
    // No extension list: GodotSteam is not loaded, and the scripts that name
    // Steam need the stub to compile.
    WriteOverride(project, !fs::exists(project / ".godot" / "extension_list.cfg", ec));
    owned_ = true;
}

ProjectOverride::~ProjectOverride() {
    if (!owned_) return;
    std::error_code ec;
    if (saved_.empty()) {
        fs::remove(file_, ec);
    } else {
        std::ofstream(file_, std::ios::binary | std::ios::trunc) << saved_;
    }
}

void DisableExtensions(const fs::path& gameDir) {
    std::error_code ec;
    fs::remove(gameDir / ".godot" / "extension_list.cfg", ec);
}

bool CopyExporters(const fs::path& from, const fs::path& gameDir, std::string& error) {
    std::error_code ec;
    const fs::path to = gameDir / "tools" / "godot";
    fs::create_directories(to, ec);
    fs::copy(from, to, fs::copy_options::overwrite_existing | fs::copy_options::recursive, ec);
    if (ec) error = "cannot copy the exporters: " + ec.message();
    return !ec;
}

bool ReplaceScripts(const fs::path& exporters, const fs::path& gameDir, std::vector<std::string>& replaced,
                    std::string& error) {
    for (const Replacement& r : kReplacements) {
        const fs::path target = gameDir / Utf8Path(r.gamePath);
        std::error_code ec;
        if (!fs::is_regular_file(target, ec)) continue;
        fs::copy_file(exporters / r.ours, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = std::string("cannot replace ") + r.gamePath + ": " + ec.message();
            return false;
        }
        replaced.push_back(r.gamePath);
    }
    return true;
}

}  // namespace extract
