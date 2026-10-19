// The room: node hierarchy, transforms and the OmniLights.
//
// Reads assets/extracted/scene_graph.json. Headless (no GL, no SDL), so it is
// unit-testable and compiles for arm-vita-eabi unchanged.
//
// Two properties of the exported data worth knowing before reading the code:
//
//   1. There is no parent field. Hierarchy is carried in `path`, so the parent
//      of "a/b/c" is "a/b" and the root's path is ".". Parents are resolved by
//      path lookup, so a node whose parent path is absent is a root rather
//      than an error.
//
//   2. Only spatial nodes carry a transform. Control, Label, Button and plain
//      Node compose as identity, so a MeshInstance3D parented under one still
//      lands in the right place.
//
// Transforms are local, not global ("Camera/blood splatter plane" sits near
// the origin while "Camera" sits at (-162, 52, -90)), so world transforms are
// composed down the chain.
#pragma once

#include "core/mat4.h"

#include <string>
#include <vector>

namespace assets {

struct SceneNode {
    std::string name;
    std::string path;
    std::string type;

    // ArrayMesh resource_name, e.g. "backroom blockout6_Cylinder_020". Empty
    // when the node has no mesh. A bare "RID(...)" marks an unnamed inline
    // primitive (PlaneMesh, BoxMesh...) with no counterpart in the extracted
    // model library. See IsInlinePrimitive().
    std::string meshId;

    int  parent   = -1;      // index into Scene::nodes, -1 for a root
    int  surfaces = 0;
    bool visible  = true;

    // The node's own flag and every ancestor's. This is what "on screen" means,
    // and it is not `visible`: many own-visible MeshInstance3Ds sit under a
    // hidden parent, among them the item grid indicators, which would draw as
    // opaque white quads across the table.
    bool visibleInTree = true;
    bool skinned  = false;
    bool hasTransform = false;

    core::Mat4 local;        // identity when hasTransform is false

    bool HasMesh() const { return !meshId.empty(); }

    // True when the mesh is a Godot runtime RID with no resource name, i.e. one
    // of the scene-authored inline primitives rather than an extracted model.
    bool IsInlinePrimitive() const {
        return meshId.rfind("RID(", 0) == 0;
    }
};

struct OmniLight {
    std::string path;
    float colour[3] = {1.0f, 1.0f, 1.0f};
    float energy    = 1.0f;
    float range     = 1.0f;

    // OmniLight3D.attenuation, the decay exponent. All 29 of the game's lights
    // use 1.0 (plain inverse-distance). 0 means the geometric term alone.
    float attenuation = 0.0f;
    // Local, not global: the exporter writes n.transform, not
    // n.global_transform, so never read a world position off this. Lights are
    // parented deep (e.g. "tabletop parent/main tabletop/compartments parent/
    // light_item in hand"), so the local origin is metres from the truth.
    // Scene::LightWorldTransform() resolves the light's path through the node
    // table and composes properly.
    core::Mat4 local;

    // Index of this light's own node, resolved at load; -1 if the path is not
    // in the node table.
    int node = -1;
};

class Scene {
public:
    const std::vector<SceneNode>& Nodes()  const { return nodes_; }
    const std::vector<OmniLight>& Lights() const { return lights_; }

    std::size_t NodeCount()  const { return nodes_.size(); }
    std::size_t LightCount() const { return lights_.size(); }

    // Composed down the parent chain. Precomputed at load, so this is a lookup.
    const core::Mat4& WorldTransform(int index) const;

    // World transform of light `i`, composed through its parent chain. Falls
    // back to the light's local transform only if its path is missing from the
    // node table.
    const core::Mat4& LightWorldTransform(int i) const;

    // -1 when absent.
    int FindByPath(const char* path) const;

    bool LoadFromString(const char* json);
    bool LoadFromFile(const char* path);

private:
    void BuildHierarchy();

    std::vector<SceneNode>  nodes_;
    std::vector<OmniLight>  lights_;
    std::vector<core::Mat4> world_;
    core::Mat4              identity_;
};

}  // namespace assets
