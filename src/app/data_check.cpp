#include "app/data_check.h"

#include "app/scene_host.h"
#include "assets/game_data_format.h"
#include "core/json_lite.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace app {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kLineWidth = 60;   // the data screen holds 60 characters a line (960 / 16)

std::string Fit(std::string line) {
    if (line.size() > kLineWidth) line = line.substr(0, kLineWidth - 3) + "...";
    return line;
}

GameDataCheck Fail(std::initializer_list<std::string> lines, int format = -1, std::size_t files = 0) {
    GameDataCheck c;
    for (const std::string& l : lines) c.lines.push_back(Fit(l));
    c.format = format;
    c.files = files;
    return c;
}

bool IsFile(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

// The keys of the manifest's "files" object.
std::vector<std::string> ListedFiles(const std::string& text) {
    std::vector<std::string> out;
    const char* begin = nullptr;
    const char* end = nullptr;
    if (!core::json::FindObject(text.data(), text.data() + text.size(), "files", &begin, &end)) return out;
    // Inside the braces every entry is "name": "hash"; the names are the keys.
    const char* p = begin + 1;
    while (p < end) {
        while (p < end && *p != '"') ++p;
        std::string key, hash;
        p = core::json::DecodeString(p, end, key);
        while (p < end && core::json::IsWs(*p)) ++p;
        if (p >= end || *p != ':') break;
        while (p < end && *p != '"') ++p;
        p = core::json::DecodeString(p, end, hash);
        if (p >= end) break;
        out.push_back(key);
    }
    return out;
}

// The manifest's "sizes" object: name -> bytes. Empty when it has none.
std::vector<std::pair<std::string, std::uintmax_t>> ListedSizes(const std::string& text) {
    std::vector<std::pair<std::string, std::uintmax_t>> out;
    const char* begin = nullptr;
    const char* end = nullptr;
    if (!core::json::FindObject(text.data(), text.data() + text.size(), "sizes", &begin, &end)) return out;
    const char* p = begin + 1;
    while (p < end) {
        while (p < end && *p != '"') ++p;
        std::string key;
        p = core::json::DecodeString(p, end, key);
        while (p < end && (core::json::IsWs(*p) || *p == ':')) ++p;
        if (p >= end || *p < '0' || *p > '9') break;
        char* after = nullptr;
        out.emplace_back(key, std::strtoull(p, &after, 10));
        p = after;
    }
    return out;
}

}  // namespace

std::vector<std::string> RequiredGameFiles() {
    std::vector<std::string> out = {"sfx.bank",       "music_main_gate_modded.ogg", "music_room.ogg",
                                    "input_map.json", "ui_fonts.json",  "particle_smoke.rgba"};
    for (const char* scene : {"res://scenes/main.tscn", "res://scenes/menu.tscn", "res://scenes/heaven.tscn"}) {
        const SceneFiles f = FilesForScene(scene, "", "", "");
        for (const std::string& n : {f.room, f.runtime, f.animations, f.ui, f.uiPixels, f.labels}) {
            if (!n.empty()) out.push_back(n);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

GameDataCheck CheckGameData(const std::string& gameDir) {
    std::string dir = gameDir;
    while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    const fs::path root(dir);

    std::ifstream in(root / "manifest.json", std::ios::binary);
    if (!in) return Fail({"No game data in " + dir, "Run bsht-extract and copy its game folder."});
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);

    const char* b = text.data();
    const char* e = b + text.size();
    const int format = core::json::ReadIntTopLevel(b, e, "format", -1);
    const std::vector<std::string> listed = ListedFiles(text);
    if (format < 0 || listed.empty()) {
        return Fail({"The game data's manifest is damaged.", "Run bsht-extract again."});
    }
    if (format < assets::kGameDataFormat) {
        return Fail({"Game data is format " + std::to_string(format) + ", this build needs " +
                         std::to_string(assets::kGameDataFormat) + ".",
                     "Run bsht-extract from this release."},
                    format, listed.size());
    }
    if (format > assets::kGameDataFormat) {
        return Fail({"Game data is newer (format " + std::to_string(format) + ") than this vpk.",
                     "Install the vpk from that release."},
                    format, listed.size());
    }

    std::vector<std::string> wanted = listed;
    for (const std::string& n : RequiredGameFiles()) {
        if (std::find(wanted.begin(), wanted.end(), n) == wanted.end()) wanted.push_back(n);
    }
    for (const std::string& n : wanted) {
        if (!IsFile(root / n)) {
            return Fail({"Game data is missing a file:", n, "Copy the whole game folder again."}, format,
                        listed.size());
        }
    }
    // A copy that stopped part way leaves a short file.
    for (const auto& [name, size] : ListedSizes(text)) {
        std::error_code ec;
        if (fs::file_size(root / name, ec) != size || ec) {
            return Fail({"Game data is damaged:", name, "Copy the whole game folder again."}, format,
                        listed.size());
        }
    }
    GameDataCheck ok;
    ok.ok = true;
    ok.format = format;
    ok.files = listed.size();
    return ok;
}

std::string DescribeCheck(const GameDataCheck& check) {
    if (check.ok) {
        return "ok, format " + std::to_string(check.format) + ", " + std::to_string(check.files) + " files";
    }
    std::string out;
    for (const std::string& l : check.lines) out += (out.empty() ? "" : " ") + l;
    return out;
}

}  // namespace app
