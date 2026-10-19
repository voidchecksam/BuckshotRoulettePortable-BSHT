#include "assets/surface_bake.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace assets {

namespace {

constexpr int kSoftSamples = 16;
constexpr float kNormalOffset = 1e-3f;

// Fixed points on the unit sphere (Fibonacci), so every bake is identical.
const std::array<std::array<float, 3>, kSoftSamples>& SpherePattern() {
    static const auto pattern = [] {
        std::array<std::array<float, 3>, kSoftSamples> p{};
        const float golden = 3.14159265f * (3.0f - std::sqrt(5.0f));
        for (int i = 0; i < kSoftSamples; ++i) {
            const float y = 1.0f - (static_cast<float>(i) + 0.5f) * 2.0f / kSoftSamples;
            const float r = std::sqrt(1.0f - y * y);
            const float phi = golden * static_cast<float>(i);
            p[i] = {r * std::cos(phi), y, r * std::sin(phi)};
        }
        return p;
    }();
    return pattern;
}

// Fraction of the light that reaches `from`: 0 or 1 for a point light, the
// share of the 16 sphere points in view for a sized one.
float Visibility(const BakedLight& l, const LightBvh& bvh, const float from[3]) {
    if (!l.shadow) return 1.0f;
    if (l.size <= 0.0f) return bvh.Occluded(from, l.position) ? 0.0f : 1.0f;
    int seen = 0;
    for (const auto& s : SpherePattern()) {
        const float to[3] = {l.position[0] + s[0] * l.size, l.position[1] + s[1] * l.size,
                             l.position[2] + s[2] * l.size};
        seen += bvh.Occluded(from, to) ? 0 : 1;
    }
    return static_cast<float>(seen) / kSoftSamples;
}

}  // namespace

void StaticLight(const std::vector<BakedLight>& lights, const LightBvh& bvh,
                 const float pos[3], const float nrm[3], const float eye[3],
                 float roughness, float outLinear[3], uint32_t layers) {
    outLinear[0] = outLinear[1] = outLinear[2] = 0.0f;
    float V[3] = {eye[0] - pos[0], eye[1] - pos[1], eye[2] - pos[2]};
    const float vl = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
    if (vl > 0.0f) for (float& v : V) v /= vl;
    const float from[3] = {pos[0] + nrm[0] * kNormalOffset, pos[1] + nrm[1] * kNormalOffset,
                           pos[2] + nrm[2] * kNormalOffset};
    for (const BakedLight& l : lights) {
        if ((l.cullMask & layers) == 0u) continue;   // scene_forward_clustered.glsl:1924
        float L[3] = {l.position[0] - pos[0], l.position[1] - pos[1], l.position[2] - pos[2]};
        const float d = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
        const float atten = light::OmniAttenuation(d, l.range, l.attenuation);
        if (atten <= 0.0f) continue;
        if (d > 0.0f) for (float& v : L) v /= d;
        const float brdf = light::BurleyNL(nrm, L, V, roughness, light::SizeA(l.size, d));
        if (brdf <= 0.0f) continue;
        const float k = l.energy * atten * brdf * Visibility(l, bvh, from);
        for (int c = 0; c < 3; ++c) outLinear[c] += l.colour[c] * k;
    }
}

void SceneLinear(const float albedoSrgbTexel[3], const SurfaceMaterial& m,
                 const float lightLinear[3], float outLinear[3]) {
    for (int i = 0; i < 3; ++i) {
        // Godot samples the sRGB texture as linear and linearises albedo_color
        // (source_color), then multiplies (scene_forward_clustered.glsl).
        const float albedo = light::SrgbToLinear(albedoSrgbTexel[i]) * light::SrgbToLinear(m.albedo[i]);
        const float lit = m.unshaded ? albedo : albedo * lightLinear[i] * (1.0f - m.metallic);
        outLinear[i] = lit + m.emission[i];
    }
}

void TexelColour(const float albedoSrgbTexel[3], const SurfaceMaterial& m,
                 const float lightLinear[3], const light::Grade& g, float outSrgb[3]) {
    float c[3];
    SceneLinear(albedoSrgbTexel, m, lightLinear, c);
    light::GradePixel(c, g, outSrgb);
}

int Tier(float distanceFromHome) {
    if (distanceFromHome <= 25.0f) return 0;
    if (distanceFromHome <= 40.0f) return 1;
    return 2;
}

float ChooseDensity(float native, int tier) {
    // {floor, cap} per tier, in texels per unit.
    static constexpr float kFloor[3] = {8.0f, 8.0f, 4.0f};
    static constexpr float kCap[3] = {32.0f, 16.0f, 4.0f};
    const int t = std::clamp(tier, 0, 2);
    return std::clamp(native, kFloor[t], kCap[t]);
}

bool IsSceneAuthoredTexture(std::string_view key) {
    constexpr std::string_view kModels = "inline_textures/models_";
    return !key.empty() && key.substr(0, kModels.size()) != kModels;
}

float ChooseDetailDensity(float native) {
    return std::clamp(native, 8.0f, kDetailDensityCap);
}

void RasteriseTriangle(const float uv[3][2], int size,
                       const std::function<void(int, int, const float*)>& sample) {
    const float x0 = uv[0][0], y0 = uv[0][1], x1 = uv[1][0], y1 = uv[1][1];
    const float x2 = uv[2][0], y2 = uv[2][1];
    const float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (std::fabs(area) < 1e-12f) return;
    const float inv = 1.0f / area;
    const int minX = std::max(0, static_cast<int>(std::floor(std::min({x0, x1, x2}))));
    const int maxX = std::min(size - 1, static_cast<int>(std::floor(std::max({x0, x1, x2}))));
    const int minY = std::max(0, static_cast<int>(std::floor(std::min({y0, y1, y2}))));
    const int maxY = std::min(size - 1, static_cast<int>(std::floor(std::max({y0, y1, y2}))));
    constexpr float kEdge = -1e-6f;   // inclusive edges, steady under rounding
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            for (int s = 0; s < 4; ++s) {
                const float px = static_cast<float>(x) + ((s & 1) ? 0.75f : 0.25f);
                const float py = static_cast<float>(y) + ((s & 2) ? 0.75f : 0.25f);
                float b[3];
                b[1] = ((px - x0) * (y2 - y0) - (x2 - x0) * (py - y0)) * inv;
                b[2] = ((x1 - x0) * (py - y0) - (px - x0) * (y1 - y0)) * inv;
                b[0] = 1.0f - b[1] - b[2];
                if (b[0] < kEdge || b[1] < kEdge || b[2] < kEdge) continue;
                sample(x, y, b);
            }
        }
    }
}

void Dilate(std::vector<uint8_t>& texels, std::vector<uint8_t>& mask, int size, int passes,
            int channels) {
    for (int p = 0; p < passes; ++p) {
        const std::vector<uint8_t> before = mask;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const int i = y * size + x;
                if (before[i]) continue;
                for (int dy = -1; dy <= 1 && !mask[i]; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= size || ny >= size) continue;
                        const int n = ny * size + nx;
                        if (!before[n]) continue;
                        for (int c = 0; c < channels; ++c) texels[i * channels + c] = texels[n * channels + c];
                        mask[i] = 1;
                        break;
                    }
                }
            }
        }
    }
}

}  // namespace assets

namespace assets {

std::vector<Placement> ShelfPack(const std::vector<PackRect>& rects, int pageSize, int& pages) {
    std::vector<std::size_t> order(rects.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return rects[a].h > rects[b].h; });
    struct Shelf { int page, y, h, x; };
    std::vector<Shelf> shelves;
    std::vector<int> pageTop;   // next free y per page
    std::vector<Placement> out(rects.size());
    auto align = [](int v) { return (v + 3) & ~3; };
    for (std::size_t i : order) {
        const int w = align(rects[i].w), h = align(rects[i].h);
        if (w > pageSize || h > pageSize) {
            pages = -1;   // no page of this size can hold it
            return out;
        }
        bool placed = false;
        for (Shelf& s : shelves) {
            if (h <= s.h && s.x + w <= pageSize) {
                out[i] = {s.page, s.x, s.y};
                s.x += w;
                placed = true;
                break;
            }
        }
        if (placed) continue;
        int page = -1;
        for (std::size_t p = 0; p < pageTop.size(); ++p) {
            if (pageTop[p] + h <= pageSize) { page = static_cast<int>(p); break; }
        }
        if (page < 0) {
            pageTop.push_back(0);
            page = static_cast<int>(pageTop.size()) - 1;
        }
        shelves.push_back({page, pageTop[static_cast<std::size_t>(page)], h, w});
        out[i] = {page, 0, pageTop[static_cast<std::size_t>(page)]};
        pageTop[static_cast<std::size_t>(page)] += h;
    }
    pages = static_cast<int>(pageTop.size());
    return out;
}

}  // namespace assets
