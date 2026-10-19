// Decoding and downscaling the room's albedo textures. Host tool only.
//
// stb_image is included here and nowhere else. The device ships no image
// decoder: it reads what the bake produced. Keeping the codec in one header
// that only the baker includes makes that true by construction.
#pragma once

#include "assets/room.h"
#include "assets/texture_pool.h"

#include <string>

namespace tools {

struct TextureBakeStats {
    int materialsSeen   = 0;
    int decoded         = 0;
    int shared          = 0;   // resolved to an entry another material had
    int colourOnly      = 0;   // no image; a flat baseColorFactor was used
    int failed          = 0;
    int batchesTextured = 0;
    int propsTextured   = 0;
};

// Fills `pool` and sets the texture index on every batch and prop in `room`.
//
// `edge` is the size textures are fitted to: the embedded images are far
// larger decoded than the Vita's VRAM. Images are only ever shrunk; a texture
// already smaller is left alone rather than blown up into blur.
//
// `extractedDir` is the exports folder (assets/extracted in the repo): inline
// meshes are keyed by a PNG path relative to it ("inline_textures/x.png",
// written by export_inline_meshes.gd) rather than by a (glb, material) pair.
TextureBakeStats BakeRoomTextures(const std::string& modelDir,
                                  const std::string& extractedDir, assets::Room& room,
                                  assets::TexturePool& pool, int edge);

// Decodes one inline texture ("inline_textures/x.png" under `extractedDir`)
// into `pool`, or finds it there already. Returns its index, or -1 when the
// file is missing or will not decode. For materials the room's own surfaces do
// not use: the ones a script swaps in (material variants).
int AddInlineTexture(const std::string& extractedDir, const std::string& key,
                     assets::TexturePool& pool, int edge);

}  // namespace tools
