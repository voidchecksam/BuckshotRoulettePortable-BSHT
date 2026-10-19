#include "assets/inline_meshes.h"

#include "core/json_lite.h"

#include <cstdio>

namespace assets {

namespace {

using core::json::FindArray;
using core::json::ReadNumberArray;
using core::json::ReadStringTopLevel;
using core::json::Span;
using core::json::SplitObjects;

void ReadFloats(const char* begin, const char* end, const char* key,
                std::vector<float>& out) {
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (!FindArray(begin, end, key, &ab, &ae)) return;

    std::vector<double> v;
    ReadNumberArray(ab, ae, v);
    out.reserve(v.size());
    for (double d : v) out.push_back(static_cast<float>(d));
}

}  // namespace

bool InlineMeshLibrary::LoadFromString(const char* json) {
    meshes_.clear();
    stats_ = InlineMeshStats{};
    if (!json) return false;

    const char* begin = json;
    const char* end = json + std::char_traits<char>::length(json);

    const char* mb = nullptr;
    const char* me = nullptr;
    if (!FindArray(begin, end, "meshes", &mb, &me)) return false;

    std::vector<Span> objs;
    SplitObjects(mb, me, objs);
    if (objs.empty()) return false;

    for (const Span& o : objs) {
        InlineMesh im;
        // TOP LEVEL: a mesh object contains surfaces, and reading a plain key
        // would find whatever a nested object happened to have first.
        im.nodePath = ReadStringTopLevel(o.first, o.second, "path");
        im.className = ReadStringTopLevel(o.first, o.second, "class");
        if (im.nodePath.empty()) continue;

        const char* sb = nullptr;
        const char* se = nullptr;
        if (FindArray(o.first, o.second, "surfaces", &sb, &se)) {
            std::vector<Span> surfs;
            SplitObjects(sb, se, surfs);

            for (const Span& s : surfs) {
                Mesh m;
                m.name = im.nodePath;
                ReadFloats(s.first, s.second, "positions", m.positions);
                ReadFloats(s.first, s.second, "normals", m.normals);
                ReadFloats(s.first, s.second, "uvs", m.uvs);

                const char* ib = nullptr;
                const char* ie = nullptr;
                if (FindArray(s.first, s.second, "indices", &ib, &ie)) {
                    std::vector<int> idx;
                    core::json::ReadIntArray(ib, ie, idx);
                    m.indices.reserve(idx.size());
                    for (int i : idx) {
                        // 16-bit only; GLES2 has no 32-bit index path. The
                        // exporter already rejects anything over 65535, so this
                        // is a belt-and-braces clamp rather than a real case.
                        m.indices.push_back(static_cast<uint16_t>(i & 0xFFFF));
                    }
                }

                if (m.positions.empty() || m.indices.empty()) continue;

                // Normals are relied on by the light bake. Synthesise a neutral
                // set rather than leave the array a different length to
                // positions, which would read out of bounds.
                if (m.normals.size() != m.positions.size()) {
                    m.normals.assign(m.positions.size(), 0.0f);
                    for (std::size_t i = 2; i < m.normals.size(); i += 3) {
                        m.normals[i] = 1.0f;
                    }
                }

                InlineMaterial mat;
                const char* matb = nullptr;
                const char* mate = nullptr;
                if (core::json::FindObject(s.first, s.second, "material", &matb, &mate)) {
                    mat.textureFile = ReadStringTopLevel(matb, mate, "texture_file");
                    mat.transparent =
                        core::json::ReadBoolTopLevel(matb, mate, "transparent", false);
                    std::vector<float> emission;
                    ReadFloats(matb, mate, "emission", emission);
                    for (std::size_t k = 0; k < 3 && k < emission.size(); ++k) {
                        mat.emission[k] = emission[k];
                    }
                }
                im.materials.push_back(std::move(mat));

                stats_.vertices += m.VertexCount();
                stats_.triangles += m.TriangleCount();
                ++stats_.surfaces;
                im.surfaces.push_back(std::move(m));
            }
        }

        if (im.surfaces.empty()) continue;
        ++stats_.nodes;
        meshes_.push_back(std::move(im));
    }

    return !meshes_.empty();
}

const InlineMesh* InlineMeshLibrary::Find(const std::string& nodePath) const {
    for (const InlineMesh& m : meshes_) {
        if (m.nodePath == nodePath) return &m;
    }
    return nullptr;
}

bool InlineMeshLibrary::LoadFromFile(const char* path) {
    meshes_.clear();
    stats_ = InlineMeshStats{};
    if (!path) return false;

    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;

    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); return false; }

    std::string buf;
    buf.resize(static_cast<std::size_t>(n));
    const std::size_t got = std::fread(&buf[0], 1, static_cast<std::size_t>(n), f);
    std::fclose(f);
    buf.resize(got);

    return LoadFromString(buf.c_str());
}

}  // namespace assets
