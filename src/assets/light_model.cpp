#include "assets/light_model.h"

#include <algorithm>
#include <cmath>

namespace assets::light {

namespace {

float Clamp01(float c) { return std::clamp(c, 0.0f, 1.0f); }

// scene_forward_lights_inc.glsl:29 SchlickFresnel: (1 - u)^5.
float SchlickFresnel(float u) {
    const float m = 1.0f - u;
    const float m2 = m * m;
    return m2 * m2 * m;
}

float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

}  // namespace

// tonemap.glsl:220-237: the curve applied per channel between the matrices.
float AcesCurve(float x) {
    return (x * (x + kAcesA) - kAcesB) / (x * (kAcesC * x + kAcesD) + kAcesE);
}

float SrgbToLinear(float c) {
    c = Clamp01(c);
    return c < 0.04045f ? c * (1.0f / 12.92f) : std::pow((c + 0.055f) * (1.0f / 1.055f), 2.4f);
}

float LinearToSrgb(float c) {
    c = Clamp01(c);                                        // tonemap.glsl:249
    return c < 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

void AddPointLight(const PointLight& l, const float at[3], float inout[3]) {
    const float dx = l.pos[0] - at[0], dy = l.pos[1] - at[1], dz = l.pos[2] - at[2];
    const float a = OmniAttenuation(std::sqrt(dx * dx + dy * dy + dz * dz), l.range, l.decay) * 0.25f;
    for (int c = 0; c < 3; ++c) inout[c] += l.linear[c] * a;
}

float HdrBlendAlpha(const float linear[3], float alpha) {
    const float over = std::max({linear[0], linear[1], linear[2], 1.0f});
    return std::min(1.0f, alpha * over);
}

float OmniAttenuation(float distance, float range, float decay) {
    if (range <= 0.0f) return 0.0f;
    float nd = distance / range;                           // :407
    nd *= nd;
    nd *= nd;                                              // nd^4
    nd = std::max(1.0f - nd, 0.0f);
    nd *= nd;                                              // nd^2
    return nd * std::pow(std::max(distance, 0.0001f), -decay);
}

float SizeA(float lightSize, float distance) {
    if (lightSize <= 0.0f) return 0.0f;                    // :576
    const float t = lightSize / std::max(0.001f, distance);
    return std::max(0.0f, 1.0f - 1.0f / std::sqrt(1.0f + t * t));
}

float BurleyNL(const float N[3], const float L[3], const float V[3], float roughness, float A) {
    const float NdotL = std::min(A + Dot(N, L), 1.0f);     // :95
    const float cNdotL = std::max(NdotL, 0.0f);
    const float cNdotV = std::max(Dot(N, V), 1e-4f);       // :97-98
    // :101 H = normalize(V + L); :110 cLdotH = clamp(A + dot(L, H), 0, 1).
    float H[3] = {V[0] + L[0], V[1] + L[1], V[2] + L[2]};
    const float len = std::sqrt(Dot(H, H));
    float cLdotH = 0.0f;
    if (len > 0.0f) {
        for (float& h : H) h /= len;
        cLdotH = Clamp01(A + Dot(L, H));
    }
    // :124-127; the 1/PI is cancelled by Godot's energy * PI.
    const float FD90_minus_1 = 2.0f * cLdotH * cLdotH * roughness - 0.5f;
    const float FdV = 1.0f + FD90_minus_1 * SchlickFresnel(cNdotV);
    const float FdL = 1.0f + FD90_minus_1 * SchlickFresnel(cNdotL);
    return FdV * FdL * cNdotL;
}

void Aces(const float in[3], float white, float out[3]) {
    constexpr float kBias = kAcesBias;                     // :215 exposure_bias
    // GLSL mat3(v0, v1, v2) takes COLUMNS, and `color *= m` is the row-vector
    // product color * m, so out[j] = dot(color, column j) = sum_i color[i] * cols[j][i].
    constexpr float rrt[3][3] = {{0.59719f, 0.35458f, 0.04823f},
                                 {0.07600f, 0.90834f, 0.01566f},
                                 {0.02840f, 0.13383f, 0.83777f}};
    constexpr float odt[3][3] = {{1.60475f, -0.53108f, -0.07367f},
                                 {-0.10208f, 1.10813f, -0.00605f},
                                 {-0.00327f, -0.07276f, 1.07602f}};
    float c[3], t[3];
    for (int j = 0; j < 3; ++j) c[j] = kBias * Dot(in, rrt[j]);     // :232 color *= rgb_to_rrt
    for (int j = 0; j < 3; ++j) t[j] = AcesCurve(c[j]);              // :233
    const float w = AcesCurve(white * kBias);                        // :236-237
    for (int j = 0; j < 3; ++j) out[j] = Dot(t, odt[j]) / w;         // :234, :239
}

void Bcs(float c[3], float brightness, float contrast, float saturation) {
    for (int i = 0; i < 3; ++i) c[i] *= brightness;                      // :337 mix(0, c, b)
    for (int i = 0; i < 3; ++i) c[i] = 0.5f + (c[i] - 0.5f) * contrast;  // :338
    const float grey = (c[0] + c[1] + c[2]) * 0.33333f;                  // :339
    for (int i = 0; i < 3; ++i) c[i] = grey + (c[i] - grey) * saturation;
}

float StoreUnorm8(float c) { return std::round(Clamp01(c) * 255.0f) / 255.0f; }

float Posterize(float c, float levels) { return std::floor(c * levels) / levels; }

void GradePixel(const float linear[3], const Grade& g, float outSrgb[3]) {
    float c[3];
    Aces(linear, g.white, c);
    for (float& v : c) v = LinearToSrgb(v);
    Bcs(c, g.brightness, g.contrast, g.saturation);
    for (int i = 0; i < 3; ++i) outSrgb[i] = Posterize(StoreUnorm8(c[i]), g.levels);
}

// godot: scene_forward_clustered.glsl:300-301 (binormal), :888-891 (the map)
void PerturbNormal(const float n[3], const float t[4], const float map[2], float depth, float out[3]) {
    const float sgn = t[3] > 0.0f ? 1.0f : (t[3] < 0.0f ? -1.0f : 0.0f);
    float b[3] = {(n[1] * t[2] - n[2] * t[1]) * sgn, (n[2] * t[0] - n[0] * t[2]) * sgn,
                  (n[0] * t[1] - n[1] * t[0]) * sgn};
    const float bl = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    if (bl > 0.0f) for (float& v : b) v /= bl;
    const float x = map[0] * 2.0f - 1.0f, y = map[1] * 2.0f - 1.0f;
    const float z = std::sqrt(std::max(0.0f, 1.0f - (x * x + y * y)));
    float m[3];
    for (int i = 0; i < 3; ++i) {
        const float target = t[i] * x + b[i] * y + n[i] * z;
        m[i] = n[i] + (target - n[i]) * depth;
    }
    const float ml = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    for (int i = 0; i < 3; ++i) out[i] = ml > 0.0f ? m[i] / ml : n[i];
}

unsigned char EncodeDynLayer(float v) {
    const float q = std::sqrt(std::clamp(v / 4.0f, 0.0f, 1.0f));
    return static_cast<unsigned char>(std::lround(q * 255.0f));
}

float DecodeDynLayer(unsigned char c) {
    const float q = static_cast<float>(c) / 255.0f;
    return q * q * 4.0f;
}

void DynUniform(const float colourSrgb[3], float energy, float k, float out[3]) {
    // Added before Godot's grade, the light is greyed by its saturation like
    // everything else (Bcs, tonemap.glsl:339): mix(grey, colour, saturation).
    const float lin[3] = {SrgbToLinear(colourSrgb[0]), SrgbToLinear(colourSrgb[1]), SrgbToLinear(colourSrgb[2])};
    const float grey = (lin[0] + lin[1] + lin[2]) * 0.33333f;
    const float s = Grade{}.saturation;
    for (int i = 0; i < 3; ++i) out[i] = (grey + (lin[i] - grey) * s) * energy * k;
}

void EnvironmentUniforms(float brightness, float contrast, float* scale, float* offset, float restBrightness,
                         float restContrast) {
    const float s = (brightness * contrast) / (restBrightness * restContrast);
    *scale = s;
    *offset = 0.5f * (1.0f - contrast) - s * 0.5f * (1.0f - restContrast);
}

float NormalMapLod(float texelsPerUnit, float distance, float cosTheta) {
    const float pixel = 2.0f * distance * std::tan(kHomeFovYDegrees * 3.14159265f / 360.0f) / kScreenHeight;
    const float stretch = 1.0f / std::max(cosTheta, 0.05f);
    return std::log2(std::max(texelsPerUnit * pixel * stretch, 1e-6f));
}

void PosterizedEnvironmentUniforms(float brightness, float contrast, float* scale, float* offset,
                                   float restBrightness, float restContrast) {
    float s = 1.0f, o = 0.0f;
    EnvironmentUniforms(brightness, contrast, &s, &o, restBrightness, restContrast);
    const Grade rest;
    // Brightening: exact (the clamp at 1 is what the shader's output does too).
    // Darkening: fitted, so a fade ends black as posterize floors it.
    if (s >= 1.0f) {
        *scale = s;
        *offset = o;
        return;
    }
    // Posterized result of each level, then the line through them.
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const int n = static_cast<int>(rest.levels) + 1;
    for (int i = 0; i < n; ++i) {
        const double x = i / static_cast<double>(rest.levels);
        // +1e-3: a texel ON a level must not floor to the one below it.
        const double y = Posterize(std::clamp(static_cast<float>(x) * s + o + 1e-3f, 0.0f, 1.0f), rest.levels);
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    const double den = n * sxx - sx * sx;
    const double fs = (n * sxy - sx * sy) / den;
    *scale = static_cast<float>(fs);
    *offset = static_cast<float>((sy - fs * sx) / n);
}

}  // namespace assets::light
