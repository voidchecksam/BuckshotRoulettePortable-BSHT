// The one fullscreen pass, GLSL 120 / GLES 1.00: the 720x408 scene upscaled
// to the panel. See render/post.h.
#pragma once

namespace render {

// One fetch, no maths. The original's only screen effect (posterize 7) is
// baked into every surface page instead.
inline const char* kBlitVertexSrc = R"GLSL(#version 120
attribute vec3 a_pos;
attribute vec2 a_uv;
varying vec2 v_uv;
void main() {
    gl_Position = vec4(a_pos, 1.0);
    v_uv = a_uv;
}
)GLSL";

inline const char* kBlitFragmentSrc = R"GLSL(#version 120
uniform sampler2D u_scene;
varying vec2 v_uv;
void main() {
    gl_FragColor = texture2D(u_scene, v_uv);
}
)GLSL";

}  // namespace render
