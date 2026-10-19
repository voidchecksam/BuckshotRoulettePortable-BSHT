#include "assets/bake.h"

#include <algorithm>
#include <cmath>

namespace assets {

namespace {

uint8_t ToByte(float linear) {
    // Clamp before the cast: a negative float cast to uint8_t is undefined, and
    // an over-bright one wraps to near-black, which reads as a lighting bug in
    // exactly the brightest places.
    const float c = std::clamp(linear, 0.0f, 1.0f);
    return static_cast<uint8_t>(c * 255.0f + 0.5f);
}

}  // namespace

float OmniAttenuation(float distance, float range, float attenuation) {
    if (range <= 0.0f) return 0.0f;

    // The geometric term.
    float nd;
    if (distance <= 0.0f) {
        nd = 1.0f;
    } else {
        nd = distance / range;
        nd *= nd;
        nd *= nd;                       // (d/range)^4
        nd = 1.0f - nd;
        if (nd <= 0.0f) return 0.0f;    // hard cut at range, as Godot does
        nd = nd * nd;
    }

    if (attenuation <= 0.0f) return nd;

    // Godot clamps the distance to 0.0001 before the pow, which is what stops a
    // vertex sitting exactly on a light producing an infinity.
    const float d = distance > 0.0001f ? distance : 0.0001f;
    return nd * std::pow(d, -attenuation);
}

float SrgbToLinear(float c) {
    if (c <= 0.0f) return 0.0f;
    if (c >= 1.0f) return 1.0f;
    return c < 0.04045f ? c * (1.0f / 12.92f)
                        : std::pow((c + 0.055f) * (1.0f / 1.055f), 2.4f);
}

float LambertWrap(float ndotl) {
    // Godot clamps N.L to 1 before the BRDF: min(size_A + dot(N, L), 1.0),
    // and size_A is 0 for an OmniLight3D of the default size 0.
    if (ndotl > 1.0f) ndotl = 1.0f;
    const float w = (ndotl + 1.0f) * 0.25f;   // roughness = 1.0
    return w > 0.0f ? w : 0.0f;
}

void LightContribution(const BakedLight& l, const float worldPos[3],
                       const float worldNrm[3], float outRgb[3]) {
    const float dx = l.position[0] - worldPos[0];
    const float dy = l.position[1] - worldPos[1];
    const float dz = l.position[2] - worldPos[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    const float atten = OmniAttenuation(dist, l.range, l.attenuation);
    if (atten <= 0.0f) {
        outRgb[0] = outRgb[1] = outRgb[2] = 0.0f;
        return;
    }

    // A vertex sitting exactly on a light has no direction to it; treat it as
    // fully facing rather than dividing by zero.
    float ndotl = 1.0f;
    if (dist > 1e-6f) {
        const float inv = 1.0f / dist;
        ndotl = worldNrm[0] * dx * inv + worldNrm[1] * dy * inv + worldNrm[2] * dz * inv;
    }

    const float k = atten * LambertWrap(ndotl) * l.energy;
    outRgb[0] = l.colour[0] * k;
    outRgb[1] = l.colour[1] * k;
    outRgb[2] = l.colour[2] * k;
}

std::vector<BakedLight> CollectLights(const Scene& scene) {
    std::vector<BakedLight> out;
    out.reserve(scene.LightCount());
    for (std::size_t i = 0; i < scene.LightCount(); ++i) {
        const OmniLight& l = scene.Lights()[i];
        // World, not local: the exporter writes local transforms and the
        // lights are parented deep.
        const core::Mat4& w = scene.LightWorldTransform(static_cast<int>(i));

        BakedLight b;
        b.position[0] = w.Tx();
        b.position[1] = w.Ty();
        b.position[2] = w.Tz();
        // Godot linearises before lighting, and so does this.
        b.colour[0] = SrgbToLinear(l.colour[0]);
        b.colour[1] = SrgbToLinear(l.colour[1]);
        b.colour[2] = SrgbToLinear(l.colour[2]);
        b.energy = l.energy;
        b.range  = l.range;
        b.attenuation = l.attenuation;
        out.push_back(b);
    }
    return out;
}

void BakeVertexLighting(Mesh& mesh, const core::Mat4& world,
                        const std::vector<BakedLight>& lights,
                        const BakeSettings& settings) {
    const int vertexCount = mesh.VertexCount();
    mesh.colours.assign(static_cast<std::size_t>(vertexCount) * 4, 0);
    if (vertexCount == 0) return;

    const bool haveNormals =
        mesh.normals.size() == mesh.positions.size();

    for (int v = 0; v < vertexCount; ++v) {
        const std::size_t p3 = static_cast<std::size_t>(v) * 3;

        const float localPos[3] = {mesh.positions[p3 + 0],
                                   mesh.positions[p3 + 1],
                                   mesh.positions[p3 + 2]};
        float worldPos[3];
        core::TransformPoint(world, localPos, worldPos);

        float worldNrm[3] = {0.0f, 0.0f, 1.0f};
        if (haveNormals) {
            const float localNrm[3] = {mesh.normals[p3 + 0],
                                       mesh.normals[p3 + 1],
                                       mesh.normals[p3 + 2]};
            // A direction, not a point: running a normal through the
            // translation adds the node's position to it, and the shading goes
            // wrong in a way that looks like bad source normals.
            core::TransformDirection(world, localNrm, worldNrm);
            core::Normalize3(worldNrm);
        }

        float acc[3] = {settings.ambient[0], settings.ambient[1], settings.ambient[2]};

        // A mesh with no normals cannot have a direction to shade by; treat
        // every surface as facing its lights, which is what the old code did
        // via lambert = 1 and matches Godot for an unlit fallback.
        const float up[3] = {0.0f, 1.0f, 0.0f};
        for (const BakedLight& l : lights) {
            float c[3];
            // The one shared implementation, also used by the shader's unit
            // test, so the two cannot drift apart.
            LightContribution(l, worldPos, haveNormals ? worldNrm : up, c);
            acc[0] += c[0];
            acc[1] += c[1];
            acc[2] += c[2];
        }

        const std::size_t c4 = static_cast<std::size_t>(v) * 4;
        mesh.colours[c4 + 0] = ToByte(acc[0] * settings.exposure);
        mesh.colours[c4 + 1] = ToByte(acc[1] * settings.exposure);
        mesh.colours[c4 + 2] = ToByte(acc[2] * settings.exposure);
        mesh.colours[c4 + 3] = 255;
    }
}

}  // namespace assets
