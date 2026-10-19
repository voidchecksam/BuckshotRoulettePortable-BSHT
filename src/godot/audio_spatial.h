// What Godot 4.1.4's AudioServer sends to the left and right channels for each
// kind of player. Every player in this game is an AudioStreamPlayer2D or 3D;
// volume_db alone would put the 2D sounds and the music ~9 dB hot (a +5 dB
// gunshot clips the bus) and the health counter's 3D speakers ~15 dB quiet.
//
// Transcribed from:
//   scene/audio/audio_stream_player.cpp     _get_volume_vector (stereo)
//   scene/2d/audio_stream_player_2d.cpp:149 _update_panning
//   scene/3d/audio_stream_player_3d.cpp:209 _get_attenuation_db,
//                                       :372 _update_panning, :40 Spcap
// Pure and headless: the audio sink, MusicRouter and StreamRouter all call it.
#pragma once

#include "godot/value_types.h"

namespace godot {

// What the AudioServer applies to a player's playbacks: linear gain per output
// channel, and a 3D player's attenuation high-shelf (core::GodotHighShelf at
// shelf_cutoff_hz; 1 = flat, as for every 2D or plain player).
struct StereoGain {
    float l = 1.0f;
    float r = 1.0f;
    float shelf = 1.0f;
    float shelf_cutoff_hz = 5000.0f;
};

// project.godot sets neither audio/general/2d_panning_strength nor
// 3d_panning_strength, so both are Godot's default.
inline constexpr float kDefaultPanningStrength = 0.5f;

// AudioStreamPlayer2D's mix inputs, Godot's defaults until the export says
// otherwise. `global_position` is in canvas pixels; with no AudioListener2D
// and no Camera2D the listener is the centre of the visible rect.
struct Spatial2D {
    Vector2 global_position;
    float max_distance = 2000.0f;
    float attenuation = 1.0f;
    float panning_strength = 1.0f;
};

// AudioStreamPlayer3D's. attenuation_model: 0 inverse, 1 inverse square,
// 2 logarithmic, 3 disabled (AudioStreamPlayer3D::AttenuationModel).
struct Spatial3D {
    float unit_size = 10.0f;
    float max_db = 3.0f;
    float max_distance = 0.0f;
    int   attenuation_model = 0;
    float panning_strength = 1.0f;
    float attenuation_filter_db = -24.0f;
    float attenuation_filter_cutoff_hz = 5000.0f;
};

StereoGain GainPlain(float volume_db);
StereoGain Gain2D(const Spatial2D& s, float volume_db, Vector2 screen_size,
                  float global_panning_strength = kDefaultPanningStrength);
// `local_pos` is the player's origin in the listener's (the current camera's)
// orthonormalised space: -z ahead, +x right.
StereoGain Gain3D(const Spatial3D& s, float volume_db, Vector3 local_pos,
                  float global_panning_strength = kDefaultPanningStrength);

}  // namespace godot
