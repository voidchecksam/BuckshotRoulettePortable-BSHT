#include "extract/exporters.h"
#include "extract/pipeline.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

using extract::ExporterRun;

// Index of the first run of `script` writing `file` (any file when empty),
// searching from `from`.
int Find(const std::vector<ExporterRun>& runs, const std::string& script, const std::string& file = "",
         size_t from = 0) {
    for (size_t i = from; i < runs.size(); ++i) {
        if (runs[i].script != "res://tools/godot/" + script) continue;
        if (file.empty()) return static_cast<int>(i);
        for (const std::string& w : runs[i].writes) {
            if (w == file) return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

// Each exporter runs after the files it reads exist. dialogue reads
// scene_strings, main's ui_layout, the item exports and the labels' authored
// text; fonts reads every dialogue table; labels3d adopts the runtime text
// fonts records, so it runs again after fonts.
TEST(Pipeline, ExportersRunAfterTheFilesTheyRead) {
    const auto runs = extract::ExporterRuns({"instances/item_beer can.tscn"});
    const int dialogue = Find(runs, "export_dialogue.gd");
    const int fonts = Find(runs, "export_fonts.gd");
    const int labelsFirst = Find(runs, "export_labels3d.gd");
    ASSERT_GE(dialogue, 0);
    ASSERT_GE(fonts, 0);
    ASSERT_GE(labelsFirst, 0);

    EXPECT_LT(Find(runs, "export_scene_strings.gd"), dialogue);
    EXPECT_LT(Find(runs, "export_ui.gd", "ui_layout.json"), dialogue);
    EXPECT_LT(Find(runs, "export_runtime_scene.gd", "runtime_item_beer_can.json"), dialogue);
    EXPECT_LT(Find(runs, "export_scene_graph.gd", "scene_graph.json"), labelsFirst);
    EXPECT_LT(labelsFirst, dialogue);
    EXPECT_LT(dialogue, fonts);
    EXPECT_GT(Find(runs, "export_labels3d.gd", "", static_cast<size_t>(fonts)), fonts);
    EXPECT_LT(Find(runs, "export_runtime_scene.gd", "runtime_main.json"), Find(runs, "export_script_textures.gd"));
}

// v2.2 ships its imported models in Godot 4.3's format, which 4.1 cannot load,
// so the model library is written from the meshes the scenes carry.
TEST(Pipeline, TheModelLibraryComesFromTheScenes) {
    const auto runs = extract::ExporterRuns({});
    EXPECT_GE(Find(runs, "export_scene_meshes.gd", "models/_mesh_names.json"), 0);
    EXPECT_EQ(Find(runs, "export_models.gd"), -1);
    EXPECT_EQ(Find(runs, "export_mesh_names.gd"), -1);
}

// Each run's outputs are cleared before it starts, so a run that exits 0
// without writing is caught. The second labels3d run would otherwise pass on
// the first run's file.
TEST(Pipeline, ARunsOutputsAreClearedBeforeItStarts) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(BSHT_SOURCE_DIR) / ".scratch" / "test_pipeline" / "clear";
    fs::create_directories(dir / "models");
    std::ofstream(dir / "labels3d.json") << "{}";
    std::ofstream(dir / "models" / "a.glb") << "x";
    const extract::ExporterRun run{"res://tools/godot/export_labels3d.gd", {}, {"labels3d.json", "models/"}};
    std::string error;

    ASSERT_TRUE(extract::ClearOutputs(run, dir, error)) << error;

    EXPECT_FALSE(fs::exists(dir / "labels3d.json"));
    EXPECT_FALSE(fs::exists(dir / "models"));
    EXPECT_EQ(extract::MissingOutputs(run, dir).size(), 2u);
}

// export_script_textures.gd only reads runtime_main.json; as its declared
// output, a run that saved nothing would still pass the check.
TEST(Pipeline, NoRunDeclaresAFileItOnlyReads) {
    const auto runs = extract::ExporterRuns({});
    EXPECT_EQ(Find(runs, "export_script_textures.gd", "runtime_main.json"), -1);
    EXPECT_GE(Find(runs, "export_script_textures.gd"), 0);
}

TEST(Pipeline, ExportStageChecksEachExporterWroteItsFiles) {
    extract::ExporterRun run{"res://tools/godot/export_input_map.gd", {}, {"input_map.json"}};
    const auto missing = extract::MissingOutputs(run, std::filesystem::path(BSHT_SOURCE_DIR) / ".scratch" / "empty_out");
    ASSERT_EQ(missing.size(), 1u);
    EXPECT_EQ(missing[0], "input_map.json");
}

// bake_audio adds sounds in the order of its list. The repo's audio/_files.txt
// is one list in byte order, .ogg and .wav together, so the bank only matches
// with the same order.
TEST(Pipeline, TheSoundListIsSortedByteForByte) {
    const auto list = extract::SoundList({"b.wav", "z.ogg", "a b.ogg", "a.wav", "notes.txt", "a_b.ogg"});
    EXPECT_EQ(list, (std::vector<std::string>{"a b.ogg", "a.wav", "a_b.ogg", "b.wav", "z.ogg"}));
}

TEST(Pipeline, MissingExporterFilesAreNamed) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(BSHT_SOURCE_DIR) / ".scratch" / "test_pipeline" / "exporters";
    fs::remove_all(dir);
    fs::create_directories(dir);
    for (const std::string& f : extract::ExporterFiles(true)) std::ofstream(dir / f) << "x";
    EXPECT_TRUE(extract::MissingExporters(dir, true).empty());

    fs::remove(dir / "export_ui.gd");
    fs::remove(dir / "steam_stub.gd");
    EXPECT_EQ(extract::MissingExporters(dir, true), (std::vector<std::string>{"export_ui.gd", "steam_stub.gd"}));
    // A dev project loads GodotSteam itself and needs no stub.
    EXPECT_EQ(extract::MissingExporters(dir, false), (std::vector<std::string>{"export_ui.gd"}));
}
