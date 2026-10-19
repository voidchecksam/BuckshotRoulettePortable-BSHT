#include "godot/particle_sim.h"

#include <algorithm>
#include <cmath>

namespace godot {

namespace {

// A float in [0, 1) from (seed, particle, cycle, stream): splitmix-style.
float Rand01(uint32_t seed, uint32_t i, uint32_t cycle, uint32_t stream) {
    uint64_t z = (static_cast<uint64_t>(seed) << 32) ^ (static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ull) ^
                 (static_cast<uint64_t>(cycle) << 20) ^ (static_cast<uint64_t>(stream) * 0xBF58476D1CE4E5B9ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<float>(z >> 40) / static_cast<float>(1ull << 24);
}

float Curve9(const float c[9], float t) {
    const float x = std::clamp(t, 0.0f, 1.0f) * 8.0f;
    const int a = std::min(static_cast<int>(x), 7);
    return c[a] + (c[a + 1] - c[a]) * (x - static_cast<float>(a));
}

}  // namespace

void ParticleSim::Bind(const SceneTree& tree) {
    systems_.clear();
    for (Node* n : tree.nodes_in_tree_order()) {
        const auto* p = cast_to<GPUParticles3D>(n);
        if (!p || p->get_look().amount <= 0 || p->get_look().texture.empty()) continue;
        System s;
        s.node = p;
        s.particles.resize(static_cast<std::size_t>(p->get_look().amount));
        systems_.push_back(std::move(s));
    }
}

// The spread cone (process_particles in ParticleProcessMaterial's shader),
// flatness 0: two angles in [-spread, spread], rotated onto `direction`.
void ParticleSim::Spawn(System& s, std::size_t i, int cycle, double birth) const {
    const GPUParticles3D::Look& L = s.node->get_look();
    const uint32_t seed = s.node->get_random_seed();
    const auto r = [&](uint32_t stream) { return Rand01(seed, static_cast<uint32_t>(i), static_cast<uint32_t>(cycle), stream); };
    System::P& p = s.particles[i];
    p.cycle = cycle;
    p.birth = birth;
    const core::Mat4& w = s.node->get_global_transform();
    p.origin[0] = w.m[12];
    p.origin[1] = w.m[13];
    p.origin[2] = w.m[14];

    const float spread = L.spread * 3.14159265f / 180.0f;
    const float a1 = (r(1) * 2.0f - 1.0f) * spread;
    const float a2 = (r(2) * 2.0f - 1.0f) * spread;
    float yzZ = std::cos(a2);
    yzZ = yzZ / std::max(0.0001f, std::sqrt(std::fabs(yzZ)));
    const float sd[3] = {std::sin(a1) * yzZ, std::sin(a2), std::cos(a1) * yzZ};
    float dir[3] = {L.direction[0], L.direction[1], L.direction[2]};
    float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len <= 0.0f) { dir[0] = 0; dir[1] = 0; dir[2] = 1; len = 1; }
    for (float& c : dir) c /= len;
    // binormal = cross((0,1,0), dir); normal = cross(binormal, dir).
    float bn[3] = {dir[2], 0.0f, -dir[0]};
    float bl = std::sqrt(bn[0] * bn[0] + bn[2] * bn[2]);
    if (bl < 0.0001f) { bn[0] = 0; bn[1] = dir[2]; bn[2] = -dir[1]; bl = std::sqrt(bn[1] * bn[1] + bn[2] * bn[2]); }
    for (float& c : bn) c /= bl;
    const float nm[3] = {bn[1] * dir[2] - bn[2] * dir[1], bn[2] * dir[0] - bn[0] * dir[2], bn[0] * dir[1] - bn[1] * dir[0]};
    const float speed = L.velocity[0] + (L.velocity[1] - L.velocity[0]) * r(3);
    float local[3];
    for (int a = 0; a < 3; ++a) local[a] = (bn[a] * sd[0] + nm[a] * sd[1] + dir[a] * sd[2]) * speed;
    // local_coords off: the velocity leaves in the emitter's orientation.
    for (int a = 0; a < 3; ++a) p.vel[a] = w.m[a] * local[0] + w.m[4 + a] * local[1] + w.m[8 + a] * local[2];
    p.scaleRand = r(4);
}

void ParticleSim::Step() {
    quads_.clear();
    for (std::size_t si = 0; si < systems_.size(); ++si) {
        System& s = systems_[si];
        const GPUParticles3D& n = *s.node;
        if (n.get_restart_count() != s.restarts) {
            s.restarts = n.get_restart_count();
            for (System::P& p : s.particles) p = System::P{};
        }
        if (s.restarts == 0 || !n.is_visible_in_tree()) continue;   // never started
        const GPUParticles3D::Look& L = n.get_look();
        const double life = std::max(n.get_lifetime(), 1e-3);
        const double T = n.get_system_time();
        const std::size_t count = s.particles.size();
        for (std::size_t i = 0; i < count; ++i) {
            const double rp = static_cast<double>(i) / static_cast<double>(count) * (1.0 - L.explosiveness);
            const double cycles = T / life - rp;
            if (cycles >= 0.0) {
                int k = static_cast<int>(std::floor(cycles));
                if (n.get_one_shot()) k = 0;
                System::P& p = s.particles[i];
                const double birth = (static_cast<double>(k) + rp) * life;
                // Emitted if born before emission stopped, whenever the simulation looks.
                const bool may = n.get_one_shot() ? p.cycle < 0 : birth < n.get_emit_end_time();
                if (k > p.cycle && may) Spawn(s, i, k, birth);
            }
            const System::P& p = s.particles[i];
            if (p.cycle < 0) continue;
            const double age = T - p.birth;
            if (age < 0.0 || age >= life) continue;
            const float t = static_cast<float>(age);
            ParticleQuad q;
            for (int a = 0; a < 3; ++a) q.pos[a] = p.origin[a] + p.vel[a] * t + 0.5f * L.gravity[a] * t * t;
            const float scale = L.scale[0] + (L.scale[1] - L.scale[0]) * p.scaleRand;
            q.size = scale * Curve9(L.scale_curve, static_cast<float>(age / life)) * L.size[0];
            q.system = static_cast<int>(si);
            if (q.size > 0.0f) quads_.push_back(q);
        }
    }
}

}  // namespace godot
