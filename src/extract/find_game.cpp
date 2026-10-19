#include "extract/find_game.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace extract {

namespace {

std::string Utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

bool LooksLikeGameFile(const fs::path& p) {
    std::string ext = Utf8(p.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == ".exe" || ext == ".pck" || ext.empty();
}

// The files and folders directly inside `dir`, sorted so the pick does not
// depend on the order the OS lists them in. Errors just end the listing.
void List(const fs::path& dir, std::vector<fs::path>& files, std::vector<fs::path>* subdirs) {
    std::vector<fs::path> f, d;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) f.push_back(it->path());
        else if (it->is_directory(ec)) d.push_back(it->path());
    }
    std::sort(f.begin(), f.end());
    std::sort(d.begin(), d.end());
    files.insert(files.end(), f.begin(), f.end());
    if (subdirs) subdirs->insert(subdirs->end(), d.begin(), d.end());
}

// A candidate file: a pack with a readable, supported version.
std::optional<FoundGame> Check(const fs::path& p, std::vector<std::string>& notes) {
    FoundGame g;
    g.path = Utf8(fs::path(p).make_preferred());
    const PackError err = ReadPack(g.path, g.pack);
    if (err == PackError::NoPack) return std::nullopt;   // not a Godot game; say nothing
    if (err != PackError::None) {
        notes.push_back(g.path + ": " + PackErrorText(err));
        return std::nullopt;
    }
    const PackEntry* gv = FindEntry(g.pack, "res://scripts/GlobalVariables.gd");
    std::vector<uint8_t> text;
    if (!gv || !ReadEntry(g.path, *gv, text)) {
        notes.push_back(g.path + ": a Godot game, but not Buckshot Roulette");
        return std::nullopt;
    }
    const auto version = ParseGameVersion(std::string_view(reinterpret_cast<const char*>(text.data()), text.size()));
    if (!version) {
        notes.push_back(g.path + ": Buckshot Roulette, but its version cannot be read");
        return std::nullopt;
    }
    if (!IsSupported(*version)) {
        notes.push_back(g.path + ": Buckshot Roulette " + Describe(*version) + "; this tool supports " +
                        SupportedVersions());
        return std::nullopt;
    }
    g.version = *version;
    return g;
}

// Absolute, so a bare file name still yields the game's folder for the
// write guard (a relative one has an empty parent).
fs::path Absolute(const std::string& utf8Path) {
    const fs::path p(std::u8string(utf8Path.begin(), utf8Path.end()));
    std::error_code ec;
    const fs::path a = fs::absolute(p, ec);
    return ec ? p : a.lexically_normal();
}

}  // namespace

FindResult FindGame(const std::string& utf8Path) {
    FindResult r;
    const fs::path given = Absolute(utf8Path);
    std::error_code ec;
    if (fs::is_regular_file(given, ec)) {
        r.game = Check(given, r.notes);
        if (!r.game && r.notes.empty()) r.notes.push_back(utf8Path + ": no Godot pack in this file");
        return r;
    }
    if (!fs::is_directory(given, ec)) {
        r.notes.push_back(utf8Path + ": no such file or folder");
        return r;
    }
    std::vector<fs::path> files;
    std::vector<fs::path> subdirs;
    List(given, files, &subdirs);
    for (const fs::path& d : subdirs) List(d, files, nullptr);
    for (const fs::path& f : files) {
        if (!LooksLikeGameFile(f)) continue;
        if (auto g = Check(f, r.notes)) {
            r.game = std::move(g);
            return r;
        }
    }
    if (r.notes.empty()) r.notes.push_back(utf8Path + ": no Buckshot Roulette executable in this folder");
    return r;
}

std::vector<fs::path> FoldersToLeaveAlone(const std::string& utf8Path, const FoundGame& game) {
    std::vector<fs::path> folders = {Absolute(game.path).parent_path()};
    const fs::path given = Absolute(utf8Path);
    std::error_code ec;
    if (fs::is_directory(given, ec)) folders.push_back(given);
    return folders;
}

}  // namespace extract
