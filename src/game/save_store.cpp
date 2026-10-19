#include "game/save_store.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

namespace game {
namespace {

constexpr char kMagic[4] = {'B', 'S', 'A', 'V'};
constexpr uint32_t kVersion = 1;
// A string longer than this is not a save this port wrote.
constexpr uint32_t kMaxString = 1u << 20;

enum class Tag : uint8_t { Bool = 0, Int = 1, Float = 2, String = 3 };

void MakeDirectory(const std::string& path) {
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0777);
#endif
}

// Creates every missing component of `dir`. Failures surface later, when the
// temp file cannot be opened.
void MakeDirectories(const std::string& dir) {
    for (size_t i = 1; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == '/' || dir[i] == '\\') {
            const std::string prefix = dir.substr(0, i);
            // "ux0:" is a device, not a directory.
            if (!prefix.empty() && prefix.back() != ':') MakeDirectory(prefix);
        }
    }
}

bool FileExists(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && (info.st_mode & S_IFMT) == S_IFREG;
}

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void PutU64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void PutString(std::vector<uint8_t>& out, const std::string& s) {
    PutU32(out, static_cast<uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

std::vector<uint8_t> Encode(const SaveMap& values) {
    std::vector<uint8_t> out(kMagic, kMagic + 4);
    PutU32(out, kVersion);
    PutU32(out, static_cast<uint32_t>(values.size()));
    for (const auto& [key, value] : values) {
        out.push_back(static_cast<uint8_t>(value.index()));
        PutString(out, key);
        if (const auto* b = std::get_if<bool>(&value)) {
            out.push_back(*b ? 1 : 0);
        } else if (const auto* i = std::get_if<int64_t>(&value)) {
            PutU64(out, static_cast<uint64_t>(*i));
        } else if (const auto* f = std::get_if<double>(&value)) {
            uint64_t bits = 0;
            std::memcpy(&bits, f, sizeof bits);
            PutU64(out, bits);
        } else {
            PutString(out, std::get<std::string>(value));
        }
    }
    return out;
}

class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}

    bool U8(uint8_t& v) {
        if (pos_ + 1 > bytes_.size()) return false;
        v = bytes_[pos_++];
        return true;
    }
    bool U32(uint32_t& v) {
        if (pos_ + 4 > bytes_.size()) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(bytes_[pos_++]) << (8 * i);
        return true;
    }
    bool U64(uint64_t& v) {
        if (pos_ + 8 > bytes_.size()) return false;
        v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(bytes_[pos_++]) << (8 * i);
        return true;
    }
    bool String(std::string& s) {
        uint32_t len = 0;
        if (!U32(len) || len > kMaxString || pos_ + len > bytes_.size()) return false;
        s.assign(reinterpret_cast<const char*>(bytes_.data() + pos_), len);
        pos_ += len;
        return true;
    }
    bool AtEnd() const { return pos_ == bytes_.size(); }

private:
    const std::vector<uint8_t>& bytes_;
    size_t pos_ = 4;  // past the magic, checked by the caller
};

bool Decode(const std::vector<uint8_t>& bytes, SaveMap& out) {
    if (bytes.size() < 12 || std::memcmp(bytes.data(), kMagic, 4) != 0) return false;
    Reader r(bytes);
    uint32_t version = 0, count = 0;
    if (!r.U32(version) || version != kVersion || !r.U32(count)) return false;
    SaveMap values;
    for (uint32_t n = 0; n < count; ++n) {
        uint8_t tag = 0;
        std::string key;
        if (!r.U8(tag) || !r.String(key)) return false;
        switch (static_cast<Tag>(tag)) {
            case Tag::Bool: {
                uint8_t b = 0;
                if (!r.U8(b) || b > 1) return false;
                values[key] = b == 1;
                break;
            }
            case Tag::Int: {
                uint64_t v = 0;
                if (!r.U64(v)) return false;
                values[key] = static_cast<int64_t>(v);
                break;
            }
            case Tag::Float: {
                uint64_t bits = 0;
                if (!r.U64(bits)) return false;
                double d = 0;
                std::memcpy(&d, &bits, sizeof d);
                values[key] = d;
                break;
            }
            case Tag::String: {
                std::string s;
                if (!r.String(s)) return false;
                values[key] = std::move(s);
                break;
            }
            default:
                return false;
        }
    }
    if (!r.AtEnd()) return false;
    out = std::move(values);
    return true;
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& bytes) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    bytes.clear();
    uint8_t chunk[4096];
    size_t got = 0;
    while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0) bytes.insert(bytes.end(), chunk, chunk + got);
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

bool LoadFile(const std::string& path, SaveMap& out) {
    std::vector<uint8_t> bytes;
    return ReadFile(path, bytes) && Decode(bytes, out);
}

}  // namespace

SaveStore::SaveStore(std::string directory) : directory_(std::move(directory)) {}

std::string SaveStore::PathOf(const std::string& name, const char* extension) const {
    return directory_ + "/" + name + extension;
}

bool SaveStore::Load(const std::string& name, SaveMap& out) const {
    out.clear();
    if (LoadFile(PathOf(name, ".sav"), out)) return true;
    // Save() removes the old .sav before its rename where rename will not
    // replace; power lost in between leaves a complete .tmp and no .sav.
    if (!FileExists(PathOf(name, ".sav")) && LoadFile(PathOf(name, ".tmp"), out)) return true;
    out.clear();
    return false;
}

bool SaveStore::Save(const std::string& name, const SaveMap& values) const {
    MakeDirectories(directory_);
    const std::string tmp = PathOf(name, ".tmp");
    const std::string sav = PathOf(name, ".sav");
    const std::vector<uint8_t> bytes = Encode(values);

    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool written = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    const bool closed = std::fclose(f) == 0;
    if (!written || !closed) {
        std::remove(tmp.c_str());
        return false;
    }
    if (std::rename(tmp.c_str(), sav.c_str()) == 0) return true;
    // Windows' and the Vita's rename refuse an existing target. Load() recovers
    // from a .tmp if power is lost between these two calls.
    std::remove(sav.c_str());
    if (std::rename(tmp.c_str(), sav.c_str()) == 0) return true;
    std::remove(tmp.c_str());
    return false;
}

bool SaveStore::Clear(const std::string& name) const {
    std::remove(PathOf(name, ".tmp").c_str());
    std::remove(PathOf(name, ".sav").c_str());
    return !Exists(name);
}

bool SaveStore::Exists(const std::string& name) const {
    return FileExists(PathOf(name, ".sav")) || FileExists(PathOf(name, ".tmp"));
}

}  // namespace game
