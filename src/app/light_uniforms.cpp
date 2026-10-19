#include "app/light_uniforms.h"

#include "assets/dyn_lights.h"

#include "assets/light_model.h"
#include "assets/moving_lights.h"
#include "godot/node.h"
#include "godot/scene_tree.h"

#include <cmath>
#include <cstdio>

namespace app {

namespace {

// The bake's channel order: R, G, B, A (assets/dyn_lights.h).
const auto& kPaths = assets::kDynLightPaths;

}  // namespace

std::string DynLights::Burning() const {
    Refresh();
    std::string out;
    for (const godot::OmniLight3D* l : lights_) {
        if (!l || l->get_light_energy() <= 0.0f || !l->is_visible_in_tree()) continue;
        if (!out.empty()) out += ", ";
        char energy[32];
        std::snprintf(energy, sizeof(energy), " %.2f", static_cast<double>(l->get_light_energy()));
        out += l->get_name() + energy;
    }
    return out;
}

int DynLights::Lights(assets::light::PointLight out[4]) const {
    Refresh();
    int n = 0;
    for (const godot::OmniLight3D* l : lights_) {
        if (!l || l->get_light_energy() <= 0.0f || !l->is_visible_in_tree()) continue;
        assets::light::PointLight& p = out[n++];
        const core::Mat4 w = l->get_global_transform();
        p.pos[0] = w.Tx();
        p.pos[1] = w.Ty();
        p.pos[2] = w.Tz();
        const godot::Color c = l->get_light_color();
        p.linear[0] = assets::light::SrgbToLinear(c.r) * l->get_light_energy();
        p.linear[1] = assets::light::SrgbToLinear(c.g) * l->get_light_energy();
        p.linear[2] = assets::light::SrgbToLinear(c.b) * l->get_light_energy();
        p.range = l->get_omni_range();
        p.decay = l->get_omni_attenuation();
    }
    return n;
}

void DynLights::Bind(godot::SceneTree& tree) {
    tree_ = &tree;
    generation_ = tree.free_generation();
    for (int i = 0; i < 4; ++i) lights_[i] = tree.get_node_as<godot::OmniLight3D>(kPaths[i]);
}

// A freed node leaves the tree's path index, so a freed light resolves to null.
void DynLights::Refresh() const {
    if (!tree_ || tree_->free_generation() == generation_) return;
    generation_ = tree_->free_generation();
    for (int i = 0; i < 4; ++i) lights_[i] = tree_->get_node_as<godot::OmniLight3D>(kPaths[i]);
}

int DynLights::Bound() const {
    Refresh();
    int n = 0;
    for (const godot::OmniLight3D* l : lights_) n += l != nullptr ? 1 : 0;
    return n;
}

bool DynLights::Uniforms(float k, float out[4][3]) const {
    Refresh();
    bool any = false;
    for (int i = 0; i < 4; ++i) {
        out[i][0] = out[i][1] = out[i][2] = 0.0f;
        const godot::OmniLight3D* l = lights_[i];
        if (!l || l->get_light_energy() <= 0.0f || !l->is_visible_in_tree()) continue;
        const godot::Color c = l->get_light_color();
        const float srgb[3] = {c.r, c.g, c.b};
        assets::light::DynUniform(srgb, l->get_light_energy(), k, out[i]);
        any = true;
    }
    return any;
}

void MovingLight::Bind(godot::SceneTree& tree) {
    tree_ = &tree;
    generation_ = tree.free_generation();
    light_ = tree.get_node_as<godot::OmniLight3D>(std::string(assets::kMovingLight));
}

void MovingLight::Refresh() const {
    if (!tree_ || tree_->free_generation() == generation_) return;
    generation_ = tree_->free_generation();
    light_ = tree_->get_node_as<godot::OmniLight3D>(std::string(assets::kMovingLight));
}

bool MovingLight::Uniforms(const float eye[3], float k, float pos[3], float* range, float colour[3]) const {
    Refresh();
    const godot::OmniLight3D* l = light_;
    if (!l || l->get_light_energy() <= 0.0f || !l->is_visible_in_tree()) return false;
    const core::Mat4 w = l->get_global_transform();
    pos[0] = w.Tx();
    pos[1] = w.Ty();
    pos[2] = w.Tz();
    *range = l->get_omni_range();
    // Near: within twice its reach of the camera (its sweep is 12.4 long).
    const float dx = pos[0] - eye[0], dy = pos[1] - eye[1], dz = pos[2] - eye[2];
    const float reach = 2.0f * *range + 6.5f;
    if (dx * dx + dy * dy + dz * dz > reach * reach) return false;
    const godot::Color c = l->get_light_color();
    const float srgb[3] = {c.r, c.g, c.b};
    assets::light::DynUniform(srgb, l->get_light_energy(), k, colour);
    return true;
}

void EnvironmentGrade::Bind(godot::SceneTree& tree) {
    tree_ = &tree;
    generation_ = tree.free_generation();
    env_ = tree.get_node_as<godot::WorldEnvironment>("WorldEnvironment");
    // Bound as the scene loads, before any script or animation moves it: the
    // values the bake graded this scene's pages with.
    if (env_) {
        restBrightness_ = env_->get_adjustment_brightness();
        restContrast_ = env_->get_adjustment_contrast();
    }
}

void EnvironmentGrade::Refresh() const {
    if (!tree_ || tree_->free_generation() == generation_) return;
    generation_ = tree_->free_generation();
    env_ = tree_->get_node_as<godot::WorldEnvironment>("WorldEnvironment");
}

bool EnvironmentGrade::Uniforms(float* scale, float* offset) const {
    *scale = 1.0f;
    *offset = 0.0f;
    Refresh();
    if (!env_) return false;
    if (std::fabs(env_->get_adjustment_saturation() - assets::light::Grade{}.saturation) > 1e-4f) {
        saturationMoved_ = true;
    }
    assets::light::PosterizedEnvironmentUniforms(env_->get_adjustment_brightness(), env_->get_adjustment_contrast(), scale,
                                                 offset, restBrightness_, restContrast_);
    return true;
}

bool EnvironmentGrade::Live(float* brightness, float* contrast) const {
    Refresh();
    if (!env_) return false;
    *brightness = env_->get_adjustment_brightness();
    *contrast = env_->get_adjustment_contrast();
    return true;
}

bool EnvironmentGrade::FogUniforms(Fog* out) const {
    *out = Fog{};
    Refresh();
    if (!env_ || !env_->get_fog_enabled()) return false;
    namespace L = assets::light;
    L::Grade g;
    g.brightness = env_->get_adjustment_brightness();
    g.contrast = env_->get_adjustment_contrast();
    g.saturation = env_->get_adjustment_saturation();
    // ubo.fog_light_color = fog_light_color.srgb_to_linear() * fog_light_energy.
    const godot::Color f = env_->get_fog_light_color();
    const float fe = env_->get_fog_light_energy();
    const float fog[3] = {L::SrgbToLinear(f.r) * fe, L::SrgbToLinear(f.g) * fe, L::SrgbToLinear(f.b) * fe};
    // BG_COLOR clears to the colour x energy; the fog-only sky pass mixes the
    // fog over it by fog_sky_affect (its fog amount at infinity is 1).
    const godot::Color b = env_->get_background_color();
    const float be = env_->get_background_mode() == 1 ? env_->get_background_energy_multiplier() : 0.0f;
    const float a = env_->get_fog_sky_affect();
    const float bg[3] = {L::SrgbToLinear(b.r) * be, L::SrgbToLinear(b.g) * be, L::SrgbToLinear(b.b) * be};
    float clear[3];
    for (int i = 0; i < 3; ++i) clear[i] = bg[i] + (fog[i] - bg[i]) * a;
    out->density = env_->get_fog_density();
    L::GradePixel(fog, g, out->colour);
    L::GradePixel(clear, g, out->clear);
    out->mix.curve = assets::FogCurveOf(g);
    out->mix.luminance = 0.2126f * fog[0] + 0.7152f * fog[1] + 0.0722f * fog[2];
    // The grey of what the FOG program mixes toward: the graded, posterized
    // colour, not the curve's (the menu's reads 0 where its curve gives 0.10).
    out->mix.grey = 0.2126f * out->colour[0] + 0.7152f * out->colour[1] + 0.0722f * out->colour[2];
    return true;
}

}  // namespace app
