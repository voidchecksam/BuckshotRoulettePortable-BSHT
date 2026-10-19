#include "extract/pack_reader.h"

#include <filesystem>
#include <fstream>

namespace extract {

namespace {

constexpr uint32_t kMagic = 0x43504447;   // "GDPC"
constexpr uint32_t kFormat = 2;           // PACK_FORMAT_VERSION in Godot 4.1
constexpr uint32_t kDirEncrypted = 1;
constexpr uint32_t kFileEncrypted = 1;

// Little-endian reads that fail instead of running past the end.
struct Reader {
    std::ifstream& f;
    uint64_t size;
    bool ok = true;

    bool Seek(uint64_t pos) {
        if (pos > size) return ok = false;
        f.clear();
        f.seekg(static_cast<std::streamoff>(pos));
        return true;
    }
    bool Bytes(void* dst, uint64_t n) {
        if (!ok) return false;
        const uint64_t pos = static_cast<uint64_t>(f.tellg());
        if (pos + n > size) return ok = false;
        f.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
        return ok = static_cast<bool>(f);
    }
    uint32_t U32() {
        uint8_t b[4] = {};
        Bytes(b, 4);
        return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
    }
    uint64_t U64() {
        const uint64_t lo = U32();
        return lo | uint64_t(U32()) << 32;
    }
};

std::filesystem::path Utf8(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

}  // namespace

const char* PackErrorText(PackError e) {
    switch (e) {
        case PackError::None: return "ok";
        case PackError::CannotOpen: return "the file cannot be opened";
        case PackError::NoPack: return "the file has no Godot pack in it";
        case PackError::UnsupportedFormat: return "the pack is not Godot 4.1's format";
        case PackError::NewerEngine: return "the pack was made with a newer Godot than 4.1";
        case PackError::Encrypted: return "the pack is encrypted";
        case PackError::Truncated: return "the pack is cut short or damaged";
    }
    return "unknown";
}

PackError ReadPack(const std::string& utf8Path, PackInfo& out) {
    out = PackInfo{};
    std::ifstream f(Utf8(utf8Path), std::ios::binary);
    if (!f) return PackError::CannotOpen;
    f.seekg(0, std::ios::end);
    Reader r{f, static_cast<uint64_t>(f.tellg())};

    uint64_t start = 0;
    r.Seek(0);
    if (r.size < 4 || r.U32() != kMagic) {
        // Appended to an executable: "u64 pack size, u32 magic" at the very end.
        if (r.size < 16) return PackError::NoPack;
        r.ok = true;
        r.Seek(r.size - 4);
        if (r.U32() != kMagic) return PackError::NoPack;
        r.Seek(r.size - 12);
        const uint64_t packSize = r.U64();
        if (packSize > r.size - 12) return PackError::NoPack;
        start = r.size - 12 - packSize;
        r.Seek(start);
        if (r.U32() != kMagic) return PackError::NoPack;
    }

    out.format = r.U32();
    out.major = r.U32();
    out.minor = r.U32();
    out.patch = r.U32();
    if (!r.ok) return PackError::Truncated;
    if (out.format != kFormat) return PackError::UnsupportedFormat;
    if (out.major > 4 || (out.major == 4 && out.minor > 1)) return PackError::NewerEngine;
    const uint32_t flags = r.U32();
    const uint64_t fileBase = r.U64();   // absolute in Godot 4.1, even when embedded
    if (flags & kDirEncrypted) return PackError::Encrypted;
    for (int i = 0; i < 16; ++i) r.U32();   // reserved
    const uint32_t count = r.U32();
    if (!r.ok || fileBase > r.size) return PackError::Truncated;
    // Each entry takes at least 40 bytes; a count that cannot fit is damage.
    constexpr uint64_t kMinEntry = 4 + 8 + 8 + 16 + 4;
    if (count > (r.size - static_cast<uint64_t>(f.tellg())) / kMinEntry) return PackError::Truncated;

    out.entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t len = r.U32();
        if (!r.ok || len > 4096) return PackError::Truncated;
        std::string path(len, '\0');
        r.Bytes(path.data(), len);
        path.resize(path.find('\0') == std::string::npos ? path.size() : path.find('\0'));
        const uint64_t ofs = r.U64();
        const uint64_t size = r.U64();
        uint8_t md5[16];
        r.Bytes(md5, 16);
        const bool encrypted = (r.U32() & kFileEncrypted) != 0;
        // Written so no sum can overflow.
        if (!r.ok || ofs > r.size - fileBase || size > r.size - fileBase - ofs) return PackError::Truncated;
        if (encrypted) return PackError::Encrypted;
        PackEntry e;
        e.path = std::move(path);
        e.offset = fileBase + ofs;
        e.size = size;
        out.entries.push_back(std::move(e));
    }
    return PackError::None;
}

bool ReadEntry(const std::string& utf8Path, const PackEntry& e, std::vector<uint8_t>& out) {
    std::ifstream f(Utf8(utf8Path), std::ios::binary);
    if (!f) return false;
    out.resize(e.size);
    f.seekg(static_cast<std::streamoff>(e.offset));
    f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(e.size));
    return static_cast<bool>(f);
}

const PackEntry* FindEntry(const PackInfo& p, std::string_view resPath) {
    for (const PackEntry& e : p.entries) {
        if (e.path == resPath) return &e;
    }
    return nullptr;
}

}  // namespace extract
