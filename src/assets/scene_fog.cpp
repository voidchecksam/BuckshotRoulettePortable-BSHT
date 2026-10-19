#include "assets/scene_fog.h"

#include <algorithm>
#include <cmath>

namespace assets {

namespace {

using light::kAcesBias;

}  // namespace

FogCurve FogCurveOf(const light::Grade& g) {
    FogCurve c;
    c.whiteInv = 1.0f / light::AcesCurve(g.white * kAcesBias);
    c.brightness = g.brightness;
    c.contrast = g.contrast;
    return c;
}

// Line for line with the FOG vertex programs (render/shaders): a change here
// is a change there.
float FogGrey(float y, const FogCurve& c) {
    const float aces = std::clamp(light::AcesCurve(kAcesBias * std::max(y, 0.0f)) * c.whiteInv, 0.0f, 1.0f);
    const float srgb = aces < 0.0031308f ? 12.92f * aces : 1.055f * std::pow(aces, 1.0f / 2.4f) - 0.055f;
    return std::clamp(0.5f + (srgb * c.brightness - 0.5f) * c.contrast, 0.0f, 1.0f);
}

float FogWeight(float t, float surface, float fog, float fogGrey, const FogCurve& c) {
    const float shown = FogGrey(surface, c);
    const float mixed = FogGrey(t * surface + (1.0f - t) * fog, c);
    const float span = shown - fogGrey;
    if (std::fabs(span) <= kFogSpanFloor) return 1.0f - t;
    return std::clamp((shown - mixed) / span, 0.0f, 1.0f);
}

}  // namespace assets
