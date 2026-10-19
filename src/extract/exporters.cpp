#include "extract/exporters.h"

#include "extract/remove_tree.h"

#include <algorithm>

namespace fs = std::filesystem;

namespace extract {

namespace {

std::string Script(const char* name) { return std::string("res://tools/godot/") + name; }

// The three scenes the port has, as SceneArgs takes them.
struct Scene {
    std::vector<std::string> args;
    const char* suffix;
};
const Scene kScenes[] = {{{}, ""}, {{"scenes/menu.tscn", "menu"}, "_menu"}, {{"scenes/heaven.tscn", "heaven"}, "_heaven"}};

constexpr const char* kLocales[] = {"en", "br", "de", "ee", "es_latam", "es", "fr", "it", "ja",
                                    "ko", "pl", "pt", "ru", "tr", "ua", "zhs", "zht"};

}  // namespace

std::string RuntimeExportName(const std::string& itemScene) {
    std::string stem = itemScene.substr(itemScene.find_last_of('/') + 1);
    stem = stem.substr(0, stem.rfind(".tscn"));
    std::replace(stem.begin(), stem.end(), ' ', '_');
    return "runtime_" + stem + ".json";
}

// Ordered so each run finds what it reads:
//   labels3d       scene_graph.json (and ui_fonts.json on its second run)
//   dialogue       scene_strings.json, ui_layout.json, runtime_item_*.json,
//                  the labels' authored text
//   fonts          every dialogue_<code>.json
//   script_textures runtime_main.json
std::vector<ExporterRun> ExporterRuns(const std::vector<std::string>& itemScenes) {
    std::vector<ExporterRun> r;
    for (const Scene& s : kScenes) {
        r.push_back({Script("export_scene_graph.gd"), s.args, {std::string("scene_graph") + s.suffix + ".json"}});
    }
    r.push_back({Script("export_scene_meshes.gd"), {}, {"models/", "models/_mesh_names.json", "models/_manifest.json"}});
    for (const Scene& s : kScenes) {
        r.push_back({Script("export_animations.gd"), s.args, {std::string("animations") + s.suffix + ".json"}});
        r.push_back({Script("export_inline_meshes.gd"), s.args, {std::string("inline_meshes") + s.suffix + ".json"}});
        r.push_back({Script("export_ui.gd"), s.args,
                     {std::string("ui_layout") + s.suffix + ".json", std::string("ui_textures") + s.suffix + ".bin"}});
    }
    r.push_back({Script("export_scene_strings.gd"), {}, {"scene_strings.json"}});
    r.push_back({Script("export_runtime_scene.gd"), {}, {"runtime_main.json"}});
    r.push_back({Script("export_runtime_scene.gd"), {"scenes/menu.tscn", "runtime_menu.json"}, {"runtime_menu.json"}});
    r.push_back({Script("export_runtime_scene.gd"), {"scenes/heaven.tscn", "runtime_heaven.json"}, {"runtime_heaven.json"}});
    r.push_back({Script("export_runtime_scene.gd"), {"instances/shell.tscn", "runtime_shell.json"}, {"runtime_shell.json"}});
    for (const std::string& item : itemScenes) {
        const std::string out = RuntimeExportName(item);
        r.push_back({Script("export_runtime_scene.gd"), {item, out}, {out}});
    }
    // Its PNGs land in inline_textures/ beside other exporters' files; the
    // script quits with an error when it saves none.
    r.push_back({Script("export_script_textures.gd"), {}, {}});
    r.push_back({Script("export_labels3d.gd"), {}, {"labels3d.json"}});
    ExporterRun dialogue{Script("export_dialogue.gd"), {}, {}};
    for (const char* code : kLocales) dialogue.writes.push_back(std::string("dialogue_") + code + ".json");
    r.push_back(dialogue);
    r.push_back({Script("export_fonts.gd"), {}, {"ui_fonts.json", "ui_font_atlas.bin"}});
    r.push_back({Script("export_labels3d.gd"), {}, {"labels3d.json"}});
    r.push_back({Script("export_round_table.gd"), {}, {"round_table.json"}});
    r.push_back({Script("export_audio_streams.gd"), {}, {"audio_streams.json"}});
    r.push_back({Script("export_input_map.gd"), {}, {"input_map.json"}});
    r.push_back({Script("export_particle_texture.gd"), {}, {"particle_smoke.rgba"}});
    r.push_back({Script("export_audio_packets.gd"), {}, {"audio/"}});
    r.push_back({Script("export_cursors.gd"), {}, {"cursors/cursors.json"}});
    return r;
}

std::vector<std::string> ExporterFiles(bool unpackedGame) {
    std::vector<std::string> files = {"scene_args.gd", "texture_export.gd", "steam_off.gd"};
    if (unpackedGame) {
        files.push_back("steam_stub.gd");
        files.push_back("pipeline_manager.gd");
    }
    for (const ExporterRun& r : ExporterRuns({})) files.push_back(r.script.substr(r.script.find_last_of('/') + 1));
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

std::vector<std::string> MissingExporters(const fs::path& dir, bool unpackedGame) {
    std::vector<std::string> missing;
    std::error_code ec;
    for (const std::string& f : ExporterFiles(unpackedGame)) {
        if (!fs::is_regular_file(dir / f, ec)) missing.push_back(f);
    }
    return missing;
}

bool ClearOutputs(const ExporterRun& run, const fs::path& outRoot, std::string& error) {
    for (const std::string& w : run.writes) {
        if (!RemoveTree(outRoot / fs::path(std::u8string(w.begin(), w.end())), error)) return false;
    }
    return true;
}

std::vector<std::string> MissingOutputs(const ExporterRun& run, const fs::path& outRoot) {
    std::vector<std::string> missing;
    std::error_code ec;
    for (const std::string& w : run.writes) {
        const fs::path p = outRoot / fs::path(std::u8string(w.begin(), w.end()));
        const bool ok = w.back() == '/' ? fs::is_directory(p, ec) && !fs::is_empty(p, ec)
                                        : fs::is_regular_file(p, ec) && fs::file_size(p, ec) > 0;
        if (!ok) missing.push_back(w);
    }
    return missing;
}

}  // namespace extract
