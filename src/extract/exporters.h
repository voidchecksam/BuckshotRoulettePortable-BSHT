// The Godot exporter runs, in order, and the files each must leave behind.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace extract {

struct ExporterRun {
    std::string script;                // res:// path
    std::vector<std::string> args;     // before --out=
    std::vector<std::string> writes;   // relative to the output root; "dir/" = a non-empty folder
};

// `itemScenes` as SceneArgs takes them: "instances/item_beer can.tscn".
std::vector<ExporterRun> ExporterRuns(const std::vector<std::string>& itemScenes);
std::vector<std::string> MissingOutputs(const ExporterRun& run, const std::filesystem::path& outRoot);
// Deletes what `run` writes, so MissingOutputs afterwards sees only this run's files.
bool ClearOutputs(const ExporterRun& run, const std::filesystem::path& outRoot, std::string& error);
// Every script the exporter runs need in tools/godot, sorted; the Steam stub
// and the scripts that replace game ones only for an unpacked game.
std::vector<std::string> ExporterFiles(bool unpackedGame);
std::vector<std::string> MissingExporters(const std::filesystem::path& dir, bool unpackedGame);
// "instances/item_beer can.tscn" -> "runtime_item_beer_can.json", as scripts::ItemSceneExportFile.
std::string RuntimeExportName(const std::string& itemScene);

}  // namespace extract
