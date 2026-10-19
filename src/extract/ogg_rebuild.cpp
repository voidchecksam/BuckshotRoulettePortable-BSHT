#include "extract/ogg_rebuild.h"

#include <ogg/ogg.h>

#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

namespace extract {

namespace {

constexpr uint32_t kDumpVersion = 1;
constexpr uint32_t kMaxPacket = 64u << 20;
constexpr uint64_t kMinPage = 8 + 4;   // granule + packet count

bool Read(std::ifstream& f, void* dst, size_t n) {
    f.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
    return static_cast<bool>(f);
}

template <typename T>
bool ReadLe(std::ifstream& f, T& v) {
    uint8_t b[sizeof(T)];
    if (!Read(f, b, sizeof b)) return false;
    uint64_t x = 0;
    for (size_t i = 0; i < sizeof(T); ++i) x |= uint64_t(b[i]) << (8 * i);
    std::memcpy(&v, &x, sizeof(T));
    return true;
}

bool ReadPages(std::ifstream& f, uint64_t fileSize, PacketDump& out) {
    uint32_t pages = 0;
    if (!ReadLe(f, pages) || pages > fileSize / kMinPage) return false;
    out.pages.resize(pages);
    for (OggPage& page : out.pages) {
        uint32_t packets = 0;
        if (!ReadLe(f, page.granule) || !ReadLe(f, packets) || packets > fileSize / 4) return false;
        page.packets.resize(packets);
        for (auto& p : page.packets) {
            uint32_t size = 0;
            if (!ReadLe(f, size) || size > kMaxPacket || size > fileSize) return false;
            p.resize(size);
            if (!Read(f, p.data(), size)) return false;
        }
    }
    return true;
}

void Append(std::vector<uint8_t>& out, const ogg_page& p) {
    out.insert(out.end(), p.header, p.header + p.header_len);
    out.insert(out.end(), p.body, p.body + p.body_len);
}

std::string Utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

}  // namespace

bool ReadPacketDump(const fs::path& file, PacketDump& out, std::string& error) {
    out = PacketDump{};
    std::error_code ec;
    const uint64_t fileSize = fs::file_size(file, ec);
    std::ifstream f(file, std::ios::binary);
    char magic[4];
    uint32_t version = 0;
    if (ec || !f || !Read(f, magic, 4) || std::string(magic, 4) != "BSPK" || !ReadLe(f, version) ||
        version != kDumpVersion || !ReadLe(f, out.samplingRate)) {
        error = Utf8(file.filename()) + ": not a packet dump";
        return false;
    }
    if (!ReadPages(f, fileSize, out)) {
        error = Utf8(file.filename()) + ": packet dump is cut short or damaged";
        return false;
    }
    return true;
}

std::vector<uint8_t> WriteOgg(const PacketDump& dump, uint32_t serial) {
    std::vector<uint8_t> out;
    ogg_stream_state os;
    ogg_stream_init(&os, static_cast<int>(serial));
    int64_t packetno = 0;
    size_t total = 0;
    for (const OggPage& p : dump.pages) total += p.packets.size();
    for (const OggPage& page : dump.pages) {
        for (size_t k = 0; k < page.packets.size(); ++k) {
            ogg_packet op = {};
            op.packet = const_cast<unsigned char*>(page.packets[k].data());
            op.bytes = static_cast<long>(page.packets[k].size());
            op.b_o_s = packetno == 0;
            op.e_o_s = packetno + 1 == static_cast<int64_t>(total);
            op.granulepos = k + 1 == page.packets.size() ? page.granule : -1;
            op.packetno = packetno++;
            ogg_stream_packetin(&os, &op);
        }
        // One page per stored page, so the page boundaries and granules match
        // the original file.
        ogg_page og;
        while (ogg_stream_flush(&os, &og) != 0) Append(out, og);
    }
    ogg_stream_clear(&os);
    return out;
}

bool RebuildOggFiles(const fs::path& dir, size_t& count, std::string& error) {
    count = 0;
    std::vector<fs::path> dumps;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == ".oggpk") dumps.push_back(it->path());
    }
    if (ec) {
        error = "cannot list " + Utf8(dir) + ": " + ec.message();
        return false;
    }
    for (const fs::path& p : dumps) {
        PacketDump d;
        if (!ReadPacketDump(p, d, error)) return false;
        fs::path ogg = p;
        ogg.replace_extension(".ogg");
        const std::vector<uint8_t> bytes = WriteOgg(d);
        std::ofstream o(ogg, std::ios::binary | std::ios::trunc);
        o.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!o) {
            error = "cannot write " + Utf8(ogg);
            return false;
        }
        o.close();
        fs::remove(p, ec);
        ++count;
    }
    return true;
}

}  // namespace extract
