// The one object shader, GLSL 120 / GLES 1.00.
//
// Every surface was baked offline to the colour the player sees (Godot's
// light, shadows, ACES, sRGB, brightness/contrast/saturation and posterize),
// so the fragment stage fetches a texel and applies one multiply-add:
//   u_scale  the environment's animated brightness (exact) times a moving
//            prop's probe ratio times its albedo change since load;
//   u_offset the environment brightness's grey offset, 0.1 * (1 - b / 1.49).
// One fetch on an interpolated UV: no dependent read, and the cost of the
// shader the hardware probe measured holding 60 (12.19 / 13.33 ms).
//
// The vertex stage keeps the optional PS1 snap and affine mapping (--psx), off
// by default: the original has neither.
//
// #version 120 is required: graphics_within_gles2_subset rejects anything
// else.
#pragma once

namespace render {

inline const char* kSurfaceVertexSrc = R"GLSL(#version 120
attribute vec3 a_pos;
attribute vec3 a_nrm;
attribute vec2 a_uv;       // the page UV (room v8)
attribute vec4 a_col;      // the animated lights' baked layers

uniform mat4 u_mvp;
uniform vec2 u_snap;       // virtual raster size; 0 = no snap
uniform float u_affine;    // 1: affine UV (PS1 swim); 0: perspective-correct

varying vec3 v_uvw;        // xy = uv * w, z = w

void main() {
    vec4 clip = u_mvp * vec4(a_pos, 1.0);
    if (clip.w > 0.0 && u_snap.x > 0.0) {
        vec2 ndc = clip.xy / clip.w;
        ndc = floor(ndc * u_snap + 0.5) / u_snap;
        clip.xy = ndc * clip.w;
    }
    gl_Position = clip;
    float w = mix(1.0, clip.w, u_affine);
    v_uvw = vec3(a_uv * w, w);
}
)GLSL";

inline const char* kSurfaceFragmentSrc = R"GLSL(#version 120
uniform sampler2D u_tex;   // the baked page: final, graded sRGB
uniform vec3  u_scale;
uniform vec3  u_offset;
uniform float u_alpha;

varying vec3 v_uvw;

void main() {
    vec4 t = texture2D(u_tex, v_uvw.xy / v_uvw.z);
    gl_FragColor = vec4(t.rgb * u_scale + u_offset, t.a * u_alpha);
}
)GLSL";

// The DYN variant, bound only while one of the animated lights (muzzle flash,
// barrel mask, inspect mask, zippo) burns. a_col holds each light's scalar at
// energy 1, encoded 255 * sqrt(v / 4); u_dynN is its linear colour times its
// energy now times the display-space gain. Added on top of the texel in
// display space, where Godot adds it before the tonemap: a recorded
// difference.
inline const char* kSurfaceDynVertexSrc = R"GLSL(#version 120
attribute vec3 a_pos;
attribute vec3 a_nrm;
attribute vec2 a_uv;
attribute vec4 a_col;

uniform mat4 u_mvp;
uniform vec2 u_snap;
uniform float u_affine;

varying vec3 v_uvw;
varying vec4 v_dyn;        // each light's scalar, decoded

void main() {
    vec4 clip = u_mvp * vec4(a_pos, 1.0);
    if (clip.w > 0.0 && u_snap.x > 0.0) {
        vec2 ndc = clip.xy / clip.w;
        ndc = floor(ndc * u_snap + 0.5) / u_snap;
        clip.xy = ndc * clip.w;
    }
    gl_Position = clip;
    float w = mix(1.0, clip.w, u_affine);
    v_uvw = vec3(a_uv * w, w);
    v_dyn = a_col * a_col * 4.0;
}
)GLSL";

inline const char* kSurfaceDynFragmentSrc = R"GLSL(#version 120
uniform sampler2D u_tex;
uniform vec3  u_scale;
uniform vec3  u_offset;
uniform float u_alpha;
uniform vec3  u_dyn0;
uniform vec3  u_dyn1;
uniform vec3  u_dyn2;
uniform vec3  u_dyn3;

varying vec3 v_uvw;
varying vec4 v_dyn;

void main() {
    vec4 t = texture2D(u_tex, v_uvw.xy / v_uvw.z);
    vec3 c = t.rgb * u_scale + u_offset;
    // Scaled by the texel's grey, not its colour: Godot greys the whole pixel
    // (saturation 0.26) after the light is added, so the felt does not tint it.
    float tg = dot(t.rgb, vec3(0.33333));
    c += tg * (v_dyn.r * u_dyn0 + v_dyn.g * u_dyn1 + v_dyn.b * u_dyn2 + v_dyn.a * u_dyn3);
    gl_FragColor = vec4(c, t.a * u_alpha);
}
)GLSL";

// The FOG variant, bound in a scene whose Environment has fog on, for whatever
// the bake did not fog (assets/scene_fog.h): Godot's exponential fog,
// transmission exp(-length(view position) * density)
// (scene_forward_clustered.glsl fog_process), per vertex, mixed by the weight
// that gives the grade of Godot's linear mix (assets::FogWeight). u_fog is the
// fog colour through the scene's grade.
inline const char* kSurfaceFogVertexSrc = R"GLSL(#version 120
attribute vec3 a_pos;
attribute vec3 a_nrm;
attribute vec2 a_uv;
attribute vec4 a_col;

uniform mat4 u_mvp;
uniform mat4 u_model;      // identity for the world-space batches
uniform vec3 u_eye;
uniform float u_fog_density;
uniform vec4 u_fog_curve;      // assets::FogCurve (1 / ACES(white), brightness, contrast); w the fog's luminance
uniform float u_fog_grey;      // the fog's own grey (assets::FogGrey)
uniform vec2 u_snap;
uniform float u_affine;

varying vec3 v_uvw;
varying float v_fog;

void main() {
    vec4 clip = u_mvp * vec4(a_pos, 1.0);
    if (clip.w > 0.0 && u_snap.x > 0.0) {
        vec2 ndc = clip.xy / clip.w;
        ndc = floor(ndc * u_snap + 0.5) / u_snap;
        clip.xy = ndc * clip.w;
    }
    gl_Position = clip;
    float w = mix(1.0, clip.w, u_affine);
    v_uvw = vec3(a_uv * w, w);
    vec3 world = (u_model * vec4(a_pos, 1.0)).xyz;
    float t = exp(-length(world - u_eye) * u_fog_density);
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

inline const char* kSurfaceFogFragmentSrc = R"GLSL(#version 120
uniform sampler2D u_tex;
uniform vec3  u_scale;
uniform vec3  u_offset;
uniform float u_alpha;
uniform vec3  u_fog;

varying vec3 v_uvw;
varying float v_fog;

void main() {
    vec4 t = texture2D(u_tex, v_uvw.xy / v_uvw.z);
    vec3 c = t.rgb * u_scale + u_offset;
    gl_FragColor = vec4(mix(c, u_fog, v_fog), t.a * u_alpha);
}
)GLSL";

// The MOVE variant, bound only while the camera is near a light an animation
// moves (assets::kMovingLight): that light is evaluated per vertex from where
// it is now (Godot's omni attenuation, decay 1, times N.L) and added in
// display space by the texel's grey, as the DYN lights are.
inline const char* kSurfaceMoveVertexSrc = R"GLSL(#version 120
attribute vec3 a_pos;
attribute vec3 a_nrm;
attribute vec2 a_uv;
attribute vec4 a_col;

uniform mat4 u_mvp;
uniform mat4 u_model;
uniform vec3 u_light_pos;
uniform float u_light_range;
uniform vec2 u_snap;
uniform float u_affine;

varying vec3 v_uvw;
varying float v_light;

void main() {
    vec4 clip = u_mvp * vec4(a_pos, 1.0);
    if (clip.w > 0.0 && u_snap.x > 0.0) {
        vec2 ndc = clip.xy / clip.w;
        ndc = floor(ndc * u_snap + 0.5) / u_snap;
        clip.xy = ndc * clip.w;
    }
    gl_Position = clip;
    float w = mix(1.0, clip.w, u_affine);
    v_uvw = vec3(a_uv * w, w);
    vec3 world = (u_model * vec4(a_pos, 1.0)).xyz;
    vec3 n = normalize((u_model * vec4(a_nrm, 0.0)).xyz);
    vec3 l = u_light_pos - world;
    float d = max(length(l), 0.0001);
    float r = d / u_light_range;
    float atten = pow(max(1.0 - r * r * r * r, 0.0), 2.0) / d;
    v_light = atten * max(dot(n, l / d), 0.0);
}
)GLSL";

inline const char* kSurfaceMoveFragmentSrc = R"GLSL(#version 120
uniform sampler2D u_tex;
uniform vec3  u_scale;
uniform vec3  u_offset;
uniform float u_alpha;
uniform vec3  u_light;     // display-space colour * energy * gain

varying vec3 v_uvw;
varying float v_light;

void main() {
    vec4 t = texture2D(u_tex, v_uvw.xy / v_uvw.z);
    vec3 c = t.rgb * u_scale + u_offset;
    c += dot(t.rgb, vec3(0.33333)) * v_light * u_light;
    gl_FragColor = vec4(c, t.a * u_alpha);
}
)GLSL";

}  // namespace render
