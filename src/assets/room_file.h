// The baked room format.
//
// The extracted .glb library is over 200 MB, most of it embedded PNG and
// TANGENT data the renderer never touches. It cannot ship in a vpk, and parsing
// glTF, lighting and merging on a 333 MHz Cortex-A9 would cost a long load
// every launch. So the pipeline runs offline and writes this: a header, then
// per batch a fixed-size record followed by raw interleaved vertices and 16-bit
// indices. No compression, no variable-length fields, no parsing: the Vita
// freads it and hands the buffers to glBufferData.
//
// The layout matches render::Vertex exactly, so the loaded bytes need no
// conversion. A static_assert in room_file.cpp pins that; a change to the
// vertex layout changes the format version with it.
#pragma once

#include <functional>

#include "assets/room.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace assets {

// Bumped whenever the byte layout changes. A stale file is rejected, never
// misread: an older room read as a newer one can look right and be unable to
// move, on a device with no debugger.
//   v2   props
//   v3   each prop indexes a live node hierarchy: animation moves ancestors
//   v4   the albedo texture pool, and a texture index per batch and prop
//   v5   each prop's visibility at load
//   v6   node and prop own-visibility, so a script can show a parent
//   v7   each prop's surface index; template props (a PackedScene's meshes)
//   v8   each texture's format: RGBA8, DXT1 or DXT5
//   v9   each prop's material variants
//   v10  each prop's bake pivot, and the light probe grid
//   v11  each variant's albedo-texture key
//   v12  prop skin streams
//   v13  a prop's own bake grade
//   v14  a_col.a in fog scenes is the surface's luminance (assets/scene_fog.h)
inline constexpr uint32_t kRoomFileVersion = 14;

enum class RoomFileError {
    None = 0,
    FileNotFound,
    TooSmall,
    BadMagic,
    BadVersion,
    Truncated,
    Inconsistent,     // header counts disagree with the batch records
};

const char* RoomFileErrorText(RoomFileError e);

// Written as-is. 36 bytes: 3+3+2 floats and 4 bytes of baked colour.
struct RoomFileVertex {
    float   pos[3];
    float   nrm[3];
    float   uv[2];
    uint8_t col[4];
};

bool WriteRoomFile(const char* path, const Room& room);

// Fills `out` with batches whose meshes are ready to upload. Bounds come from
// the file, so area culling needs no geometry pass at load.
//
// `progress`, when given, is called with the fraction of the file read so far
// (0..1, never decreasing, ending at exactly 1 on success). The room read is the
// boot's one long blocking call, so this is what drives the loading bar.
RoomFileError ReadRoomFile(const char* path, Room& out,
                           const std::function<void(float)>& progress = {});

// Reports what a written file contains, for the bake tool's output.
struct RoomFileStats {
    uint32_t batches   = 0;
    uint32_t props     = 0;
    uint32_t nodes     = 0;
    uint32_t vertices  = 0;
    uint32_t indices   = 0;
    uint64_t bytes     = 0;
};
RoomFileStats MeasureRoom(const Room& room);

// Byte offsets of the header fields, from offsetof on the real on-disk struct.
// Tests corrupt specific fields to prove the reader rejects bad files; offsets
// taken from the struct itself cannot drift when the header gains a field.
struct RoomFileOffsets {
    static const std::size_t kVersion;
    static const std::size_t kTotalVertices;
};

}  // namespace assets
