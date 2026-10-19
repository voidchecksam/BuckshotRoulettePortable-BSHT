// The UI shader: one textured, tinted, alpha-blended quad batch.
//
// GLSL 120 / GLES 1.00. `attribute`/`varying`, texture2D, gl_FragColor. The
// attribute NAMES match the slots Shader binds before linking (a_pos, a_uv,
// a_col); GLES2 has no layout qualifiers, so the names are the contract.
//
// Positions arrive in DESIGN PIXELS and are mapped to clip space here rather
// than on the CPU. The scene was authored at 960x540 and the Vita panel is
// 960x544, so a resolution change becomes one uniform rather than a rebuild of
// every quad.
//
// The fragment stage is one texture fetch and one multiply. Fill rate is this
// port's measured budget, and the UI blends over an already-shaded frame.
#pragma once

namespace render {
namespace shaders {

inline const char* kSpriteVertexSrc = R"GLSL(#version 120
attribute vec2 a_pos;      // design-space pixels, origin top-left
attribute vec2 a_uv;
attribute vec4 a_col;

// (2/width, -2/height). The negative Y flips the axis: UI coordinates run down
// from the top-left, clip space runs up from the centre.
uniform vec2 u_invHalfSize;

varying vec2 v_uv;
varying vec4 v_col;

void main() {
    vec2 ndc = a_pos * u_invHalfSize + vec2(-1.0, 1.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = a_uv;
    v_col = a_col;
}
)GLSL";

inline const char* kSpriteFragmentSrc = R"GLSL(#version 120
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
