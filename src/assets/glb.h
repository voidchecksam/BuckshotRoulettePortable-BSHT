// Headless glTF 2.0 / GLB loader.
//
// No GL, no SDL: pure parsing, unit-testable without a window and the same on
// arm-vita-eabi. Uploading to the GPU is render/'s job.
//
// It reads the subset the exporter produces (export_scene_meshes.gd):
//   mode          TRIANGLES only
//   POSITION      FLOAT VEC3     (required)
//   NORMAL        FLOAT VEC3     (required)
//   TEXCOORD_0    FLOAT VEC2     (optional)
//   COLOR_0       FLOAT VEC4     (optional)
//   JOINTS_0 / WEIGHTS_0         (skinned meshes)
//   indices       UNSIGNED_INT, UNSIGNED_SHORT or UNSIGNED_BYTE
//   bufferViews   tightly packed, no byteStride
// Anything outside that is reported, never skipped: a dropped mesh is a hole
// in the room that is painful to trace back to its cause.
//
// Indices are always 16-bit here. The exporter writes UNSIGNED_INT (5125), and
// GLES2 has no 32-bit indices without GL_OES_element_index_uint, which is not
// among the Vita's 48 extensions: 32-bit indices would run on desktop and fail
// on hardware. The largest primitive in v2.2's library has 37,244 vertices, so
// every mesh downconverts losslessly; TooManyVertices makes a library that
// breaks this fail here rather than on the device.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

struct Mesh {
    std::string name;

    std::vector<float>    positions;   // 3 per vertex
    std::vector<float>    normals;     // 3 per vertex
    std::vector<float>    uvs;         // 2 per vertex, empty if absent
    // 4 per vertex: TANGENT xyz + handedness w, empty if absent. Bake input
    // only (the normal maps); the room file does not carry it.
    std::vector<float>    tangents;
    // 2 per vertex: the surface page's UV set. No source model carries
    // TEXCOORD_1, so the bake generates it.
    std::vector<float>    uv2;
    std::vector<uint8_t>  colours;     // 4 per vertex RGBA; filled by bake step
    // JOINTS_0 / WEIGHTS_0, 4 per vertex, empty for an unskinned mesh.
    // A joint is an index into the glTF skin's joint list = the Skin's bind.
    std::vector<uint8_t>  joints;
    std::vector<float>    weights;
    std::vector<uint16_t> indices;     // always 16-bit, see header comment

    int materialIndex = -1;

    // Index of the glTF mesh this primitive came from. Model::meshes is
    // per-primitive, so this is not the position in that vector: a glTF mesh
    // with 3 primitives contributes 3 entries sharing one sourceMeshIndex. The
    // mesh-name join table is keyed by glTF mesh index, so without this a
    // scene node would resolve to the wrong geometry, with plausible results.
    int sourceMeshIndex = -1;

    int VertexCount()   const { return static_cast<int>(positions.size() / 3); }
    int TriangleCount() const { return static_cast<int>(indices.size() / 3); }
    bool HasUvs()     const { return !uvs.empty(); }
    bool HasColours() const { return !colours.empty(); }
};

// The base-colour image for one material, still in its encoded form.
//
// The port ships no image decoder. These are decoded only by the offline baker,
// a host tool that may link stb_image; the device reads pixels already decoded,
// downscaled and packed. Keeping them encoded keeps the loader free of any
// codec, and the library's PNGs are never decoded on a 333 MHz core.
struct MaterialImage {
    // Offset and length inside the GLB's BIN chunk. Empty when the material has
    // no base-colour texture (69 of the 311 materials in v2.2's library).
    const uint8_t* data = nullptr;
    std::size_t    size = 0;

    // Multiplied into the texture, or used alone when there is none. 112 of the
    // 123 materials leave this white and carry all their colour in the image.
    float baseColour[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    bool HasImage() const { return data != nullptr && size > 0; }
};

struct Model {
    std::string       name;

    // Indexed by Mesh::materialIndex. Pointers reference the caller's GLB
    // buffer, so they are valid only while that buffer is alive: nothing is
    // copied.
    std::vector<MaterialImage> materials;

    // One entry per glTF PRIMITIVE, not per glTF mesh. A primitive is the
    // renderable unit: one material, one draw call. The extracted library holds
    // 947 glTF meshes but 1,426 primitives, so the two counts differ and
    // conflating them makes the manifest cross-check meaningless.
    std::vector<Mesh> meshes;

    // Number of glTF "meshes" entries this file contained. Kept only so the
    // loader can be checked against _manifest.json, which counts those.
    int sourceMeshCount = 0;

    int TotalTriangles() const {
        int n = 0;
        for (const Mesh& m : meshes) n += m.TriangleCount();
        return n;
    }
};

enum class GlbError {
    None = 0,
    FileNotFound,
    TooSmall,
    BadMagic,
    BadVersion,
    TruncatedChunk,
    MissingJsonChunk,
    MissingBinChunk,
    MalformedJson,
    NoMeshes,
    UnsupportedMode,
    MissingPosition,
    UnsupportedComponentType,
    AccessorOutOfRange,
    TooManyVertices,
};

// Human-readable reason, for logs and test failure messages. Never returns null.
const char* GlbErrorText(GlbError e);

// Returns GlbError::None on success. `out` is cleared first, and is left empty
// on failure rather than partly filled: a half-loaded model rendering with
// holes is worse than no model at all.
GlbError LoadGlb(const char* path, Model& out);

// Same, for an in-memory buffer. This is what the malformed-input tests drive,
// since constructing a broken file on disk for each case would be far noisier.
GlbError LoadGlbFromMemory(const uint8_t* data, std::size_t size, Model& out);

}  // namespace assets
