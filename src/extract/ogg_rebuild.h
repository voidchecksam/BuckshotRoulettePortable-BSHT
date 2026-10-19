// Rebuilds .ogg files from the pages Godot keeps for an imported Ogg Vorbis
// stream (OggPacketSequence: per page, the packets that end on it, and the
// page's granule position). libogg writes the pages.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace extract {

struct OggPage {
    int64_t granule = 0;
    std::vector<std::vector<uint8_t>> packets;
};

struct PacketDump {
    float samplingRate = 0;
    std::vector<OggPage> pages;
};

// A .oggpk file as export_audio_packets.gd writes it.
bool ReadPacketDump(const std::filesystem::path& file, PacketDump& out, std::string& error);
std::vector<uint8_t> WriteOgg(const PacketDump& dump, uint32_t serial = 0x42534854);
// Every *.oggpk under `dir` (recursively) becomes the .ogg beside it; the dump is removed.
bool RebuildOggFiles(const std::filesystem::path& dir, size_t& count, std::string& error);

}  // namespace extract
