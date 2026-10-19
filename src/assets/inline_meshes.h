// Geometry for the meshes that have no name.
//
// scene_graph.json identifies meshes by resource_name, and the nodes authored
// directly in the scene (PlaneMesh, BoxMesh, CylinderMesh, inline ArrayMesh)
// have none, so nothing in the .glb library joins to them.
//
// The geometry comes from Godot's own tessellation via surface_get_arrays, not
// from re-deriving a plane from its size parameters, so there is no second
// implementation of Godot's primitives to drift from the original, and a
// PlaneMesh and an inline ArrayMesh arrive through one path.
//
// Headless: no GL, no SDL.
#pragma once

#include "assets/glb.h"

#include <string>
#include <vector>

namespace assets {

// The StandardMaterial3D state the bake can honour, per surface. The health
// counter's lightning icons need all three: the texture's alpha is the icon,
// the material is alpha-transparent, and the green is emission
// (main.tscn sub_resource 234).
struct InlineMaterial {
    // Relative to assets/extracted ("inline_textures/x.png"); empty: none.
    std::string textureFile;
    bool  transparent = false;
    float emission[3] = {0.0f, 0.0f, 0.0f};   // emission * energy multiplier
};

struct InlineMesh {
    std::string nodePath;
    std::string className;     // PlaneMesh, BoxMesh, CylinderMesh, ...
    std::vector<Mesh> surfaces;
    std::vector<InlineMaterial> materials;   // parallel to surfaces

    int TriangleCount() const {
        int n = 0;
        for (const Mesh& m : surfaces) n += m.TriangleCount();
        return n;
    }
};

struct InlineMeshStats {
    int nodes     = 0;
    int surfaces  = 0;
    int vertices  = 0;
    int triangles = 0;
};

class InlineMeshLibrary {
public:
    bool LoadFromFile(const char* path);
    bool LoadFromString(const char* json);

    // nullptr when the node has no inline geometry.
    const InlineMesh* Find(const std::string& nodePath) const;

    const std::vector<InlineMesh>& Meshes() const { return meshes_; }
    std::size_t Size() const { return meshes_.size(); }
    const InlineMeshStats& Stats() const { return stats_; }

private:
    std::vector<InlineMesh> meshes_;
    InlineMeshStats stats_;
};

}  // namespace assets
