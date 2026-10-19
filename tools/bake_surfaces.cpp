#include "bake_surfaces.h"

#include "assets/scene_fog.h"

#include "assets/light_model.h"

#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"
#include "xatlas/xatlas.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace tools {

namespace {

constexpr uint32_t kMaxVertices = 65535;   // GLES2: 16-bit indices only
constexpr int kDilate = 2;
constexpr uint32_t kPadding = 2;

// xatlas reports each phase in percent; print every 10% so a long unwrap says
// which phase it is in.
[[maybe_unused]] bool Progress(xatlas::ProgressCategory category, int progress, void* last) {
    int& shown = *static_cast<int*>(last);
    const int key = static_cast<int>(category) * 1000 + progress / 10;
    if (key != shown) {
        shown = key;
        std::printf("surfaces:   %s %d%%\n", xatlas::StringForEnum(category), progress);
        std::fflush(stdout);
    }
    return true;
}

// One mesh handed to xatlas: part of a batch in one tier, or a whole prop.
struct Group {
    bool isProp = false;
    int index = 0;               // batch or prop index
    int tier = 0;
    float density = 0.0f;        // texels per world unit
    bool alpha = false;
    bool variant = false;        // a prop with material variants: pages of its own
    int grade = -1;              // SurfaceBakeInput::propGrades index; -1: the scene's
    std::vector<uint32_t> faces; // triangle indices into the source mesh
    std::vector<uint32_t> verts; // group-local vertex -> source vertex
};

// The world-space vertex data of one source mesh.
struct WorldMesh {
    const assets::Mesh* mesh = nullptr;
    std::vector<float> pos;   // 3 per vertex, world
    std::vector<float> nrm;   // 3 per vertex, world, unit
    std::vector<float> tan;   // 4 per vertex, world xyz (unit) + handedness; empty: none
};

// Where a group is seen from: its grade's eye (the ending's camera for the
// car), else the scene's.
const float* GroupEye(const SurfaceBakeInput& in, const Group& g) {
    if (g.grade >= 0 && static_cast<std::size_t>(g.grade) < in.propGradeEyes.size()) {
        return in.propGradeEyes[static_cast<std::size_t>(g.grade)].data();
    }
    return in.eye;
}

WorldMesh ToWorld(const assets::Mesh& m, const core::Mat4& w) {
    WorldMesh out;
    out.mesh = &m;
    const int n = m.VertexCount();
    out.pos.resize(static_cast<std::size_t>(n) * 3);
    out.nrm.resize(static_cast<std::size_t>(n) * 3);
    for (int v = 0; v < n; ++v) {
        const float* p = &m.positions[static_cast<std::size_t>(v) * 3];
        const float x = p[0], y = p[1], z = p[2];
        // core::Mat4 is column-major: m[col * 4 + row].
        for (int r = 0; r < 3; ++r) {
            out.pos[v * 3 + r] = w.m[0 * 4 + r] * x + w.m[1 * 4 + r] * y + w.m[2 * 4 + r] * z + w.m[3 * 4 + r];
        }
        float nw[3] = {0.0f, 1.0f, 0.0f};
        if (static_cast<std::size_t>(v) * 3 + 2 < m.normals.size()) {
            const float* q = &m.normals[static_cast<std::size_t>(v) * 3];
            for (int r = 0; r < 3; ++r) {
                nw[r] = w.m[0 * 4 + r] * q[0] + w.m[1 * 4 + r] * q[1] + w.m[2 * 4 + r] * q[2];
            }
            const float len = std::sqrt(nw[0] * nw[0] + nw[1] * nw[1] + nw[2] * nw[2]);
            if (len > 0.0f) for (float& c : nw) c /= len;
        }
        for (int r = 0; r < 3; ++r) out.nrm[v * 3 + r] = nw[r];
    }
    // Tangents turn with the node; a mirrored node flips the handedness, as
    // Godot builds the binormal in model space and then turns it.
    if (m.tangents.size() == static_cast<std::size_t>(n) * 4) {
        const float det = w.m[0] * (w.m[5] * w.m[10] - w.m[9] * w.m[6]) -
                          w.m[4] * (w.m[1] * w.m[10] - w.m[9] * w.m[2]) +
                          w.m[8] * (w.m[1] * w.m[6] - w.m[5] * w.m[2]);
        out.tan.resize(static_cast<std::size_t>(n) * 4);
        for (int v = 0; v < n; ++v) {
            const float* q = &m.tangents[static_cast<std::size_t>(v) * 4];
            float tw[3];
            for (int r = 0; r < 3; ++r) tw[r] = w.m[0 * 4 + r] * q[0] + w.m[1 * 4 + r] * q[1] + w.m[2 * 4 + r] * q[2];
            const float len = std::sqrt(tw[0] * tw[0] + tw[1] * tw[1] + tw[2] * tw[2]);
            for (int r = 0; r < 3; ++r) out.tan[v * 4 + r] = len > 0.0f ? tw[r] / len : 0.0f;
            out.tan[v * 4 + 3] = det < 0.0f ? -q[3] : q[3];
        }
    }
    return out;
}

float TriArea3(const float* a, const float* b, const float* c) {
    const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float x = u[1] * v[2] - u[2] * v[1], y = u[2] * v[0] - u[0] * v[2], z = u[0] * v[1] - u[1] * v[0];
    return 0.5f * std::sqrt(x * x + y * y + z * z);
}

// The albedo's own texels per world unit on one triangle (0: untextured).
float NativeDensity(const WorldMesh& wm, uint32_t tri, const assets::BakedTexture* tex) {
    const assets::Mesh& m = *wm.mesh;
    if (!tex || tex->width <= 1 || !m.HasUvs()) return 0.0f;
    const uint16_t* ix = &m.indices[static_cast<std::size_t>(tri) * 3];
    const float area = TriArea3(&wm.pos[ix[0] * 3], &wm.pos[ix[1] * 3], &wm.pos[ix[2] * 3]);
    if (area <= 0.0f) return 0.0f;
    const float* a = &m.uvs[ix[0] * 2];
    const float* b = &m.uvs[ix[1] * 2];
    const float* c = &m.uvs[ix[2] * 2];
    const float uvArea = 0.5f * std::fabs((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]));
    return std::sqrt(uvArea * static_cast<float>(tex->width) * static_cast<float>(tex->height) / area);
}

// Area-weighted median of the triangles' native densities.
float GroupDensity(const WorldMesh& wm, const std::vector<uint32_t>& faces,
                   const assets::BakedTexture* tex, int tier, int detail = 0) {
    std::vector<std::pair<float, float>> d;   // density, area
    float total = 0.0f;
    for (uint32_t f : faces) {
        const uint16_t* ix = &wm.mesh->indices[static_cast<std::size_t>(f) * 3];
        const float area = TriArea3(&wm.pos[ix[0] * 3], &wm.pos[ix[1] * 3], &wm.pos[ix[2] * 3]);
        d.emplace_back(NativeDensity(wm, f, tex), area);
        total += area;
    }
    std::sort(d.begin(), d.end());
    float acc = 0.0f, median = 0.0f;
    for (const auto& [density, area] : d) {
        acc += area;
        median = density;
        if (acc >= total * 0.5f) break;
    }
    if (detail == 2) return std::min(assets::ChooseDetailDensity(median), 128.0f);   // a casing
    return detail != 0 ? assets::ChooseDetailDensity(median) : assets::ChooseDensity(median, tier);
}

// Bilinear, repeat-wrapped sample of an RGBA8 texture, 0..1 per channel.
void SampleAlbedo(const assets::TexturePool& pool, int index, const float uv[2], float out[4]) {
    const assets::BakedTexture& t = pool.Textures()[static_cast<std::size_t>(index)];
    const uint8_t* px = pool.PixelsFor(index);
    if (!px || t.width <= 1) {   // colour-only: white; the material carries the colour
        out[0] = out[1] = out[2] = out[3] = 1.0f;
        return;
    }
    const float fx = uv[0] * static_cast<float>(t.width) - 0.5f;
    const float fy = uv[1] * static_cast<float>(t.height) - 0.5f;
    const float x0f = std::floor(fx), y0f = std::floor(fy);
    const float ax = fx - x0f, ay = fy - y0f;
    auto wrap = [](int v, int n) { v %= n; return v < 0 ? v + n : v; };
    const int x0 = wrap(static_cast<int>(x0f), t.width), x1 = wrap(static_cast<int>(x0f) + 1, t.width);
    const int y0 = wrap(static_cast<int>(y0f), t.height), y1 = wrap(static_cast<int>(y0f) + 1, t.height);
    for (int c = 0; c < 4; ++c) {
        const float p00 = px[(y0 * t.width + x0) * 4 + c], p10 = px[(y0 * t.width + x1) * 4 + c];
        const float p01 = px[(y1 * t.width + x0) * 4 + c], p11 = px[(y1 * t.width + x1) * 4 + c];
        const float top = p00 + (p10 - p00) * ax, bottom = p01 + (p11 - p01) * ax;
        out[c] = (top + (bottom - top) * ay) / 255.0f;
    }
}

// What a group's source vertex `sv` of face `face` is cut from: the texture
// and material ColourFace shades that face with (`face` indexes the source
// mesh; a batch's face names its source surface), the texel at the vertex's UV.
VertexSurface SurfaceOf(const SurfaceBakeInput& in, const assets::Room& room, const Group& g, const assets::Mesh& src,
                        uint32_t sv, uint32_t face) {
    VertexSurface s;
    const std::size_t gi = static_cast<std::size_t>(g.index);
    int tex = -1;
    if (g.isProp) {
        if (gi < in.propMaterial.size()) s.material = in.propMaterial[gi];
        if (gi < in.propTexture.size()) tex = in.propTexture[gi];
    } else {
        const assets::RoomBatch& b = room.Batches()[gi];
        if (face < b.triSource.size() && static_cast<std::size_t>(b.triSource[face]) < in.sourceMaterial.size()) {
            s.material = in.sourceMaterial[static_cast<std::size_t>(b.triSource[face])];
        }
        if (gi < in.batchTexture.size()) tex = in.batchTexture[gi];
    }
    if (tex >= 0 && in.albedo && src.uvs.size() >= (static_cast<std::size_t>(sv) + 1) * 2) {
        float rgba[4];
        SampleAlbedo(*in.albedo, tex, &src.uvs[static_cast<std::size_t>(sv) * 2], rgba);
        std::copy_n(rgba, 3, s.texel);
    }
    return s;
}

// Per-page accumulation: linear colour and alpha summed over subsamples.
struct Accum {
    int size = 0;
    bool alpha = false;
    std::vector<float> sum;        // 4 per texel: r g b a
    std::vector<uint16_t> count;
};

// Everything one chart's faces need to colour their texels.
struct Context {
    const SurfaceBakeInput* in = nullptr;
    const assets::LightBvh* bvh = nullptr;        // the room's texels: room + props cast
    const assets::LightBvh* propBvh = nullptr;    // props' texels: the static room only
    const assets::Room* room = nullptr;
    // Per prop, the mesh at RoomProp::altWorld, or an empty mesh when it has none.
    const std::vector<WorldMesh>* propAlt = nullptr;
    // Per prop, the mesh at each of RoomProp::meanPoses (empty: none).
    const std::vector<std::vector<WorldMesh>>* propMean = nullptr;
    // Per prop, the mesh at each of RoomProp::extraPoses (empty: none).
    const std::vector<std::vector<WorldMesh>>* propExtra = nullptr;
};

// Colours every texel one source face covers. `uvTex` are the face's three
// atlas UVs in texels; `srcVerts` its three source vertices.
// `variant`, when given, replaces the surface's material and albedo texture.
void ColourFace(const Context& cx, const Group& g, const WorldMesh& wm, uint32_t face,
                const float uvTex[3][2], const uint32_t srcVerts[3], Accum& acc,
                const PropVariantInput* variant) {
    const assets::Mesh& m = *wm.mesh;
    const assets::SurfaceMaterial& mat =
        variant ? variant->material
        : g.isProp ? cx.in->propMaterial[static_cast<std::size_t>(g.index)]
                   : cx.in->sourceMaterial[static_cast<std::size_t>(
                         cx.room->Batches()[static_cast<std::size_t>(g.index)].triSource[face])];
    const int tex = variant ? variant->texture
                    : g.isProp ? cx.in->propTexture[static_cast<std::size_t>(g.index)]
                               : cx.in->batchTexture[static_cast<std::size_t>(g.index)];
    int normalTex = -1;
    if (cx.in->normal) {
        if (variant) normalTex = variant->normalTexture;
        else if (g.isProp) {
            if (!cx.in->propNormal.empty()) normalTex = cx.in->propNormal[static_cast<std::size_t>(g.index)];
        } else {
            normalTex = cx.in->sourceNormal[static_cast<std::size_t>(
                cx.room->Batches()[static_cast<std::size_t>(g.index)].triSource[face])];
        }
    }
    uint32_t layers = 0xFFFFFFFFu;
    if (g.isProp) {
        if (!cx.in->propLayers.empty()) layers = cx.in->propLayers[static_cast<std::size_t>(g.index)];
    } else if (!cx.in->sourceLayers.empty()) {
        layers = cx.in->sourceLayers[static_cast<std::size_t>(
            cx.room->Batches()[static_cast<std::size_t>(g.index)].triSource[face])];
    }
    // The subsamples of one texel arrive together; light is evaluated once per
    // texel at their mean position and normal, albedo per subsample.
    const WorldMesh* alt = nullptr;
    if (g.isProp && cx.propAlt) {
        const WorldMesh& a = (*cx.propAlt)[static_cast<std::size_t>(g.index)];
        if (!a.pos.empty()) alt = &a;
    }
    const std::vector<WorldMesh>* mean = nullptr;
    if (g.isProp && cx.propMean && !(*cx.propMean)[static_cast<std::size_t>(g.index)].empty()) {
        mean = &(*cx.propMean)[static_cast<std::size_t>(g.index)];
    }
    const std::vector<WorldMesh>* extra = nullptr;
    if (g.isProp && cx.propExtra && !(*cx.propExtra)[static_cast<std::size_t>(g.index)].empty()) {
        extra = &(*cx.propExtra)[static_cast<std::size_t>(g.index)];
    }
    int curX = -1, curY = -1, n = 0;
    float bs[4][3];
    // Static light at this texel with the mesh placed as `pose`.
    auto lightAt = [&](const WorldMesh& pose, float light[3]) {
        float pos[3] = {0, 0, 0}, nrm[3] = {0, 0, 0};
        const bool mapped = normalTex >= 0 && !pose.tan.empty() && m.HasUvs();
        // The map's texels per world unit on this face, for the mip `home`
        // samples it at (light::NormalMapLod).
        int mip1 = -1;
        float mapDensity = 0.0f;
        if (mapped) {
            if (static_cast<std::size_t>(normalTex) < cx.in->normalMip1.size()) {
                mip1 = cx.in->normalMip1[static_cast<std::size_t>(normalTex)];
            }
            const assets::BakedTexture& nt = cx.in->normal->Textures()[static_cast<std::size_t>(normalTex)];
            const float* a = &pose.pos[srcVerts[0] * 3];
            const float* b = &pose.pos[srcVerts[1] * 3];
            const float* c = &pose.pos[srcVerts[2] * 3];
            const float world = TriArea3(a, b, c);
            const float* ua = &m.uvs[srcVerts[0] * 2];
            const float* ub = &m.uvs[srcVerts[1] * 2];
            const float* uc = &m.uvs[srcVerts[2] * 2];
            const float uv = 0.5f * std::fabs((ub[0] - ua[0]) * (uc[1] - ua[1]) - (uc[0] - ua[0]) * (ub[1] - ua[1]));
            if (world > 0.0f) {
                mapDensity = std::sqrt(uv * static_cast<float>(nt.width) * static_cast<float>(nt.height) / world);
            }
        }
        for (int s = 0; s < n; ++s) {
            float ns[3] = {0, 0, 0}, ts[4] = {0, 0, 0, 0}, uv[2] = {0, 0};
            for (int k = 0; k < 3; ++k) {
                const uint32_t v = srcVerts[k];
                for (int a = 0; a < 3; ++a) {
                    pos[a] += bs[s][k] * pose.pos[v * 3 + a] / static_cast<float>(n);
                    ns[a] += bs[s][k] * pose.nrm[v * 3 + a];
                }
                if (mapped) {
                    for (int a = 0; a < 4; ++a) ts[a] += bs[s][k] * pose.tan[v * 4 + a];
                    uv[0] += bs[s][k] * m.uvs[v * 2];
                    uv[1] += bs[s][k] * m.uvs[v * 2 + 1];
                }
            }
            if (mapped) {
                // Godot perturbs the interpolated, renormalised normal with the
                // map at this pixel (scene_forward_clustered.glsl:775-891).
                const float nl = std::sqrt(ns[0] * ns[0] + ns[1] * ns[1] + ns[2] * ns[2]);
                const float tl = std::sqrt(ts[0] * ts[0] + ts[1] * ts[1] + ts[2] * ts[2]);
                if (nl > 0.0f && tl > 0.0f) {
                    for (float& c : ns) c /= nl;
                    for (int a = 0; a < 3; ++a) ts[a] /= tl;
                    float map[4];
                    SampleAlbedo(*cx.in->normal, normalTex, uv, map);
                    if (mip1 >= 0) {
                        float at[3] = {0, 0, 0};
                        for (int k = 0; k < 3; ++k)
                            for (int a = 0; a < 3; ++a) at[a] += bs[s][k] * pose.pos[srcVerts[k] * 3 + a];
                        float view[3];
                        float d = 0.0f;
                        for (int a = 0; a < 3; ++a) {
                            view[a] = GroupEye(*cx.in, g)[a] - at[a];
                            d += view[a] * view[a];
                        }
                        d = std::sqrt(d);
                        const float cosTheta =
                            d > 0.0f ? std::fabs(ns[0] * view[0] + ns[1] * view[1] + ns[2] * view[2]) / d : 1.0f;
                        const float t = std::clamp(assets::light::NormalMapLod(mapDensity, d, cosTheta), 0.0f, 1.0f);
                        if (t > 0.0f) {
                            float far[4];
                            SampleAlbedo(*cx.in->normal, mip1, uv, far);
                            for (int c = 0; c < 2; ++c) map[c] += (far[c] - map[c]) * t;
                        }
                    }
                    float out[3];
                    assets::light::PerturbNormal(ns, ts, map, mat.normalScale, out);
                    std::copy_n(out, 3, ns);
                }
            }
            for (int a = 0; a < 3; ++a) nrm[a] += ns[a];
        }
        const float len = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
        if (len > 0.0f) for (float& c : nrm) c /= len;
        // A prop takes shadows from the static room only (see propBvh).
        assets::StaticLight(cx.in->lights, g.isProp ? *cx.propBvh : *cx.bvh, pos, nrm, GroupEye(*cx.in, g),
                            mat.roughness, light, layers);
        for (int a = 0; a < 3; ++a) light[a] += cx.in->ambient[a];
    };
    auto luma = [](const float c[3]) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; };
    auto flush = [&]() {
        if (n == 0) return;
        float light[3] = {1.0f, 1.0f, 1.0f};
        if (!mat.unshaded && mean) {
            // A prop that passes through its light on a loop: the mean.
            for (float& c : light) c = 0.0f;
            for (const WorldMesh& pose : *mean) {
                float at[3];
                lightAt(pose, at);
                for (int c = 0; c < 3; ++c) light[c] += at[c] / static_cast<float>(mean->size());
            }
        } else if (!mat.unshaded) {
            lightAt(wm, light);
            // A texel is lit where it is SEEN. A prop a reveal moves has two
            // poses, and each face is on show in the one that faces it to the
            // lamps: the shell compartment's felt at rest, its slotted top
            // once it has turned over. Keep the brighter, per texel.
            if (alt) {
                float other[3];
                lightAt(*alt, other);
                if (luma(other) > luma(light)) std::copy_n(other, 3, light);
            }
            if (extra) {
                for (const WorldMesh& pose : *extra) {
                    float other[3];
                    lightAt(pose, other);
                    if (luma(other) > luma(light)) std::copy_n(other, 3, light);
                }
            }
        }
        const std::size_t t = static_cast<std::size_t>(curY) * acc.size + curX;
        // Baked fog (scene_forward_clustered.glsl fog_process): from the
        // texel's centre in its rest pose to the scene's fixed eye.
        float fogAmount = 0.0f;
        // Props are fogged at runtime (assets/scene_fog.h, FogWeight): they
        // move, and the menu's shells fall through five units of depth.
        if (cx.in->fogDensity > 0.0f && !g.isProp) {
            float at[3] = {0.0f, 0.0f, 0.0f};
            for (int s = 0; s < n; ++s)
                for (int k = 0; k < 3; ++k)
                    for (int a = 0; a < 3; ++a)
                        at[a] += bs[s][k] * wm.pos[srcVerts[k] * 3 + a] / static_cast<float>(n);
            float d = 0.0f;
            for (int a = 0; a < 3; ++a) d += (at[a] - cx.in->eye[a]) * (at[a] - cx.in->eye[a]);
            fogAmount = 1.0f - std::exp(std::min(0.0f, -std::sqrt(d) * cx.in->fogDensity));
        }
        for (int s = 0; s < n; ++s) {
            float albedo[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            if (tex >= 0 && m.HasUvs()) {
                float uv[2] = {0.0f, 0.0f};
                for (int k = 0; k < 3; ++k) {
                    uv[0] += bs[s][k] * m.uvs[srcVerts[k] * 2];
                    uv[1] += bs[s][k] * m.uvs[srcVerts[k] * 2 + 1];
                }
                SampleAlbedo(*cx.in->albedo, tex, uv, albedo);
            }
            float lin[3];
            assets::SceneLinear(albedo, mat, light, lin);
            for (int c = 0; c < 3; ++c) lin[c] += (cx.in->fog[c] - lin[c]) * fogAmount;
            for (int c = 0; c < 3; ++c) acc.sum[t * 4 + c] += lin[c];
            // A prop's material alpha is drawn live (u_alpha = the material's
            // albedo alpha now): baking the load-time alpha too would leave
            // anything that loads at 0 and fades in (the CRT icons) invisible.
            // A merged batch has no live alpha and keeps it.
            acc.sum[t * 4 + 3] += albedo[3] * (g.isProp ? 1.0f : mat.alpha);
            ++acc.count[t];
        }
        n = 0;
    };
    assets::RasteriseTriangle(uvTex, acc.size, [&](int x, int y, const float* b) {
        if (x != curX || y != curY) {
            flush();
            curX = x;
            curY = y;
        }
        if (n < 4) {
            bs[n][0] = b[0];
            bs[n][1] = b[1];
            bs[n][2] = b[2];
            ++n;
        }
    });
    flush();
}

// Grades, dilates and encodes one accumulated page.
SurfacePage FinishPage(const Accum& acc, const assets::light::Grade& grade) {
    SurfacePage page;
    page.size = acc.size;
    page.alpha = acc.alpha;
    const std::size_t n = static_cast<std::size_t>(acc.size) * acc.size;
    page.rgba.assign(n * 4, 0);
    std::vector<uint8_t> mask(n, 0);
    for (std::size_t t = 0; t < n; ++t) {
        if (acc.count[t] == 0) continue;
        const float k = 1.0f / acc.count[t];
        const float lin[3] = {acc.sum[t * 4] * k, acc.sum[t * 4 + 1] * k, acc.sum[t * 4 + 2] * k};
        float srgb[3];
        assets::light::GradePixel(lin, grade, srgb);
        for (int c = 0; c < 3; ++c) page.rgba[t * 4 + c] = static_cast<uint8_t>(std::lround(srgb[c] * 255.0f));
        page.rgba[t * 4 + 3] =
            static_cast<uint8_t>(std::lround(std::clamp(acc.sum[t * 4 + 3] * k, 0.0f, 1.0f) * 255.0f));
        mask[t] = 1;
    }
    assets::Dilate(page.rgba, mask, acc.size, kDilate, 4);
    // Opaque: DXT1, 8 bytes a 4x4 block. Blended: DXT5, 16 bytes, smooth alpha.
    const std::size_t blockBytes = page.alpha ? 16 : 8;
    page.encoded.resize(n / 16 * blockBytes);
    uint8_t block[16 * 4];
    const int blocks = acc.size / 4;
    for (int by = 0; by < blocks; ++by) {
        for (int bx = 0; bx < blocks; ++bx) {
            for (int y = 0; y < 4; ++y) {
                const std::size_t row = static_cast<std::size_t>(by * 4 + y) * acc.size + bx * 4;
                std::copy_n(&page.rgba[row * 4], 16, &block[y * 16]);
            }
            stb_compress_dxt_block(&page.encoded[(static_cast<std::size_t>(by) * blocks + bx) * blockBytes], block,
                                   page.alpha ? 1 : 0, STB_DXT_HIGHQUAL);
        }
    }
    return page;
}

}  // namespace

bool BakeSurfaces(const assets::Room& room, const SurfaceBakeInput& in, SurfaceBakeResult& out) {
    out = SurfaceBakeResult{};
    const auto& batches = room.Batches();
    const auto& props = room.Props();
    if (in.sourceMaterial.size() != room.Sources().size() || in.propMaterial.size() != props.size() ||
        in.propAlpha.size() != props.size() || in.batchTexture.size() != batches.size() ||
        in.propTexture.size() != props.size() || !in.albedo ||
        (!in.propVariants.empty() && in.propVariants.size() != props.size()) ||
        (in.normal && (in.sourceNormal.size() != room.Sources().size() ||
                       (!in.propNormal.empty() && in.propNormal.size() != props.size())))) {
        out.error = "input tables do not match the room";
        return false;
    }

    // Shadows are cast by every visible surface that casts them, as Godot's
    // omni shadow pass takes every visible shadow-casting instance
    // (renderer_scene_cull.cpp:2476): the merged room and the props visible at
    // load, at their rest pose.
    std::vector<float> tris;
    std::vector<WorldMesh> batchWorld;
    batchWorld.reserve(batches.size());
    for (const assets::RoomBatch& b : batches) {
        batchWorld.push_back(ToWorld(b.mesh, core::Mat4{}));
        const WorldMesh& wm = batchWorld.back();
        for (std::size_t t = 0; t < b.mesh.indices.size() / 3; ++t) {
            if (!in.sourceCastsShadow.empty() && t < b.triSource.size() &&
                !in.sourceCastsShadow[static_cast<std::size_t>(b.triSource[t])]) {
                continue;
            }
            for (int k = 0; k < 3; ++k) {
                const uint16_t i = b.mesh.indices[t * 3 + k];
                tris.insert(tris.end(), &wm.pos[i * 3], &wm.pos[i * 3] + 3);
            }
        }
    }
    // A prop's texels take shadows from the static room only: bake_room poses
    // each prop where its reveal animation leaves it, but OTHER props would be
    // occluding from their rest poses, which may not match.
    assets::LightBvh propBvh;
    propBvh.Build(tris);
    std::vector<WorldMesh> propWorld;
    propWorld.reserve(props.size());
    for (std::size_t p = 0; p < props.size(); ++p) {
        propWorld.push_back(ToWorld(props[p].mesh, props[p].bakeWorld));
        const bool casts = in.propCastsShadow.empty() || in.propCastsShadow[p];
        if (!props[p].visibleAtLoad || props[p].IsTemplate() || !casts) continue;
        // It shadows the room from where it rests, not from its reveal pose.
        const WorldMesh castMesh = props[p].hasCastWorld ? ToWorld(props[p].mesh, props[p].castWorld) : WorldMesh{};
        const WorldMesh& wm = props[p].hasCastWorld ? castMesh : propWorld.back();
        for (uint16_t i : props[p].mesh.indices) tris.insert(tris.end(), &wm.pos[i * 3], &wm.pos[i * 3] + 3);
    }
    assets::LightBvh bvh;
    bvh.Build(tris);
    std::printf("surfaces: shadow BVH over %lu triangles\n", static_cast<unsigned long>(bvh.TriangleCount()));

    auto texOf = [&](int index) -> const assets::BakedTexture* {
        if (index < 0 || static_cast<std::size_t>(index) >= in.albedo->Textures().size()) return nullptr;
        return &in.albedo->Textures()[static_cast<std::size_t>(index)];
    };

    // Groups: each batch split by tier, each prop whole.
    std::vector<Group> groups;
    for (std::size_t b = 0; b < batches.size(); ++b) {
        const WorldMesh& wm = batchWorld[b];
        Group byTier[3];
        for (int t = 0; t < 3; ++t) { byTier[t].index = static_cast<int>(b); byTier[t].tier = t; }
        const uint32_t faces = static_cast<uint32_t>(batches[b].mesh.TriangleCount());
        for (uint32_t f = 0; f < faces; ++f) {
            const uint16_t* ix = &batches[b].mesh.indices[static_cast<std::size_t>(f) * 3];
            float c[3];
            for (int a = 0; a < 3; ++a) {
                c[a] = (wm.pos[ix[0] * 3 + a] + wm.pos[ix[1] * 3 + a] + wm.pos[ix[2] * 3 + a]) / 3.0f -
                       in.eye[a];
            }
            byTier[assets::Tier(std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]))].faces.push_back(f);
        }
        for (Group& g : byTier) {
            if (g.faces.empty()) continue;
            g.density = GroupDensity(wm, g.faces, texOf(in.batchTexture[b]), g.tier);
            groups.push_back(std::move(g));
        }
    }
    for (std::size_t p = 0; p < props.size(); ++p) {
        Group g;
        g.isProp = true;
        g.index = static_cast<int>(p);
        g.alpha = in.propAlpha[p] != 0;
        g.variant = !in.propVariants.empty() && !in.propVariants[p].empty();
        g.grade = p < in.propGrade.size() ? in.propGrade[p] : -1;
        if (g.grade >= static_cast<int>(in.propGrades.size())) g.grade = -1;
        const float* eye = GroupEye(in, g);
        // A prop takes the tier of where it is lit: the dealer and the items at
        // the table are T0; a prop in the lobby or the restroom is not.
        {
            const core::Mat4& w = props[p].bakeWorld;
            const float d[3] = {w.Tx() - eye[0], w.Ty() - eye[1], w.Tz() - eye[2]};
            g.tier = assets::Tier(std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
        }
        const uint32_t faces = static_cast<uint32_t>(props[p].mesh.TriangleCount());
        for (uint32_t f = 0; f < faces; ++f) g.faces.push_back(f);
        if (g.faces.empty()) continue;
        const int detail = p < in.propDetail.size() ? in.propDetail[p] : 0;
        g.density = GroupDensity(propWorld[p], g.faces, texOf(in.propTexture[p]), g.tier, detail);
        groups.push_back(std::move(g));
    }

    // The texel budget, before anything is unwrapped: area x density^2.
    for (Group& g : groups) {
        const WorldMesh& wm = g.isProp ? propWorld[static_cast<std::size_t>(g.index)]
                                       : batchWorld[static_cast<std::size_t>(g.index)];
        double area = 0.0;
        for (uint32_t f : g.faces) {
            const uint16_t* ix = &wm.mesh->indices[static_cast<std::size_t>(f) * 3];
            area += TriArea3(&wm.pos[ix[0] * 3], &wm.pos[ix[1] * 3], &wm.pos[ix[2] * 3]);
        }
        (g.isProp ? out.propTexels : out.texelsByTier)[g.tier] += area * g.density * g.density;
    }
    if (in.planOnly) {
        // The biggest spenders, so a density change is aimed, not guessed.
        std::vector<std::pair<double, std::size_t>> spend;
        for (std::size_t gi = 0; gi < groups.size(); ++gi) {
            const Group& g = groups[gi];
            const WorldMesh& wm = g.isProp ? propWorld[static_cast<std::size_t>(g.index)]
                                           : batchWorld[static_cast<std::size_t>(g.index)];
            double area = 0.0;
            for (uint32_t f : g.faces) {
                const uint16_t* ix = &wm.mesh->indices[static_cast<std::size_t>(f) * 3];
                area += TriArea3(&wm.pos[ix[0] * 3], &wm.pos[ix[1] * 3], &wm.pos[ix[2] * 3]);
            }
            spend.emplace_back(area * g.density * g.density, gi);
        }
        std::sort(spend.rbegin(), spend.rend());
        for (std::size_t k = 0; k < spend.size() && k < 25; ++k) {
            const Group& g = groups[spend[k].second];
            const std::string name = g.isProp ? props[static_cast<std::size_t>(g.index)].nodePath +
                                                    props[static_cast<std::size_t>(g.index)].templateKey
                                              : batches[static_cast<std::size_t>(g.index)].sourceGlb;
            std::printf("  %6.2fM  %s T%d %5.1f/u  %s\n", spend[k].first / 1e6, g.isProp ? "prop " : "batch",
                        g.tier, static_cast<double>(g.density), name.c_str());
        }
    }
    std::printf("surfaces: planned texels, batches T0 %.2fM T1 %.2fM T2 %.2fM; props T0 %.2fM T1 %.2fM T2 %.2fM\n",
                out.texelsByTier[0] / 1e6, out.texelsByTier[1] / 1e6, out.texelsByTier[2] / 1e6,
                out.propTexels[0] / 1e6, out.propTexels[1] / 1e6, out.propTexels[2] / 1e6);
    std::fflush(stdout);
    if (in.planOnly) return true;

    // Every group is unwrapped on its own, in parallel: its charts stay
    // together, so a mesh samples one page (one bind) unless it is larger than
    // a page. One shared atlas would scatter each mesh's charts over every
    // page (89 batches became 1,164 draws, 728 props 4,039).
    struct Unwrap {
        xatlas::Atlas* atlas = nullptr;
        bool multi = false;        // bigger than a page: its own pages
        int w = 0, h = 0;          // single-atlas size (not multi)
        int firstPage = -1;        // the page of atlas index 0
        int ox = 0, oy = 0;        // its offset on that page (not multi)
        int set = -1;              // its entry in `sets`
    };
    std::vector<Unwrap> unwrap(groups.size());
    const unsigned threads = in.threads > 0 ? static_cast<unsigned>(in.threads)
                                            : std::max(1u, std::thread::hardware_concurrency());
    {
        std::atomic<std::size_t> nextGroup{0};
        std::atomic<std::size_t> unwrapped{0};
        std::mutex errorMutex;
        auto run = [&](Group& g, const WorldMesh& wm, uint32_t resolution) -> xatlas::Atlas* {
            std::unordered_map<uint32_t, uint32_t> local;
            std::vector<uint32_t> idx;
            idx.reserve(g.faces.size() * 3);
            g.verts.clear();
            for (uint32_t f : g.faces) {
                for (int k = 0; k < 3; ++k) {
                    const uint32_t v = wm.mesh->indices[static_cast<std::size_t>(f) * 3 + k];
                    auto [it, fresh] = local.emplace(v, static_cast<uint32_t>(g.verts.size()));
                    if (fresh) g.verts.push_back(v);
                    idx.push_back(it->second);
                }
            }
            // xatlas sizes charts from positions: scaling them by the density
            // at texelsPerUnit 1 gives this group its own texel density.
            std::vector<float> pos(g.verts.size() * 3), nrm(g.verts.size() * 3);
            for (std::size_t v = 0; v < g.verts.size(); ++v) {
                for (int a = 0; a < 3; ++a) {
                    pos[v * 3 + a] = wm.pos[g.verts[v] * 3 + a] * g.density;
                    nrm[v * 3 + a] = wm.nrm[g.verts[v] * 3 + a];
                }
            }
            xatlas::MeshDecl decl;
            decl.vertexPositionData = pos.data();
            decl.vertexPositionStride = sizeof(float) * 3;
            decl.vertexNormalData = nrm.data();
            decl.vertexNormalStride = sizeof(float) * 3;
            decl.vertexCount = static_cast<uint32_t>(g.verts.size());
            decl.indexData = idx.data();
            decl.indexCount = static_cast<uint32_t>(idx.size());
            decl.indexFormat = xatlas::IndexFormat::UInt32;
            xatlas::Atlas* a = xatlas::Create();
            const xatlas::AddMeshError e = xatlas::AddMesh(a, decl, 1);
            if (e != xatlas::AddMeshError::Success) {
                const std::lock_guard<std::mutex> lock(errorMutex);
                out.error = std::string("xatlas AddMesh: ") + xatlas::StringForEnum(e);
                xatlas::Destroy(a);
                return nullptr;
            }
            xatlas::PackOptions pack;
            pack.resolution = resolution;
            pack.texelsPerUnit = 1.0f;
            pack.padding = kPadding;
            pack.bilinear = true;
            pack.blockAlign = true;
            pack.maxChartSize = static_cast<uint32_t>(in.pageSize) - 8;
            xatlas::Generate(a, xatlas::ChartOptions(), pack);
            return a;
        };
        auto worker = [&]() {
            for (;;) {
                const std::size_t gi = nextGroup.fetch_add(1);
                if (gi >= groups.size()) return;
                Group& g = groups[gi];
                const WorldMesh& wm = g.isProp ? propWorld[static_cast<std::size_t>(g.index)]
                                               : batchWorld[static_cast<std::size_t>(g.index)];
                xatlas::Atlas* a = run(g, wm, 0);
                if (!a) return;
                if (static_cast<int>(a->width) > in.pageSize || static_cast<int>(a->height) > in.pageSize) {
                    xatlas::Destroy(a);
                    a = run(g, wm, static_cast<uint32_t>(in.pageSize));
                    if (!a) return;
                    unwrap[gi].multi = true;
                } else {
                    unwrap[gi].w = static_cast<int>(a->width);
                    unwrap[gi].h = static_cast<int>(a->height);
                }
                unwrap[gi].atlas = a;
                const std::size_t d = unwrapped.fetch_add(1) + 1;
                if (d % 100 == 0 || d == groups.size()) {
                    std::printf("surfaces: unwrapped %lu / %lu meshes\n", static_cast<unsigned long>(d),
                                static_cast<unsigned long>(groups.size()));
                    std::fflush(stdout);
                }
            }
        };
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threads; ++t) pool.emplace_back(worker);
        for (std::thread& t : pool) t.join();
        if (!out.error.empty()) {
            for (Unwrap& u : unwrap) if (u.atlas) xatlas::Destroy(u.atlas);
            return false;
        }
    }

    // Pages: opaque groups shelf-packed, then the opaque multi-page groups;
    // the same again for the alpha groups. Props with material variants are
    // packed apart, onto pages of their own that are then cloned once per
    // variant: the clones share the UV2, and only those small pages repeat.
    struct PageSet {
        int base = 0, pages = 0;
        std::vector<int> cloneBase;   // per variant: the first page of its clone
    };
    std::vector<PageSet> sets;
    // Variant props are packed one set per variant count: a set is cloned once
    // per variant, so a prop with two materials does not pay for the clones a
    // three-material prop needs.
    auto variantCount = [&](std::size_t gi) {
        return groups[gi].variant ? in.propVariants[static_cast<std::size_t>(groups[gi].index)].size() : 0;
    };
    // A page is finished at one grade: props with their own grade get sets of
    // their own, one per grade.
    struct SetSpec { int alpha; bool variantSet; std::size_t count; int grade; };
    std::vector<int> gradesUsed = {-1};
    for (const Group& g : groups) {
        if (std::find(gradesUsed.begin(), gradesUsed.end(), g.grade) == gradesUsed.end()) gradesUsed.push_back(g.grade);
    }
    std::vector<SetSpec> specs;
    for (int grade : gradesUsed) {
        specs.push_back({0, false, 0, grade});
        specs.push_back({1, false, 0, grade});
        for (int alpha = 0; alpha < 2; ++alpha) {
            std::vector<std::size_t> counts;
            for (std::size_t gi = 0; gi < groups.size(); ++gi) {
                if (groups[gi].grade == grade && groups[gi].variant && groups[gi].alpha == (alpha == 1)) {
                    counts.push_back(variantCount(gi));
                }
            }
            std::sort(counts.begin(), counts.end());
            counts.erase(std::unique(counts.begin(), counts.end()), counts.end());
            for (std::size_t c : counts) specs.push_back({alpha, true, c, grade});
        }
    }
    for (const SetSpec& spec : specs) {
        const int alpha = spec.alpha;
        const bool variantSet = spec.variantSet;
        auto inSet = [&](std::size_t gi) {
            return groups[gi].alpha == (alpha == 1) && groups[gi].variant == variantSet &&
                   (!variantSet || variantCount(gi) == spec.count) && groups[gi].grade == spec.grade;
        };
        if (spec.grade >= 0) {
            bool any = false;
            for (std::size_t gi = 0; gi < groups.size() && !any; ++gi) any = inSet(gi);
            if (!any) continue;   // the scene's empty sets keep their old behaviour
        }
        const int setIndex = static_cast<int>(sets.size());
        std::vector<assets::PackRect> rects;
        std::vector<std::size_t> owner;
        for (std::size_t gi = 0; gi < groups.size(); ++gi) {
            if (!inSet(gi)) continue;
            unwrap[gi].set = setIndex;
            if (unwrap[gi].multi) continue;
            rects.push_back({unwrap[gi].w, unwrap[gi].h});
            owner.push_back(gi);
        }
        int shelfPages = 0;
        std::vector<assets::Placement> at = assets::ShelfPack(rects, in.pageSize, shelfPages);
        if (shelfPages < 0) {   // cannot happen: a chart bigger than a page is `multi`
            out.error = "a chart larger than a page reached the shelf packer";
            for (Unwrap& u : unwrap) if (u.atlas) xatlas::Destroy(u.atlas);
            return false;
        }
        // A set that fits one page gets the smallest power-of-two page that
        // holds it: the alpha props fill a sliver of 1024^2, and RGBA8 is 4 MB
        // there.
        int shelfSize = in.pageSize;
        while (shelfPages == 1 && shelfSize > 64) {
            int fit = 0;
            std::vector<assets::Placement> smaller = assets::ShelfPack(rects, shelfSize / 2, fit);
            if (fit != 1) break;
            shelfSize /= 2;
            at = std::move(smaller);
        }
        const int base = static_cast<int>(out.pages.size());
        for (std::size_t r = 0; r < at.size(); ++r) {
            Unwrap& u = unwrap[owner[r]];
            u.firstPage = base + at[r].page;
            u.ox = at[r].x;
            u.oy = at[r].y;
        }
        int pages = shelfPages;
        for (std::size_t gi = 0; gi < groups.size(); ++gi) {
            if (!inSet(gi) || !unwrap[gi].multi) continue;
            unwrap[gi].firstPage = base + pages;
            pages += static_cast<int>(std::max<uint32_t>(unwrap[gi].atlas->atlasCount, 1));
            if (groups[gi].isProp) out.propSplits += static_cast<int>(unwrap[gi].atlas->atlasCount) - 1;
        }
        for (int p = 0; p < pages; ++p) {
            SurfacePage pg;
            pg.size = p < shelfPages ? shelfSize : in.pageSize;
            pg.alpha = alpha == 1;
            pg.grade = spec.grade;
            out.pages.push_back(std::move(pg));
        }
        PageSet set;
        set.base = base;
        set.pages = pages;
        if (variantSet) {
            for (std::size_t v = 0; v < spec.count; ++v) {
                set.cloneBase.push_back(static_cast<int>(out.pages.size()));
                for (int p = 0; p < pages; ++p) out.pages.push_back(out.pages[static_cast<std::size_t>(base + p)]);
            }
        }
        sets.push_back(set);
        std::printf("surfaces: %s%s%s: %d pages (%d shared, %d for meshes bigger than a page)%s\n",
                    alpha ? "alpha" : "opaque", variantSet ? " variant props" : "",
                    spec.grade >= 0 ? (" at grade " + std::to_string(spec.grade)).c_str() : "", pages, shelfPages,
                    pages - shelfPages,
                    variantSet ? (", x" + std::to_string(set.cloneBase.size() + 1) + " materials").c_str() : "");
    }
    auto pageOf = [&](std::size_t gi, int atlasIndex) {
        const Unwrap& u = unwrap[gi];
        return u.multi ? u.firstPage + std::max(atlasIndex, 0) : u.firstPage;
    };
    // The page variant `v` of group `gi` colours where its base page is `page`.
    auto variantPage = [&](std::size_t gi, int page, std::size_t v) {
        const PageSet& set = sets[static_cast<std::size_t>(unwrap[gi].set)];
        return set.cloneBase[v] + (page - set.base);
    };
    auto uvOf = [&](std::size_t gi, const xatlas::Vertex& v, float uv[2]) {
        const Unwrap& u = unwrap[gi];
        uv[0] = v.uv[0] + (u.multi ? 0.0f : static_cast<float>(u.ox));
        uv[1] = v.uv[1] + (u.multi ? 0.0f : static_cast<float>(u.oy));
    };

    // Colour the texels, chart by chart. Charts are disjoint in texel space
    // (and groups disjoint rectangles), so threads never write the same texel.
    std::vector<Accum> accum(out.pages.size());
    for (std::size_t p = 0; p < accum.size(); ++p) {
        const int size = out.pages[p].size;
        accum[p].size = size;
        accum[p].alpha = out.pages[p].alpha;
        accum[p].sum.assign(static_cast<std::size_t>(size) * size * 4, 0.0f);
        accum[p].count.assign(static_cast<std::size_t>(size) * size, 0);
    }
    struct Work { std::size_t group; uint32_t chart; int variant; };   // variant -1: the base material
    std::vector<Work> work;
    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        const xatlas::Mesh& om = unwrap[gi].atlas->meshes[0];
        const int variants = groups[gi].variant
                                 ? static_cast<int>(in.propVariants[static_cast<std::size_t>(groups[gi].index)].size())
                                 : 0;
        for (int v = -1; v < variants; ++v) {
            for (uint32_t c = 0; c < om.chartCount; ++c) work.push_back({gi, c, v});
        }
    }
    std::vector<WorldMesh> propAlt(props.size());
    for (std::size_t p = 0; p < props.size(); ++p) {
        if (props[p].hasAltWorld) propAlt[p] = ToWorld(props[p].mesh, props[p].altWorld);
    }
    std::vector<std::vector<WorldMesh>> propMean(props.size());
    for (std::size_t p = 0; p < props.size(); ++p) {
        for (const core::Mat4& pose : props[p].meanPoses) propMean[p].push_back(ToWorld(props[p].mesh, pose));
    }
    std::vector<std::vector<WorldMesh>> propExtra(props.size());
    for (std::size_t p = 0; p < props.size(); ++p) {
        for (const core::Mat4& pose : props[p].extraPoses) propExtra[p].push_back(ToWorld(props[p].mesh, pose));
    }
    const Context cx{&in, &bvh, &propBvh, &room, &propAlt, &propMean, &propExtra};
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> done{0};
    auto worker = [&]() {
        for (;;) {
            const std::size_t w = next.fetch_add(1);
            if (w >= work.size()) return;
            const std::size_t gi = work[w].group;
            const Group& g = groups[gi];
            const xatlas::Mesh& om = unwrap[gi].atlas->meshes[0];
            const xatlas::Chart& chart = om.chartArray[work[w].chart];
            const int basePage = pageOf(gi, static_cast<int>(chart.atlasIndex));
            const int variant = work[w].variant;
            const PropVariantInput* vin =
                variant < 0 ? nullptr
                            : &in.propVariants[static_cast<std::size_t>(g.index)][static_cast<std::size_t>(variant)];
            Accum& acc = accum[static_cast<std::size_t>(
                variant < 0 ? basePage : variantPage(gi, basePage, static_cast<std::size_t>(variant)))];
            const WorldMesh& wm = g.isProp ? propWorld[static_cast<std::size_t>(g.index)]
                                           : batchWorld[static_cast<std::size_t>(g.index)];
            for (uint32_t fi = 0; fi < chart.faceCount; ++fi) {
                const uint32_t face = chart.faceArray[fi];
                float uv[3][2];
                uint32_t src[3];
                for (int k = 0; k < 3; ++k) {
                    const xatlas::Vertex& v = om.vertexArray[om.indexArray[face * 3 + k]];
                    uvOf(gi, v, uv[k]);
                    src[k] = g.verts[v.xref];
                }
                ColourFace(cx, g, wm, g.faces[face], uv, src, acc, vin);
            }
            const std::size_t d = done.fetch_add(1) + 1;
            if (d % 2000 == 0) {
                std::printf("surfaces: %lu / %lu charts\n", static_cast<unsigned long>(d),
                            static_cast<unsigned long>(work.size()));
                std::fflush(stdout);
            }
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) pool.emplace_back(worker);
    for (std::thread& t : pool) t.join();

    for (std::size_t p = 0; p < out.pages.size(); ++p) {
        const int grade = out.pages[p].grade;
        out.pages[p] = FinishPage(accum[p], grade >= 0 ? in.propGrades[static_cast<std::size_t>(grade)] : in.grade);
        out.pages[p].grade = grade;
        out.encodedBytes += out.pages[p].encoded.size();
    }

    // Rebuild the geometry around the pages. Static batches are MERGED per page
    // (they are world space already): one draw per page, not per source batch.
    std::unordered_map<int, std::size_t> openBatch;   // page -> index in out.batches
    auto appendToPage = [&](int page, const assets::Mesh& mesh, const std::vector<VertexSurface>& surfaces) {
        auto it = openBatch.find(page);
        if (it != openBatch.end() &&
            out.batches[it->second].mesh.VertexCount() + mesh.VertexCount() > static_cast<int>(kMaxVertices)) {
            it = openBatch.end();
        }
        if (it == openBatch.end()) {
            assets::RoomBatch b;
            b.sourceGlb = "page";
            b.materialIndex = page;
            b.page = page;
            out.batches.push_back(std::move(b));
            out.batchSurfaces.emplace_back();
            it = openBatch.insert_or_assign(page, out.batches.size() - 1).first;
        }
        std::vector<VertexSurface>& vs = out.batchSurfaces[it->second];
        vs.insert(vs.end(), surfaces.begin(), surfaces.end());
        assets::RoomBatch& b = out.batches[it->second];
        const uint16_t base = static_cast<uint16_t>(b.mesh.VertexCount());
        b.mesh.positions.insert(b.mesh.positions.end(), mesh.positions.begin(), mesh.positions.end());
        b.mesh.normals.insert(b.mesh.normals.end(), mesh.normals.begin(), mesh.normals.end());
        b.mesh.uvs.insert(b.mesh.uvs.end(), mesh.uvs.begin(), mesh.uvs.end());
        b.mesh.uv2.insert(b.mesh.uv2.end(), mesh.uv2.begin(), mesh.uv2.end());
        b.mesh.colours.insert(b.mesh.colours.end(), mesh.colours.begin(), mesh.colours.end());
        for (uint16_t i : mesh.indices) b.mesh.indices.push_back(static_cast<uint16_t>(base + i));
        for (std::size_t v = 0; v + 2 < mesh.positions.size(); v += 3) {
            for (int a = 0; a < 3; ++a) {
                b.boundsMin[a] = std::min(b.boundsMin[a], mesh.positions[v + a]);
                b.boundsMax[a] = std::max(b.boundsMax[a], mesh.positions[v + a]);
            }
        }
    };
    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        const Group& g = groups[gi];
        const xatlas::Mesh& om = unwrap[gi].atlas->meshes[0];
        const assets::Mesh& src = g.isProp ? props[static_cast<std::size_t>(g.index)].mesh
                                           : batches[static_cast<std::size_t>(g.index)].mesh;
        // Faces by page. A face xatlas did not place (a degenerate triangle,
        // which covers no pixel) rides on the group's first page.
        std::unordered_map<int, std::vector<uint32_t>> byPage;
        for (uint32_t f = 0; f < om.indexCount / 3; ++f) {
            const int ai = om.vertexArray[om.indexArray[f * 3]].atlasIndex;
            if (ai < 0) ++out.unplacedFaces;
            byPage[pageOf(gi, ai)].push_back(f);
        }
        for (auto& [page, faces] : byPage) {
            std::size_t start = 0;
            while (start < faces.size()) {
                assets::Mesh mesh;
                mesh.name = src.name;
                mesh.materialIndex = src.materialIndex;
                mesh.sourceMeshIndex = src.sourceMeshIndex;
                std::unordered_map<uint32_t, uint16_t> remap;
                std::vector<VertexSurface> surfaces;
                std::size_t f = start;
                for (; f < faces.size(); ++f) {
                    if (remap.size() + 3 > kMaxVertices) break;
                    for (int k = 0; k < 3; ++k) {
                        const uint32_t ov = om.indexArray[faces[f] * 3 + k];
                        auto [it, fresh] = remap.emplace(ov, static_cast<uint16_t>(remap.size()));
                        if (fresh) {
                            const xatlas::Vertex& xv = om.vertexArray[ov];
                            const uint32_t sv = g.verts[xv.xref];
                            surfaces.push_back(SurfaceOf(in, room, g, src, sv, g.faces[faces[f]]));
                            mesh.positions.insert(mesh.positions.end(), &src.positions[sv * 3], &src.positions[sv * 3] + 3);
                            if (src.normals.size() >= (sv + 1) * 3) {
                                mesh.normals.insert(mesh.normals.end(), &src.normals[sv * 3], &src.normals[sv * 3] + 3);
                            } else {
                                mesh.normals.insert(mesh.normals.end(), {0.0f, 1.0f, 0.0f});
                            }
                            if (src.HasUvs()) {
                                mesh.uvs.insert(mesh.uvs.end(), &src.uvs[sv * 2], &src.uvs[sv * 2] + 2);
                            } else {
                                mesh.uvs.insert(mesh.uvs.end(), {0.0f, 0.0f});
                            }
                            if (!src.joints.empty()) {   // the skin follows its vertex
                                mesh.joints.insert(mesh.joints.end(), &src.joints[sv * 4], &src.joints[sv * 4] + 4);
                                mesh.weights.insert(mesh.weights.end(), &src.weights[sv * 4], &src.weights[sv * 4] + 4);
                            }
                            if (src.HasColours()) {
                                mesh.colours.insert(mesh.colours.end(), &src.colours[sv * 4], &src.colours[sv * 4] + 4);
                            } else {
                                mesh.colours.insert(mesh.colours.end(), {255, 255, 255, 255});
                            }
                            float uv[2];
                            uvOf(gi, xv, uv);
                            const float size = static_cast<float>(out.pages[static_cast<std::size_t>(page)].size);
                            mesh.uv2.push_back(uv[0] / size);
                            mesh.uv2.push_back(uv[1] / size);
                        }
                        mesh.indices.push_back(it->second);
                    }
                }
                start = f;
                if (g.isProp) {
                    assets::RoomProp p = props[static_cast<std::size_t>(g.index)];
                    p.mesh = std::move(mesh);
                    p.page = page;
                    p.variants.clear();
                    if (g.grade >= 0) {
                        const assets::light::Grade& own = in.propGrades[static_cast<std::size_t>(g.grade)];
                        p.bakeBrightness = own.brightness;
                        p.bakeContrast = own.contrast;
                    }
                    if (g.variant) {
                        const auto& vins = in.propVariants[static_cast<std::size_t>(g.index)];
                        for (std::size_t v = 0; v < vins.size(); ++v) {
                            assets::PropVariant pv;
                            std::copy_n(vins[v].albedo, 4, pv.albedo);
                            std::copy_n(vins[v].emission, 3, pv.emission);
                            pv.textureKey = vins[v].textureKey;
                            pv.page = variantPage(gi, page, v);
                            p.variants.push_back(pv);
                        }
                    }
                    out.props.push_back(std::move(p));
                    out.propSurfaces.push_back(std::move(surfaces));
                } else {
                    appendToPage(page, mesh, surfaces);
                }
            }
        }
    }
    for (Unwrap& u : unwrap) xatlas::Destroy(u.atlas);

    // Over budget still returns the pages, so a failed bake can be inspected.
    const double mb = static_cast<double>(out.encodedBytes) / 1048576.0;
    if (mb > in.maxMegabytes) {
        char msg[128];
        std::snprintf(msg, sizeof msg, "pages total %.2f MB, over the %.1f MB budget", mb,
                      static_cast<double>(in.maxMegabytes));
        out.error = msg;
        return false;
    }
    return true;
}

void BakeDynLayers(const std::vector<assets::BakedLight>& dyn, const assets::LightBvh& bvh,
                   const core::Mat4& world, uint32_t layers, assets::Mesh& mesh) {
    const WorldMesh wm = ToWorld(mesh, world);
    const int n = mesh.VertexCount();
    mesh.colours.assign(static_cast<std::size_t>(n) * 4, 0);
    for (std::size_t li = 0; li < dyn.size() && li < 4; ++li) {
        assets::BakedLight unit = dyn[li];
        unit.energy = 1.0f;
        unit.colour[0] = unit.colour[1] = unit.colour[2] = 1.0f;
        const std::vector<assets::BakedLight> one{unit};
        for (int v = 0; v < n; ++v) {
            float c[3];
            assets::StaticLight(one, bvh, &wm.pos[static_cast<std::size_t>(v) * 3],
                                &wm.nrm[static_cast<std::size_t>(v) * 3], assets::kHomeSocket, 1.0f, c, layers);
            mesh.colours[static_cast<std::size_t>(v) * 4 + li] = assets::light::EncodeDynLayer(c[0]);
        }
    }
}

namespace {

// One refinement pass's shared midpoints, keyed by the edge's two vertices.
class EdgeMidpoints {
public:
    EdgeMidpoints(assets::Mesh& mesh, std::vector<VertexSurface>* surfaces, std::vector<float>& world)
        : mesh_(mesh), surfaces_(surfaces), world_(world) {}

    // The vertex at the middle of a-b, made once per edge; -1 at the cap.
    int Of(uint16_t a, uint16_t b) {
        const uint32_t key = a < b ? (static_cast<uint32_t>(a) << 16) | b : (static_cast<uint32_t>(b) << 16) | a;
        if (auto it = made_.find(key); it != made_.end()) return it->second;
        if (mesh_.VertexCount() >= 65535) {
            capped_ = true;
            return -1;
        }
        const int v = mesh_.VertexCount();
        Average(mesh_.positions, a, b, 3);
        Average(world_, a, b, 3);
        if (!mesh_.normals.empty()) {
            Average(mesh_.normals, a, b, 3);
            float* nv = &mesh_.normals[static_cast<std::size_t>(v) * 3];
            const float l = std::sqrt(nv[0] * nv[0] + nv[1] * nv[1] + nv[2] * nv[2]);
            if (l > 0.0f) for (int k = 0; k < 3; ++k) nv[k] /= l;
        }
        if (!mesh_.uvs.empty()) Average(mesh_.uvs, a, b, 2);
        if (!mesh_.uv2.empty()) Average(mesh_.uv2, a, b, 2);
        if (!mesh_.colours.empty()) {
            for (int k = 0; k < 4; ++k) {
                const int sum = mesh_.colours[a * 4u + k] + mesh_.colours[b * 4u + k];
                mesh_.colours.push_back(static_cast<uint8_t>((sum + 1) / 2));
            }
        }
        if (surfaces_) {
            VertexSurface s = (*surfaces_)[a];   // the material of one end: a face's own
            for (int k = 0; k < 3; ++k) s.texel[k] = 0.5f * ((*surfaces_)[a].texel[k] + (*surfaces_)[b].texel[k]);
            surfaces_->push_back(s);
        }
        made_.emplace(key, v);
        return v;
    }

    bool Capped() const { return capped_; }
    bool Any() const { return !made_.empty(); }

private:
    static void Average(std::vector<float>& attr, uint16_t a, uint16_t b, int width) {
        for (int k = 0; k < width; ++k) {
            attr.push_back(0.5f * (attr[static_cast<std::size_t>(a) * width + k] +
                                   attr[static_cast<std::size_t>(b) * width + k]));
        }
    }

    assets::Mesh& mesh_;
    std::vector<VertexSurface>* surfaces_;
    std::vector<float>& world_;
    std::unordered_map<uint32_t, int> made_;
    bool capped_ = false;
};

}  // namespace

FogTessellation TessellateForFog(assets::Mesh& mesh, std::vector<VertexSurface>* surfaces, const core::Mat4& world,
                                 float maxEdge) {
    FogTessellation result;
    const int n = mesh.VertexCount();
    if (!(maxEdge > 0.0f) || !mesh.joints.empty() || mesh.indices.size() < 3) return result;
    if (surfaces && surfaces->size() != static_cast<std::size_t>(n)) surfaces = nullptr;
    const auto sized = [n](const auto& attr, std::size_t width) {
        return attr.empty() || attr.size() == static_cast<std::size_t>(n) * width;
    };
    if (!sized(mesh.normals, 3) || !sized(mesh.uvs, 2) || !sized(mesh.uv2, 2) || !sized(mesh.colours, 4)) {
        return result;
    }
    const int before = mesh.TriangleCount();
    std::vector<float> wpos(static_cast<std::size_t>(n) * 3);
    for (int v = 0; v < n; ++v) {
        const float* p = &mesh.positions[static_cast<std::size_t>(v) * 3];
        for (int r = 0; r < 3; ++r) {
            wpos[static_cast<std::size_t>(v) * 3 + r] =
                world.m[r] * p[0] + world.m[4 + r] * p[1] + world.m[8 + r] * p[2] + world.m[12 + r];
        }
    }
    const float limit2 = maxEdge * maxEdge;
    const auto length2 = [&](uint16_t a, uint16_t b) {
        float d2 = 0.0f;
        for (int k = 0; k < 3; ++k) {
            const float e = wpos[a * 3u + k] - wpos[b * 3u + k];
            d2 += e * e;
        }
        return d2;
    };
    const auto isLong = [&](uint16_t a, uint16_t b) { return length2(a, b) > limit2; };
    for (int pass = 0; pass < 64; ++pass) {
        EdgeMidpoints mids(mesh, surfaces, wpos);
        std::vector<uint16_t> out;
        out.reserve(mesh.indices.size() * 2);
        for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const uint16_t v[3] = {mesh.indices[t], mesh.indices[t + 1], mesh.indices[t + 2]};
            int m[3];
            int split = 0;
            for (int e = 0; e < 3; ++e) {
                m[e] = isLong(v[e], v[(e + 1) % 3]) ? mids.Of(v[e], v[(e + 1) % 3]) : -1;
                if (m[e] >= 0) ++split;
            }
            const auto tri = [&out](int a, int b, int c) {
                out.push_back(static_cast<uint16_t>(a));
                out.push_back(static_cast<uint16_t>(b));
                out.push_back(static_cast<uint16_t>(c));
            };
            if (split == 0) {
                tri(v[0], v[1], v[2]);
            } else if (split == 3) {
                tri(v[0], m[0], m[2]);
                tri(m[0], v[1], m[1]);
                tri(m[2], m[1], v[2]);
                tri(m[0], m[1], m[2]);
            } else if (split == 1) {
                const int i = m[0] >= 0 ? 0 : (m[1] >= 0 ? 1 : 2);   // edge v[i] -> v[i + 1]
                tri(v[i], m[i], v[(i + 2) % 3]);
                tri(m[i], v[(i + 1) % 3], v[(i + 2) % 3]);
            } else {
                const int j = m[0] < 0 ? 0 : (m[1] < 0 ? 1 : 2);     // the edge left whole
                const int i = (j + 1) % 3, k = (j + 2) % 3;
                tri(m[i], v[k], m[k]);
                tri(v[j], v[i], m[i]);
                tri(v[j], m[i], m[k]);
            }
        }
        mesh.indices = std::move(out);
        result.capped = mids.Capped();
        if (!mids.Any() || mids.Capped()) break;
    }
    float longest2 = 0.0f;
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        for (int e = 0; e < 3; ++e) {
            longest2 = std::max(longest2, length2(mesh.indices[t + e], mesh.indices[t + (e + 1) % 3]));
        }
    }
    result.added = mesh.TriangleCount() - before;
    result.longestEdge = std::sqrt(longest2);
    return result;
}

void BakeFogLuminance(const std::vector<assets::BakedLight>& lights, const core::Mat4& world, uint32_t layers,
                      const float ambient[3], const std::vector<VertexSurface>* surfaces, assets::Mesh& mesh) {
    BakeFogLuminance(lights, std::vector<core::Mat4>{world}, layers, ambient, surfaces, mesh);
}

void BakeFogLuminance(const std::vector<assets::BakedLight>& lights, const std::vector<core::Mat4>& poses,
                      uint32_t layers, const float ambient[3], const std::vector<VertexSurface>* surfaces,
                      assets::Mesh& mesh) {
    const int n = mesh.VertexCount();
    std::vector<float> light(static_cast<std::size_t>(n) * 3, 0.0f);   // the mean, linear
    assets::LightBvh none;   // unshadowed: a brightness for the fog mix, not a picture
    none.Build({});
    const float k = poses.empty() ? 0.0f : 1.0f / static_cast<float>(poses.size());
    for (const core::Mat4& world : poses) {
        const WorldMesh wm = ToWorld(mesh, world);
        for (int v = 0; v < n; ++v) {
            const std::size_t i = static_cast<std::size_t>(v) * 3;
            float c[3];
            assets::StaticLight(lights, none, &wm.pos[i], &wm.nrm[i], assets::kHomeSocket, 1.0f, c, layers);
            for (int a = 0; a < 3; ++a) light[i + a] += (c[a] + ambient[a]) * k;
        }
    }
    const VertexSurface white;
    mesh.colours.assign(static_cast<std::size_t>(n) * 4, 0);
    for (int v = 0; v < n; ++v) {
        const std::size_t i = static_cast<std::size_t>(v);
        const VertexSurface& s =
            surfaces && i < surfaces->size() ? (*surfaces)[i] : white;
        float lin[3];
        assets::SceneLinear(s.texel, s.material, &light[i * 3], lin);
        const float x = 0.2126f * lin[0] + 0.7152f * lin[1] + 0.0722f * lin[2];
        mesh.colours[i * 4 + 3] = assets::EncodeFogLuminance(x);
    }
}

assets::ProbeGrid BakeProbes(const assets::Room& room, const SurfaceBakeInput& in, const float lo[3],
                             const float hi[3], float step, uint32_t layers) {
    assets::ProbeGrid grid;
    if (step <= 0.0f) return grid;
    std::vector<float> tris;
    for (const assets::RoomBatch& b : room.Batches()) {
        for (std::size_t t = 0; t < b.mesh.indices.size() / 3; ++t) {
            if (!in.sourceCastsShadow.empty() && t < b.triSource.size() &&
                !in.sourceCastsShadow[static_cast<std::size_t>(b.triSource[t])]) {
                continue;
            }
            for (int k = 0; k < 3; ++k) {
                const uint16_t i = b.mesh.indices[t * 3 + k];
                tris.insert(tris.end(), &b.mesh.positions[i * 3], &b.mesh.positions[i * 3] + 3);
            }
        }
    }
    assets::LightBvh bvh;
    bvh.Build(tris);
    for (int a = 0; a < 3; ++a) {
        grid.origin[a] = lo[a];
        grid.dims[a] = std::max(1, static_cast<int>(std::floor((hi[a] - lo[a]) / step + 1e-4f)) + 1);
    }
    grid.step = step;
    grid.rgb.assign(static_cast<std::size_t>(grid.dims[0]) * grid.dims[1] * grid.dims[2] * 3, 0.0f);
    static const float kAxes[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    std::atomic<int> nextZ{0};
    auto work = [&]() {
        for (int z = nextZ++; z < grid.dims[2]; z = nextZ++) {
            for (int y = 0; y < grid.dims[1]; ++y) {
                for (int x = 0; x < grid.dims[0]; ++x) {
                    const float at[3] = {lo[0] + step * static_cast<float>(x), lo[1] + step * static_cast<float>(y),
                                         lo[2] + step * static_cast<float>(z)};
                    float* out = &grid.rgb[((static_cast<std::size_t>(z) * grid.dims[1] + y) * grid.dims[0] + x) * 3];
                    for (const auto& n : kAxes) {
                        float c[3];
                        assets::StaticLight(in.lights, bvh, at, n, in.eye, 1.0f, c, layers);
                        for (int k = 0; k < 3; ++k) out[k] += c[k] / 6.0f;
                    }
                }
            }
        }
    };
    const unsigned threads = in.threads > 0 ? static_cast<unsigned>(in.threads)
                                            : std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) pool.emplace_back(work);
    for (std::thread& t : pool) t.join();
    return grid;
}

bool WritePagePpm(const SurfacePage& page, const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", page.size, page.size);
    for (std::size_t t = 0; t < page.rgba.size() / 4; ++t) std::fwrite(&page.rgba[t * 4], 1, 3, f);
    std::fclose(f);
    return true;
}

}  // namespace tools
