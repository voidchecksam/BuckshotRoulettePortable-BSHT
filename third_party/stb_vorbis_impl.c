// The single implementation translation unit.
//
// Vendored (public domain, v1.22) because no Vorbis decoder was available:
// no ffmpeg, no sox, no soundfile, vdpm's channel tool is missing, and Godot
// stores the .ogg data compressed rather than decoding it.
//
// It earns its place twice over -- the offline SFX bake AND on-device music
// streaming use the same decoder, so there is one dependency rather than
// libogg + libvorbis + libvorbisfile.
//
// PUSHDATA is disabled: we only ever pull from a file or a memory buffer.
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
