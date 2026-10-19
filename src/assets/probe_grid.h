// The light probe grid.
//
// Every prop's texels hold its colour at its bake pose. When the gun is picked
// up or an item carried to the grid, the static light around it changes; the
// bake stores the linear static light (with shadows) on a regular grid over
// the table, and each frame the CPU scales a moved prop by
//   ratio = probe(pivot now) / probe(bake pivot)
// per channel, passed to the surface shader's u_scale. Exact at the bake pose,
// per object, nothing lit per vertex or per pixel. Headless: the bake writes
// it, the device reads it, the tests check both.
#pragma once

#include <vector>

namespace assets {

struct ProbeGrid {
    float origin[3] = {0.0f, 0.0f, 0.0f};   // world position of point (0, 0, 0)
    float step = 0.5f;                      // spacing, world units
    int dims[3] = {0, 0, 0};                // points along x, y, z
    // Linear RGB per point, x fastest: ((z * dims[1] + y) * dims[0] + x) * 3.
    std::vector<float> rgb;

    bool Empty() const { return rgb.empty() || dims[0] <= 0 || dims[1] <= 0 || dims[2] <= 0; }
    // Trilinear, clamped to the grid's edge. False when the grid is empty.
    bool Sample(const float pos[3], float out[3]) const;
    // True when `pos` lies in the grid's box (edges included); never for an
    // empty grid.
    bool Contains(const float pos[3]) const;
};

// Largest brightening a ratio may apply: a pivot moved from shadow into a pool
// of light would otherwise blow out.
inline constexpr float kMaxPropRatio = 4.0f;
// Below this the bake-pose light carries no information (a black baked texel
// stays black at any ratio), so the channel is left at 1.
inline constexpr float kMinProbeLight = 1e-4f;

// probe(now) / probe(baked) per channel, clamped to [0, kMaxPropRatio]; 1 for
// an empty grid or a dark bake point.
void PropRatio(const ProbeGrid& grid, const float pivotNow[3], const float bakePivot[3], float out[3]);

// For a prop an animation throws out of the lit room (assets/offstage.h):
// beyond the probes it is dark (0), inside them PropRatio capped at 1. Its
// texels are baked at the table, the brightest place it goes, and a single
// probe at its pivot would light the face when it leans in under the lamp
// (Godot keeps it dark). The bake covers the prop's way back with probes, so
// it walks out of the dark into the light. An empty grid says nothing: 1.
void OffstageRatio(const ProbeGrid& grid, const float pivotNow[3], const float bakePivot[3], float out[3]);

}  // namespace assets
