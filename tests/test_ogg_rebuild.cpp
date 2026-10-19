#include "extract/ogg_rebuild.h"

#include <gtest/gtest.h>
#include <ogg/ogg.h>

#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;

namespace {

// Reads an .ogg back into pages of packets with libogg's own parser.
extract::PacketDump Parse(const std::vector<uint8_t>& bytes) {
    extract::PacketDump d;
    ogg_sync_state sync;
    ogg_sync_init(&sync);
    char* buf = ogg_sync_buffer(&sync, static_cast<long>(bytes.size()));
    std::copy(bytes.begin(), bytes.end(), buf);
    ogg_sync_wrote(&sync, static_cast<long>(bytes.size()));
    ogg_stream_state os;
    bool started = false;
    ogg_page page;
    while (ogg_sync_pageout(&sync, &page) == 1) {
        if (!started) {
            ogg_stream_init(&os, ogg_page_serialno(&page));
            started = true;
        }
        ogg_stream_pagein(&os, &page);
        extract::OggPage out;
        out.granule = ogg_page_granulepos(&page);
        ogg_packet p;
        while (ogg_stream_packetout(&os, &p) == 1) out.packets.emplace_back(p.packet, p.packet + p.bytes);
        d.pages.push_back(std::move(out));
    }
    if (started) ogg_stream_clear(&os);
    ogg_sync_clear(&sync);
    return d;
}

}  // namespace

TEST(OggRebuild, PacketsAndGranulesSurviveTheRoundTrip) {
    extract::PacketDump d;
    d.pages = {{0, {{1, 2, 3}}}, {0, {{4, 5}, {6}}}, {4096, {std::vector<uint8_t>(70000, 7)}}, {8000, {{8}}}};

    const extract::PacketDump back = Parse(extract::WriteOgg(d));

    std::vector<std::vector<uint8_t>> want, got;
    for (const auto& p : d.pages) want.insert(want.end(), p.packets.begin(), p.packets.end());
    for (const auto& p : back.pages) got.insert(got.end(), p.packets.begin(), p.packets.end());
    EXPECT_EQ(got, want);
    ASSERT_FALSE(back.pages.empty());
    EXPECT_EQ(back.pages.back().granule, 8000);
}

TEST(OggRebuild, ADamagedDumpIsRefused) {
    const fs::path dir = fs::path(BSHT_SOURCE_DIR) / ".scratch" / "test_ogg_rebuild";
    fs::create_directories(dir);
    std::ofstream(dir / "cut.oggpk", std::ios::binary) << "BSPK\x01\x00\x00\x00";
    extract::PacketDump d;
    std::string error;
    EXPECT_FALSE(extract::ReadPacketDump(dir / "cut.oggpk", d, error));
    EXPECT_NE(error.find("cut.oggpk"), std::string::npos);
}
