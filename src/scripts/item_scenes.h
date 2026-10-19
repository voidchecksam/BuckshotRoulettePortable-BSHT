// The item scenes ItemManager deals, and the files they are exported to.
//
// Items are not main.tscn geometry: each is its own packed scene,
// instances/item_<name>.tscn, which ItemManager.GrabItem instantiates. So each
// goes through the same export, bake and load path as the shell, and three
// places have to agree on which scenes and which files:
//
//   bsht-extract (extract::ExporterRuns)   writes   runtime_item_<name>.json
//   tools/bake_room.cpp                    bakes    their meshes as template props
//   app::GameApp                           loads    them before the scripts are bound
//
// The list comes from ItemManager's own exports rather than a table here, so
// it is the scene's list, not ours.
#pragma once

#include "godot/node.h"
#include "scripts/bind_support.h"

#include <algorithm>
#include <string>
#include <vector>

namespace scripts {

// Every distinct res:// scene in ItemManager's instanceArray and
// instanceArray_dealer, in first-seen order. Both arrays name the same nine
// scenes today; collecting from both means neither can hold one the other lacks.
inline std::vector<std::string> ItemScenePaths(const godot::Node& itemManager) {
    std::vector<std::string> out;
    const ExportReader ex(itemManager.get_exports_json());
    for (const char* key : {"instanceArray", "instanceArray_dealer"}) {
        for (const ExportReader& resource : ex.resource_array(key)) {
            const std::string path = resource.res_path("instance");
            if (!path.empty() && std::find(out.begin(), out.end(), path) == out.end()) {
                out.push_back(path);
            }
        }
    }
    return out;
}

// res://instances/item_beer can.tscn -> runtime_item_beer_can.json.
// Must agree with extract::RuntimeExportName, which names these files: a space
// in the scene name becomes an underscore in the file name only, and the
// res:// path keeps its space because that is what ItemResource records.
inline std::string ItemSceneExportFile(const std::string& resPath) {
    std::string stem = resPath.substr(resPath.find_last_of('/') + 1);
    stem = stem.substr(0, stem.rfind(".tscn"));
    std::replace(stem.begin(), stem.end(), ' ', '_');
    return "runtime_" + stem + ".json";
}

}  // namespace scripts
