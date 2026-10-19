#include "godot/audio_spatial.h"

#include <algorithm>
#include <cmath>

namespace godot {

namespace {

constexpr float kCmpEpsilon = 0.00001f;   // core/math/math_defs.h CMP_EPSILON

float DbToLinear(float db) { return std::exp(db * 0.11512925464970228420089957273422f); }
float LinearToDb(float linear) { return std::log(linear) * 8.6858896380650365530225783783321f; }

float Length(Vector3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

// audio_stream_player_3d.cpp:209-237
float AttenuationDb(const Spatial3D& s, float volume_db, float distance) {
    float att = 0.0f;
    switch (s.attenuation_model) {
    case 0: att = LinearToDb(1.0f / ((distance / s.unit_size) + kCmpEpsilon)); break;
    case 1: {
        const float d = distance / s.unit_size;
        att = LinearToDb(1.0f / (d * d + kCmpEpsilon));
        break;
    }
    case 2: att = -20.0f * std::log(distance / s.unit_size + kCmpEpsilon); break;
    default: break;   // ATTENUATION_DISABLED
    }
    att += volume_db;
    return std::min(att, s.max_db);
}

// Spcap::calculate for SPEAKER_MODE_STEREO (audio_stream_player_3d.cpp:40-97):
// front-left (-1,0,-1) and front-right (1,0,-1), normalised. They are
// orthogonal, so each speaker's effective_number_of_speakers is 1 + 0.5 = 1.5.
StereoGain SpcapStereo(Vector3 dir, float tightness) {
    constexpr float kInvSqrt2 = 0.70710678118654752f;
    constexpr float kEffective = 1.5f;
    const float dotL = (-dir.x - dir.z) * kInvSqrt2;
    const float dotR = (dir.x - dir.z) * kInvSqrt2;
    const float gl = 0.5f * std::pow(1.0f + dotL, tightness) / kEffective;
    const float gr = 0.5f * std::pow(1.0f + dotR, tightness) / kEffective;
    const float sum = gl * gl + gr * gr;
    if (sum <= 0.0f) return {0.0f, 0.0f};
    return {std::sqrt(gl * gl / sum), std::sqrt(gr * gr / sum)};
}

}  // namespace

// AudioStreamPlayer::_get_volume_vector: stereo is AudioFrame(v, v).
StereoGain GainPlain(float volume_db) {
    const float v = DbToLinear(volume_db);
    return {v, v};
}

// audio_stream_player_2d.cpp:172-211, the default listener (no AudioListener2D,
// identity canvas transform): the centre of the visible rect.
StereoGain Gain2D(const Spatial2D& s, float volume_db, Vector2 screen_size, float global_panning_strength) {
    const Vector2 listener{screen_size.x * 0.5f, screen_size.y * 0.5f};
    const float dx = s.global_position.x - listener.x;
    const float dy = s.global_position.y - listener.y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist > s.max_distance) return {0.0f, 0.0f};
    const float multiplier = std::pow(1.0f - dist / s.max_distance, s.attenuation) * DbToLinear(volume_db);
    float pan = screen_size.x > 0.0f ? dx / screen_size.x : 0.0f;
    pan = std::clamp(pan, -1.0f, 1.0f);
    pan *= s.panning_strength * global_panning_strength * 0.5f;
    pan = std::clamp(pan + 0.5f, 0.0f, 1.0f);
    return {(1.0f - pan) * multiplier, pan * multiplier};
}

// audio_stream_player_3d.cpp:417-447, with the attenuation high-shelf's gain
// (:438, db_att). PORT: doppler is not applied; it is off on every player in
// this game.
StereoGain Gain3D(const Spatial3D& s, float volume_db, Vector3 local_pos, float global_panning_strength) {
    const float dist = Length(local_pos);
    if (s.max_distance > 0.0f && dist > s.max_distance) return {0.0f, 0.0f};
    float multiplier = DbToLinear(AttenuationDb(s, volume_db, dist));
    if (s.max_distance > 0.0f) multiplier *= std::max(0.0f, 1.0f - dist / s.max_distance);
    const float tightness = global_panning_strength * 2.0f * s.panning_strength;
    // Vector3::normalized() of a zero vector stays zero.
    const Vector3 dir = dist > 0.0f ? Vector3{local_pos.x / dist, local_pos.y / dist, local_pos.z / dist}
                                    : Vector3{};
    const StereoGain pan = SpcapStereo(dir, tightness);
    StereoGain out{pan.l * multiplier, pan.r * multiplier};
    out.shelf = DbToLinear((1.0f - std::min(1.0f, multiplier)) * s.attenuation_filter_db);
    out.shelf_cutoff_hz = s.attenuation_filter_cutoff_hz;
    return out;
}

}  // namespace godot
