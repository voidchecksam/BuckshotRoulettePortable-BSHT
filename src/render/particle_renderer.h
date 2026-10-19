// Draws godot::ParticleSim's quads: camera-facing, depth-tested, not
// depth-writing, alpha-blended, with the Label3D shader (texture x colour).
// One texture: every system in the game draws textures/smoke.png, baked to
// a raw RGBA file by tools/godot/export_particle_texture.gd.
#pragma once

#include "assets/light_model.h"
#include "assets/probe_grid.h"
#include "core/mat4.h"
#include "godot/particle_sim.h"
#include "render/shader.h"
#include "render/sprite_batch.h"

#include <cstdint>
#include <string>
#include <vector>

namespace render {

class ParticleRenderer {
public:
    ~ParticleRenderer();
    // Needs a GL context. False when the texture file cannot be read: then
    // nothing is drawn.
    bool Init(const std::string& texturePath);
    void Destroy();
    void Bind(const godot::SceneTree& tree, const assets::ProbeGrid* probes);
    // Re-resolves after a free (a freed system leaves the list).
    void Rebind(const godot::SceneTree& tree) { sim_.Bind(tree); }
    // `lights`: the live animated lights (the zippo's flame lights its own
    // flame and the cigarette's smoke), added per quad to the probe light.
    void Draw(const core::Mat4& view, const core::Mat4& viewProj,
              const assets::light::PointLight* lights = nullptr, int lightCount = 0);
    int DrawnQuads() const { return drawn_; }
    // "path n, path n" for every system with live quads this frame, so the log
    // names which smoke drew.
    std::string LiveSummary() const;
    // One bit per system (the first 64) with live quads this frame: the log
    // reports when this set changes.
    uint64_t LiveMask() const;

    // At most this many quads a frame: smoke is many large, faint, blended
    // quads, and fill is the Vita's budget. The farthest are dropped first.
    static constexpr int kMaxQuads = 192;

private:
    struct Vertex {
        float pos[3];
        float uv[2];
        uint8_t col[4];
    };
    godot::ParticleSim sim_;
    const assets::ProbeGrid* probes_ = nullptr;
    Shader shader_;
    SpriteTexture texture_;
    uint32_t vbo_ = 0;
    int uMvp_ = -1, uTex_ = -1;
    std::vector<Vertex> verts_;
    int drawn_ = 0;
};

}  // namespace render
