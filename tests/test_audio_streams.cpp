// Each stream's length and loop flag, as Godot imported it
// (tools/godot/export_audio_streams.gd -> assets/extracted/audio_streams.json).
// The audio sink reports the length, so a player's is_playing lasts as long as
// its sound; a looping stream never ends on its own.
#include "assets/audio_streams.h"

#include <gtest/gtest.h>

#include <string>

namespace {

TEST(AudioStreams, ReadsLengthAndLoopByFileName) {
    const std::string json =
        R"({"format":1,"streams":[{"name":"car drive ambience.ogg","length":31.5,"loop":true},)"
        R"({"name":"temp gunshot_live.wav","length":1.25,"loop":false}]})";
    assets::AudioStreams streams;
    ASSERT_TRUE(streams.Parse(json));
    const assets::AudioStreamInfo* car = streams.Find("car drive ambience.ogg");
    ASSERT_NE(car, nullptr);
    EXPECT_FLOAT_EQ(car->length, 31.5f);
    EXPECT_TRUE(car->loop);
    const assets::AudioStreamInfo* shot = streams.Find("temp gunshot_live.wav");
    ASSERT_NE(shot, nullptr);
    EXPECT_FALSE(shot->loop);
    EXPECT_EQ(streams.Find("nope.ogg"), nullptr);
    EXPECT_EQ(streams.Size(), 2u);
}

TEST(AudioStreams, AFileWithoutStreamsIsRefused) {
    assets::AudioStreams streams;
    EXPECT_FALSE(streams.Parse("{}"));
    EXPECT_FALSE(streams.Parse("not json"));
    EXPECT_EQ(streams.Size(), 0u);
}

// The real export, when it exists: every long file the bank leaves out has an
// entry, so none of them can stream with an unknown length.
TEST(AudioStreams, TheExportCoversEveryStreamedFile) {
    assets::AudioStreams streams;
    const std::string dir = std::string(BSHT_SOURCE_DIR);
    if (!streams.LoadFromFile((dir + "/assets/extracted/audio_streams.json").c_str())) {
        GTEST_SKIP() << "audio_streams.json not exported";
    }
    std::FILE* f = std::fopen((dir + "/assets/baked/music_list.txt").c_str(), "rb");
    if (!f) GTEST_SKIP() << "music_list.txt not baked";
    char line[512];
    int checked = 0;
    while (std::fgets(line, sizeof line, f)) {
        std::string name(line);
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
        if (name.empty()) continue;
        const assets::AudioStreamInfo* info = streams.Find(name);
        EXPECT_NE(info, nullptr) << name;
        if (info) EXPECT_GT(info->length, 0.0f) << name;
        ++checked;
    }
    std::fclose(f);
    EXPECT_GT(checked, 20);
}

}  // namespace
