// bsht-extract's stages, in order: check, unpack, export, convert, bake,
// package. Each logs a line when it starts and when it ends.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace extract {

struct Options {
    std::string game;                    // what the player pointed at (UTF-8)
    std::filesystem::path out = "bsht-data";
    std::filesystem::path project;       // dev: an already unpacked project
    std::filesystem::path unpackTo;      // dev: unpack the game here for Godot references, then stop
    std::filesystem::path godot;         // the Godot 4.1.4 binary
    std::filesystem::path exporters;     // tools/godot
    bool keepWork = false;
};

int RunPipeline(const Options& o);

// The sounds bake_audio reads, from the file names in the audio folder: the
// .ogg and .wav files in byte order, as `ls` wrote the repo's list.
std::vector<std::string> SoundList(std::vector<std::string> names);

}  // namespace extract
