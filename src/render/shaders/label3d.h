// Label3D: world-space glyph quads from the font atlas.
//
// Unshaded and alpha-blended, as Godot's Label3D material is. The atlas is
// LUMINANCE_ALPHA with luminance pinned white, so `texel * colour` tints the
// text with no extra work per fragment.
#pragma once

namespace render {
namespace shaders {

inline const char* kLabel3DVertexSrc = R"GLSL(#version 120
attribute vec3 a_pos;      // world space
attribute vec2 a_uv;
attribute vec4 a_col;

uniform mat4 u_mvp;

varying vec2 v_uv;
varying vec4 v_col;

void main() {
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    v_uv = a_uv;
    v_col = a_col;
}
)GLSL";

inline const char* kLabel3DFragmentSrc = R"GLSL(#version 120
#ifdef GL_ES
precision mediump float;
#endif

uniform sampler2D u_tex;

varying vec2 v_uv;
varying vec4 v_col;

void main() {
    gl_FragColor = texture2D(u_tex, v_uv) * v_col;
}
)GLSL";

}  // namespace shaders
}  // namespace render
