// Resolves a scene node's mesh_id to the geometry it draws.
//
// scene_graph.json names each MeshInstance3D's mesh by its ArrayMesh
// resource_name ("backroom blockout6_Cylinder_020"). Godot 4.1's GLTFDocument
// does not write mesh names into a .glb, so tools/godot/export_scene_meshes.gd
// writes models/_mesh_names.json beside the files (name -> file and mesh
// index). This reads it.
//
// Over v1.2's main scene: 543 names in 54 models, resolving all 581 named
// MeshInstance3D. The other 76 are unnamed inline primitives (PlaneMesh,
// BoxMesh...) that come from inline_meshes.json instead.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace assets {

struct MeshRef {
    std::string glb;         // file name only, e.g. "backroom_blockout6.glb"
    int meshIndex = -1;      // the glTF mesh index (Mesh::sourceMeshIndex)

    bool Valid() const { return meshIndex >= 0 && !glb.empty(); }
};

class MeshIndex {
public:
    bool LoadFromFile(const char* path);
    bool LoadFromString(const char* json);

    // An invalid MeshRef for an unknown id: an unnamed inline primitive, or a
    // name the library does not have.
    MeshRef Resolve(const std::string& meshId) const;

    std::size_t Size() const { return byName_.size(); }
    std::size_t ModelCount() const { return modelCount_; }

private:
    std::unordered_map<std::string, MeshRef> byName_;
    std::size_t modelCount_ = 0;
};

}  // namespace assets
