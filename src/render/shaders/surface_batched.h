// Prop batching's programs: the base, DYN and FOG surface programs for a chunk
// of props drawn at once. Each vertex carries its prop's palette slot in
// a_uv2.x. The slot holds the prop's MVP as four columns, the same matrix the
// unbatched path sets as u_mvp, multiplied the same way so edges land on the
// same pixels (world rows dotted in the shader leave seams along the table),
// and its colour scale (tint x probe ratio x the environment's) and alpha,
// which ride to the fragment as a varying. The fragment work is the unbatched
// programs' exactly.
//
// kPropBatchSlots x 5 vec4 + a few stays inside the 128 vertex uniform vectors
// vitaGL reports. If the device rejects these programs, the renderer draws
// every prop the unbatched way and logs why.
//
// #version 120 is required: graphics_within_gles2_subset rejects anything
// else.
#pragma once

namespace render {

inline constexpr int kPropBatchSlots = 20;   // u_mvp_cols[80], u_tint[20] below

inline const char* kBatchedVertexSrc = R"GLSL(#version 120
attribute vec3 a_pos;
attribute vec2 a_uv;
attribute vec2 a_uv2;       // .x: the prop's palette slot

uniform vec4 u_mvp_cols[80];   // per slot: the prop's MVP, four columns
uniform vec4 u_tint[20];       // per slot: rgb colour scale, a = alpha
uniform vec2 u_snap;
uniform float u_affine;

varying vec3 v_uvw;
varying vec4 v_tint;

void main() {
    int s = int(a_uv2.x + 0.5);
    mat4 mvp = mat4(u_mvp_cols[4 * s], u_mvp_cols[4 * s + 1], u_mvp_cols[4 * s + 2], u_mvp_cols[4 * s + 3]);
    vec4 clip = mvp * vec4(a_pos, 1.0);
    if (clip.w > 0.0 && u_snap.x > 0.0) {
        vec2 ndc = clip.xy / clip.w;
        ndc = floor(ndc * u_snap + 0.5) / u_snap;
        clip.xy = ndc * clip.w;
    }
    gl_Position = clip;
    float w = mix(1.0, clip.w, u_affine);
    v_uvw = vec3(a_uv * w, w);
    v_tint = u_tint[s];
}
)GLSL";

inline const char* kBatchedFragmentSrc = R"GLSL(#version 120
uniform sampler2D u_tex;
uniform vec3 u_offset;

varying vec3 v_uvw;
varying vec4 v_tint;

void main() {
    vec4 t = texture2D(u_tex, v_uvw.xy / v_uvw.z);
    gl_FragColor = vec4(t.rgb * v_tint.rgb + u_offset, t.a * v_tint.a);
}
)GLSL";

// + the animated lights (the DYN program's terms).
inline const char* kBatchedDynVertexSrc = R"GLSL(#version 120
attribute vec3 a_pos;
attribute vec2 a_uv;
attribute vec4 a_col;
attribute vec2 a_uv2;

uniform vec4 u_mvp_cols[80];
uniform vec4 u_tint[20];
uniform vec2 u_snap;
uniform float u_affine;

varying vec3 v_uvw;
varying vec4 v_tint;
varying vec4 v_dyn;

void main() {
    int s = int(a_uv2.x + 0.5);
    mat4 mvp = mat4(u_mvp_cols[4 * s], u_mvp_cols[4 * s + 1], u_mvp_cols[4 * s + 2], u_mvp_cols[4 * s + 3]);
    vec4 clip = mvp * vec4(a_pos, 1.0);
    if (clip.w > 0.0 && u_snap.x > 0.0) {
        vec2 ndc = clip.xy / clip.w;
        ndc = floor(ndc * u_snap + 0.5) / u_snap;
        clip.xy = ndc * clip.w;
    }
    gl_Position = clip;
    float w = mix(1.0, clip.w, u_affine);
    v_uvw = vec3(a_uv * w, w);
    v_tint = u_tint[s];
    v_dyn = a_col * a_col * 4.0;
}
)GLSL";

inline const char* kBatchedDynFragmentSrc = R"GLSL(#version 120
uniform sampler2D u_tex;
uniform vec3 u_offset;
uniform vec3 u_dyn0;
uniform vec3 u_dyn1;
uniform vec3 u_dyn2;
uniform vec3 u_dyn3;

varying vec3 v_uvw;
varying vec4 v_tint;
varying vec4 v_dyn;

void main() {
    vec4 t = texture2D(u_tex, v_uvw.xy / v_uvw.z);
    vec3 c = t.rgb * v_tint.rgb + u_offset;
    float tg = dot(t.rgb, vec3(0.33333));
    c += tg * (v_dyn.r * u_dyn0 + v_dyn.g * u_dyn1 + v_dyn.b * u_dyn2 + v_dyn.a * u_dyn3);
    gl_FragColor = vec4(c, t.a * v_tint.a);
}
)GLSL";

// + Environment fog (the FOG program's terms). The distance to the eye is taken
// in view space from the clip position (the projection is a symmetric
// perspective, so view = (clip.x / P00, clip.y / P11, -clip.w)), where the
// unbatched program measures world - eye: the same length.
inline const char* kBatchedFogVertexSrc = R"GLSL(#version 120
attribute vec3 a_pos;
attribute vec2 a_uv;
attribute vec4 a_col;
attribute vec2 a_uv2;

uniform vec4 u_mvp_cols[80];
uniform vec4 u_tint[20];
uniform vec2 u_proj_scale;     // the projection's P00, P11
uniform float u_fog_density;
uniform vec4 u_fog_curve;      // assets::FogCurve (1 / ACES(white), brightness, contrast); w the fog's luminance
uniform float u_fog_grey;      // the fog's own grey (assets::FogGrey)
uniform vec2 u_snap;
uniform float u_affine;

varying vec3 v_uvw;
varying vec4 v_tint;
varying float v_fog;

void main() {
    int s = int(a_uv2.x + 0.5);
    mat4 mvp = mat4(u_mvp_cols[4 * s], u_mvp_cols[4 * s + 1], u_mvp_cols[4 * s + 2], u_mvp_cols[4 * s + 3]);
    vec4 clip = mvp * vec4(a_pos, 1.0);
    vec4 unsnapped = clip;
    if (clip.w > 0.0 && u_snap.x > 0.0) {
        vec2 ndc = clip.xy / clip.w;
        ndc = floor(ndc * u_snap + 0.5) / u_snap;
        clip.xy = ndc * clip.w;
    }
    gl_Position = clip;
    float w = mix(1.0, clip.w, u_affine);
    v_uvw = vec3(a_uv * w, w);
    v_tint = u_tint[s];
    vec3 view = vec3(unsnapped.x / u_proj_scale.x, unsnapped.y / u_proj_scale.y, unsnapped.w);
    float t = exp(-length(view) * u_fog_density);
    // Godot's fog, mixed in LINEAR light before the grade (assets::FogWeight,
    // line for line; inlined: vitaGL's translator takes no user functions).
    // a_col.a = the surface's linear luminance (assets::EncodeFogLuminance).
    float lumSurf = exp2(a_col.a * 20.0 - 10.0);
    float lumMix = t * lumSurf + (1.0 - t) * u_fog_curve.w;
    float acesSurf = 1.8 * lumSurf;
    acesSurf = (acesSurf * acesSurf + 0.0245786 * acesSurf - 0.000090537) / (0.983729 * acesSurf * acesSurf + 0.432951 * acesSurf + 0.238081);
    acesSurf = clamp(acesSurf * u_fog_curve.x, 0.0, 1.0);
    float greySurf = acesSurf < 0.0031308 ? 12.92 * acesSurf : 1.055 * pow(acesSurf, 0.4166667) - 0.055;
    greySurf = clamp(0.5 + (greySurf * u_fog_curve.y - 0.5) * u_fog_curve.z, 0.0, 1.0);
    float acesMix = 1.8 * lumMix;
    acesMix = (acesMix * acesMix + 0.0245786 * acesMix - 0.000090537) / (0.983729 * acesMix * acesMix + 0.432951 * acesMix + 0.238081);
    acesMix = clamp(acesMix * u_fog_curve.x, 0.0, 1.0);
    float greyMix = acesMix < 0.0031308 ? 12.92 * acesMix : 1.055 * pow(acesMix, 0.4166667) - 0.055;
    greyMix = clamp(0.5 + (greyMix * u_fog_curve.y - 0.5) * u_fog_curve.z, 0.0, 1.0);
    float span = greySurf - u_fog_grey;
    v_fog = abs(span) > 0.002 ? clamp((greySurf - greyMix) / span, 0.0, 1.0) : 1.0 - t;
}
)GLSL";

inline const char* kBatchedFogFragmentSrc = R"GLSL(#version 120
uniform sampler2D u_tex;
uniform vec3 u_offset;
uniform vec3 u_fog;

varying vec3 v_uvw;
varying vec4 v_tint;
varying float v_fog;

void main() {
    vec4 t = texture2D(u_tex, v_uvw.xy / v_uvw.z);
    vec3 c = t.rgb * v_tint.rgb + u_offset;
    gl_FragColor = vec4(mix(c, u_fog, v_fog), t.a * v_tint.a);
}
)GLSL";

}  // namespace render
