#include "extract/package.h"

#include "assets/game_data_format.h"

#include "extract/remove_tree.h"

#include <picosha2.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace extract {

namespace {

constexpr const char* kExports[] = {
    "particle_smoke.rgba", "runtime_menu.json", "animations_menu.json", "ui_layout_menu.json",
    "ui_textures_menu.bin", "runtime_heaven.json", "animations_heaven.json", "ui_layout_heaven.json",
    "ui_textures_heaven.bin", "round_table.json", "audio_streams.json", "animations.json",
    "runtime_main.json", "runtime_shell.json", "ui_layout.json", "ui_textures.bin", "ui_fonts.json",
    "ui_font_atlas.bin", "dialogue_en.json", "scene_strings.json", "labels3d.json", "input_map.json"};
constexpr const char* kBakes[] = {"full_world.room", "menu.room", "heaven.room", "sfx.bank"};
constexpr const char* kLocales[] = {"br", "de", "ee", "es_latam", "es", "fr", "it", "ja",
                                    "ko", "pl", "pt", "ru", "tr", "ua", "zhs", "zht"};
constexpr int kItemScenes = 9;
constexpr const char* kToolVersion = "bsht-extract 1.0";

fs::path U8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string Name(const fs::path& p) {
    const std::u8string s = p.filename().u8string();
    return std::string(s.begin(), s.end());
}

std::string Flat(std::string name) {
    std::replace(name.begin(), name.end(), ' ', '_');
    return name;
}

std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

std::string JsonString(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string Sha256Of(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::vector<unsigned char> hash(picosha2::k_digest_size);
    picosha2::hash256(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>(), hash.begin(), hash.end());
    return picosha2::bytes_to_hex_string(hash.begin(), hash.end());
}

// The .ogg files under `dir`, by name, sorted.
std::vector<std::string> OggsIn(const fs::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".ogg") out.push_back(Name(it->path()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

std::vector<ShippedFile> ShippedFiles(const fs::path& out, std::string& error) {
    const fs::path ex = out / "extracted";
    std::vector<ShippedFile> files;
    std::set<std::string> taken;
    auto add = [&](const std::string& from, const std::string& to) {
        std::error_code ec;
        if (!fs::is_regular_file(out / U8(from), ec)) {
            if (error.empty()) error = from + " is missing";
            return;
        }
        if (taken.insert(to).second) files.push_back({from, to});
    };
    for (const char* b : kBakes) add(std::string("baked/") + b, b);
    for (const char* e : kExports) add(std::string("extracted/") + e, e);
    for (const char* c : kLocales) add(std::string("extracted/dialogue_") + c + ".json", std::string("dialogue_") + c + ".json");

    // Item scenes, and the collision faces each runtime export names.
    std::vector<std::string> names;
    std::error_code ec;
    for (fs::directory_iterator it(ex, ec), end; !ec && it != end; it.increment(ec)) names.push_back(Name(it->path()));
    std::sort(names.begin(), names.end());
    int items = 0;
    for (const std::string& n : names) {
        const bool runtimeJson = n.rfind("runtime_", 0) == 0 && U8(n).extension() == ".json";
        if (!runtimeJson) continue;
        if (n.rfind("runtime_item_", 0) == 0) {
            add("extracted/" + n, n);
            ++items;
        }
        if (ReadText(ex / U8(n)).find("\"collision_faces\"") != std::string::npos) {
            const std::string faces = n.substr(0, n.size() - 5) + "_faces.bin";
            add("extracted/" + faces, faces);
        }
    }
    if (items != kItemScenes && error.empty()) {
        error = "expected " + std::to_string(kItemScenes) + " item scenes, found " + std::to_string(items);
    }
    // Without lengths every long sound would stop after one frame.
    const std::regex someStream("\"streams\"\\s*:\\s*\\[\\s*\\{");
    if (error.empty() && !std::regex_search(ReadText(ex / "audio_streams.json"), someStream)) {
        error = "extracted/audio_streams.json lists no streams (export_audio_streams.gd found none)";
    }

    // The glyph pages ui_fonts.json lists.
    const std::regex page("ui_font_page_[a-z0-9_]+[.]bin");
    const std::string fonts = ReadText(ex / "ui_fonts.json");
    for (std::sregex_iterator it(fonts.begin(), fonts.end(), page), end; it != end; ++it) {
        add("extracted/" + it->str(), it->str());
    }

    // Music and long sounds, streamed from files whose names lose their
    // spaces (the device asks for them so).
    add("extracted/audio/music_room vol2.ogg", "music_room.ogg");
    add("extracted/audio/music/music main_gate modded.ogg", "music_menu.ogg");
    for (const std::string& m : OggsIn(ex / "audio" / "music")) add("extracted/audio/music/" + m, Flat(m));
    std::istringstream list(ReadText(out / "baked" / "music_list.txt"));
    for (std::string line; std::getline(list, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (!line.empty()) add("extracted/audio/" + line, Flat(Name(U8(line))));
    }

    // The desktop's mouse cursors, in a folder of their own; the Vita has none,
    // so they are not required.
    if (fs::exists(ex / "cursors" / "cursors.json", ec)) {
        std::vector<std::string> cursors;
        for (fs::directory_iterator it(ex / "cursors", ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec)) cursors.push_back(Name(it->path()));
        }
        std::sort(cursors.begin(), cursors.end());
        for (const std::string& c : cursors) add("extracted/cursors/" + c, "cursors/" + c);
    }
    return files;
}

bool Package(const fs::path& outRoot, const fs::path& gameOut, const std::string& gameVersion, std::string& error,
             const fs::path& workDir) {
    const std::vector<ShippedFile> files = ShippedFiles(outRoot, error);
    if (!error.empty()) return false;
    // Staged in the work folder (outRoot is <out>/.work/out): nothing outside
    // the tool's own folders is created or deleted.
    const fs::path work = workDir.empty() ? outRoot.parent_path() : workDir;
    const fs::path staging = work / "game.new";
    const fs::path previous = work / "game.old";
    std::error_code ec;
    if (!RemoveTree(staging, error)) return false;
    fs::create_directories(staging, ec);
    std::map<std::string, std::string> hashes;
    std::map<std::string, std::uintmax_t> sizes;
    for (const ShippedFile& f : files) {
        const fs::path dst = staging / U8(f.to);
        fs::create_directories(dst.parent_path(), ec);
        if (!fs::copy_file(outRoot / U8(f.from), dst, fs::copy_options::overwrite_existing, ec)) {
            error = "cannot copy " + f.from + ": " + ec.message();
            return false;
        }
        hashes[f.to] = Sha256Of(dst);
        sizes[f.to] = fs::file_size(dst, ec);
    }
    std::ofstream m(staging / "manifest.json", std::ios::binary);
    m << "{\n \"format\": " << assets::kGameDataFormat << ",\n \"tool\": " << JsonString(kToolVersion)
      << ",\n \"game\": " << JsonString(gameVersion) << ",\n \"files\": {";
    bool first = true;
    for (const auto& [name, hash] : hashes) {
        m << (first ? "\n" : ",\n") << "  " << JsonString(name) << ": \"" << hash << "\"";
        first = false;
    }
    // Sizes let the port tell a copy that stopped part way (app/data_check.h).
    m << "\n },\n \"sizes\": {";
    first = true;
    for (const auto& [name, size] : sizes) {
        m << (first ? "\n" : ",\n") << "  " << JsonString(name) << ": " << size;
        first = false;
    }
    m << "\n }\n}\n";
    m.close();
    if (!m) {
        error = "cannot write the manifest";
        return false;
    }
    // The old game/ moves aside first, so a failed swap leaves it whole.
    if (!RemoveTree(previous, error)) return false;
    if (fs::exists(gameOut, ec)) {
        fs::rename(gameOut, previous, ec);
        if (ec) {
            error = "cannot move the old data aside (is a file in it open?): " + ec.message();
            return false;
        }
    }
    fs::create_directories(gameOut.parent_path(), ec);
    fs::rename(staging, gameOut, ec);
    if (ec) {
        error = "cannot move the data into place: " + ec.message();
        std::error_code back;
        if (fs::exists(previous, back)) fs::rename(previous, gameOut, back);
        return false;
    }
    return RemoveTree(previous, error);
}

}  // namespace extract
