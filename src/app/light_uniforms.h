// The animated lights' uniforms.
//
// Four OmniLight3Ds sit at energy 0 at rest and are driven by animation: the
// muzzle flash, the barrel mask, the shotgun inspect mask and the zippo. The
// bake stored each one's scalar per vertex at energy 1 (a_col's channels, in
// that order); here each frame's energy and colour become u_dynN, and the DYN
// program is bound only while one of them burns. Headless: reads godot:: only.
#pragma once

#include "assets/light_model.h"
#include "assets/scene_fog.h"

#include <string>
#include <cstdint>

namespace godot {
class OmniLight3D;
class SceneTree;
class WorldEnvironment;
}  // namespace godot

namespace app {

// The display-space gain k in u_dynN = srgb_to_linear(colour) * energy * k,
// fitted with the flash light at its baked pose, energy 20, frame 783: felt
// brightness added vs Godot gave k 1.5 / 2.0 / 4.0 across three felt regions
// (Godot adds a near-uniform +0.32; the port falls off faster). Median.
inline constexpr float kDynGain = 2.0f;

class DynLights {
public:
    // Finds the four lights by path; a missing one stays dark. The paths are
    // the bake's channel order (tools/bake_room.cpp, "the animated lights").
    void Bind(godot::SceneTree& tree);
    int Bound() const;
    // Fills u_dyn0..3 for gain `k`; true when any of them burns (a light
    // counts only while visible in the tree, as Godot draws it).
    bool Uniforms(float k, float out[4][3]) const;
    // The burning ones as live point lights (particles are lit by them): how
    // many were written to `out`.
    int Lights(assets::light::PointLight out[4]) const;
    // "<name> <energy>, ..." for the burning ones, for the log ("" when none).
    std::string Burning() const;

private:
    // Re-finds the lights by path once the tree has freed nodes since.
    void Refresh() const;
    const godot::SceneTree* tree_ = nullptr;
    mutable uint64_t generation_ = 0;
    mutable const godot::OmniLight3D* lights_[4] = {nullptr, nullptr, nullptr, nullptr};
};

// The Environment's animated brightness and contrast as the surface shader's
// u_scale / u_offset. Saturation is graded in at rest and no animation moves
// it; a change is reported, not applied.
// The light an animation moves (assets::kMovingLight): its live position and
// display-space colour, while the camera is near enough to see what it lights.
// The MOVE program costs vertex work, so it is not bound at the table.
class MovingLight {
public:
    void Bind(godot::SceneTree& tree);
    // False when the light is missing, off, or the camera is far from it.
    bool Uniforms(const float eye[3], float k, float pos[3], float* range, float colour[3]) const;

private:
    void Refresh() const;
    const godot::SceneTree* tree_ = nullptr;
    mutable uint64_t generation_ = 0;
    mutable const godot::OmniLight3D* light_ = nullptr;
};

class EnvironmentGrade {
public:
    void Bind(godot::SceneTree& tree);
    // False when the scene has no WorldEnvironment: the caller keeps (1, 0).
    bool Uniforms(float* scale, float* offset) const;
    // The live brightness and contrast: a prop baked at its own grade is graded
    // against that grade, not the scene's rest. False with no WorldEnvironment.
    bool Live(float* brightness, float* contrast) const;
    // True once the saturation has been seen away from its rest value.
    bool SaturationMoved() const { return saturationMoved_; }

    // Environment fog. `colour` is the fog's linear colour x energy put through
    // the grade the pages were baked with (GradePixel), so the shader mixes it
    // in display space; `clear` the background with fog_sky_affect of the fog
    // over it (sky.glsl's fog-only pass).
    struct Fog {
        float density = 0.0f;
        float colour[3] = {0.0f, 0.0f, 0.0f};
        float clear[3] = {0.0f, 0.0f, 0.0f};
        assets::FogMix mix;   // the FOG program's linear-light mix (assets/scene_fog.h)
    };
    // False when the scene has no WorldEnvironment or its fog is off.
    bool FogUniforms(Fog* out) const;

private:
    void Refresh() const;   // as DynLights::Refresh
    const godot::SceneTree* tree_ = nullptr;
    mutable uint64_t generation_ = 0;
    mutable const godot::WorldEnvironment* env_ = nullptr;
    mutable bool saturationMoved_ = false;
    // The Environment at bind: the grade the scene's pages were baked with.
    float restBrightness_ = 1.49f;
    float restContrast_ = 0.8f;
};

}  // namespace app
