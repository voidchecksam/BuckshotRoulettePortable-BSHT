#include "godot/animation.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace godot {
namespace {

using assets::Interp;
using assets::Key;
using assets::Track;
using assets::Value;
using assets::ValueKind;

constexpr int kLoopNone = 0;
constexpr int kLoopLinear = 1;

bool IsNumeric(ValueKind k) { return k == ValueKind::Int || k == ValueKind::Float; }

// Animation::_find, forward only. godot: animation.cpp:2312-2353
int FindKey(const std::vector<Key>& keys, double time) {
    const int len = static_cast<int>(keys.size());
    if (len == 0) return -2;
    int low = 0;
    int high = len - 1;
    int middle = 0;
    while (low <= high) {
        middle = (low + high) / 2;
        const double keyTime = keys[static_cast<std::size_t>(middle)].time;
        if (is_equal_approx(time, keyTime)) return middle;
        if (time < keyTime) {
            high = middle - 1;
        } else {
            low = middle + 1;
        }
    }
    if (keys[static_cast<std::size_t>(middle)].time > time) --middle;
    return middle;
}

// Animation::interpolate_variant for the kinds a track can hold.
// godot: animation.cpp:5664-5942
Value InterpolateVariant(const Value& a, const Value& b, double c) {
    if (a.kind != b.kind) {
        if (IsNumeric(a.kind) && IsNumeric(b.kind)) {
            Value out;
            out.kind = ValueKind::Float;
            out.f[0] = static_cast<float>(a.f[0] + (static_cast<double>(b.f[0]) - a.f[0]) * c);
            return out;
        }
        return a;
    }
    Value out = a;
    const float fc = static_cast<float>(c);
    switch (a.kind) {
    case ValueKind::None:
        return Value{};
    case ValueKind::Int: {
        const auto va = static_cast<int64_t>(a.f[0]);
        const auto vb = static_cast<int64_t>(b.f[0]);
        out.f[0] = static_cast<float>(static_cast<int64_t>(va + static_cast<float>(vb - va) * fc));
        return out;
    }
    case ValueKind::Float:
        out.f[0] = static_cast<float>(a.f[0] + (static_cast<double>(b.f[0]) - a.f[0]) * c);
        return out;
    case ValueKind::Vec2:
    case ValueKind::Vec3:
    case ValueKind::Color:
        for (int i = 0; i < 4; ++i) out.f[i] = a.f[i] + (b.f[i] - a.f[i]) * fc;
        return out;
    case ValueKind::Quat: {
        const Quaternion q = slerp({a.f[0], a.f[1], a.f[2], a.f[3]}, {b.f[0], b.f[1], b.f[2], b.f[3]}, fc);
        out.f[0] = q.x; out.f[1] = q.y; out.f[2] = q.z; out.f[3] = q.w;
        return out;
    }
    case ValueKind::Str:
        // PORT: Godot mixes strings as a typewriter effect; no track in
        // animations.json animates a string, so the first key is held.
        return a;
    default:
        // BOOL and everything without a case: Godot's default branch.
        return c < 0.5 ? a : b;
    }
}

// Animation::_cubic_interpolate_in_time(Variant). godot: animation.cpp:2396-2470
Value CubicVariant(const Value& pre, const Value& a, const Value& b, const Value& post, double c,
                   double preT, double toT, double postT) {
    const bool allNumeric = IsNumeric(pre.kind) && IsNumeric(a.kind) && IsNumeric(b.kind) &&
                            IsNumeric(post.kind);
    const bool anyFloat = pre.kind == ValueKind::Float || a.kind == ValueKind::Float ||
                          b.kind == ValueKind::Float || post.kind == ValueKind::Float;
    if (allNumeric && anyFloat) {
        Value out;
        out.kind = ValueKind::Float;
        out.f[0] = static_cast<float>(
            cubic_interpolate_in_time(a.f[0], b.f[0], pre.f[0], post.f[0], c, toT, preT, postT));
        return out;
    }
    if (pre.kind != a.kind || b.kind != a.kind || post.kind != a.kind) return a;
    if (a.kind == ValueKind::Vec2 || a.kind == ValueKind::Vec3) {
        Value out = a;
        const int n = a.kind == ValueKind::Vec2 ? 2 : 3;
        for (int i = 0; i < n; ++i) {
            out.f[i] = static_cast<float>(
                cubic_interpolate_in_time(a.f[i], b.f[i], pre.f[i], post.f[i], c, toT, preT, postT));
        }
        return out;
    }
    // PORT: QUATERNION uses spherical_cubic_interpolate_in_time; animations.json
    // has no cubic quaternion keys, so this takes the linear (slerp) branch.
    return InterpolateVariant(a, b, c);
}

// The neighbouring keys and blend inputs of Animation::_interpolate.
// godot: animation.cpp:2514-2629 (forward only)
struct Segment {
    int idx = 0;
    int next = 0;
    int pre = 0;
    int post = 0;
    double c = 0.0;
};

Segment Neighbours(const std::vector<Key>& keys, double time, int len, bool cubic, bool wrap,
                   double length) {
    Segment s;
    int idx = FindKey(keys, time);
    const int maxi = len - 1;
    const bool startEdge = idx == -1;
    const bool endEdge = idx >= maxi;
    double delta = 0.0;
    double from = 0.0;
    const auto t = [&keys](int i) { return static_cast<double>(keys[static_cast<std::size_t>(i)].time); };

    if (!wrap) {
        if (startEdge) idx = 0;
        s.next = std::clamp(idx + 1, 0, maxi);
        if (cubic) {
            s.pre = std::clamp(idx - 1, 0, maxi);
            s.post = std::clamp(idx + 2, 0, maxi);
        }
    } else {
        if (startEdge) idx = maxi;
        s.next = posmod(idx + 1, len);
        if (cubic) {
            s.pre = posmod(idx - 1, len);
            s.post = posmod(idx + 2, len);
        }
        if (startEdge) {
            const double endtime = std::max(length - t(idx), 0.0);
            delta = endtime + t(s.next);
            from = endtime + time;
        } else if (endEdge) {
            delta = (length - t(idx)) + t(s.next);
            from = time - t(idx);
        }
    }
    if (!startEdge && !endEdge) {
        delta = t(s.next) - t(idx);
        from = time - t(idx);
    }
    s.idx = idx;
    s.c = is_zero_approx(delta) ? 0.0 : from / delta;
    return s;
}

// The cubic times of Animation::_interpolate. godot: animation.cpp:2657-2664
void CubicTimes(const std::vector<Key>& keys, const Segment& s, bool wrap, double length,
                double& preT, double& toT, double& postT) {
    const auto t = [&keys](int i) { return static_cast<double>(keys[static_cast<std::size_t>(i)].time); };
    const double at = t(s.idx);
    if (!wrap) {
        preT = t(s.pre) - at;
        toT = t(s.next) - at;
        postT = t(s.post) - at;
        return;
    }
    preT = s.pre > s.idx ? -length + t(s.pre) - at : t(s.pre) - at;
    toT = s.next < s.idx ? length + t(s.next) - at : t(s.next) - at;
    postT = (s.next < s.idx || s.post <= s.idx) ? length + t(s.post) - at : t(s.post) - at;
}

// Animation::_interpolate. godot: animation.cpp:2496-2698
bool Interpolate(const std::vector<Key>& keys, double time, Interp interp, bool loopWrap,
                 const assets::Animation& anim, Value& out) {
    const int len = FindKey(keys, anim.length) + 1;
    if (len <= 0) return false;
    if (len == 1) {
        out = keys[0].value;
        return true;
    }
    // PORT: ping-pong (loop mode 2) takes the non-wrapping branch; no animation uses it.
    const bool wrap = loopWrap && anim.loopMode == kLoopLinear;
    const bool cubic = interp == Interp::Cubic;
    const Segment s = Neighbours(keys, time, len, cubic, wrap, anim.length);
    const Key& k = keys[static_cast<std::size_t>(s.idx)];

    double c = s.c;
    if (k.transition == 0.0f) {
        out = k.value;
        return true;
    }
    if (k.transition != 1.0f) c = ease(c, k.transition);

    const Value& next = keys[static_cast<std::size_t>(s.next)].value;
    switch (interp) {
    case Interp::Nearest:
        out = k.value;
        return true;
    case Interp::Linear:
        out = InterpolateVariant(k.value, next, c);
        return true;
    case Interp::Cubic: {
        double preT = 0.0, toT = 0.0, postT = 0.0;
        CubicTimes(keys, s, wrap, anim.length, preT, toT, postT);
        out = CubicVariant(keys[static_cast<std::size_t>(s.pre)].value, k.value, next,
                           keys[static_cast<std::size_t>(s.post)].value, c, preT, toT, postT);
        return true;
    }
    }
    out = k.value;
    return true;
}

const Track* TrackAt(const assets::Animation& anim, int track) {
    if (track < 0 || track >= static_cast<int>(anim.tracks.size())) return nullptr;
    return &anim.tracks[static_cast<std::size_t>(track)];
}

bool TransformTrack(const assets::Animation& anim, int track, double time, assets::TrackType type,
                    Value& out) {
    const Track* t = TrackAt(anim, track);
    if (!t || t->type != type || t->keys.empty()) return false;
    return Interpolate(t->keys, time, t->interp, t->loopWrap, anim, out);
}

// Animation::_track_get_key_indices_in_range. godot: animation.cpp:2737-2789
void KeysInRange(const std::vector<Key>& keys, double fromTime, double toTime, bool backward,
                 std::vector<int>& out) {
    const int len = static_cast<int>(keys.size());
    if (len == 0) return;
    int from = 0;
    int to = len - 1;
    const auto t = [&keys](int i) { return static_cast<double>(keys[static_cast<std::size_t>(i)].time); };
    if (!backward) {
        while (t(from) < fromTime || is_equal_approx(t(from), fromTime)) {
            if (to < ++from) return;
        }
        while (t(to) > toTime && !is_equal_approx(t(to), toTime)) {
            if (--to < from) return;
        }
    } else {
        while (t(from) < fromTime && !is_equal_approx(t(from), fromTime)) {
            if (to < ++from) return;
        }
        while (t(to) > toTime || is_equal_approx(t(to), toTime)) {
            if (--to < from) return;
        }
    }
    if (!backward) {
        for (int i = from; i <= to; ++i) out.push_back(i);
    } else {
        for (int i = to; i >= from; --i) out.push_back(i);
    }
}

}  // namespace

bool value_track_interpolate(const assets::Animation& anim, int track, double time, Value& out) {
    const Track* t = TrackAt(anim, track);
    if (!t || t->type != assets::TrackType::Value || t->keys.empty()) return false;
    // godot: animation.cpp:2708 -- DISCRETE samples nearest.
    const Interp interp = t->updateMode == assets::UpdateMode::Discrete ? Interp::Nearest : t->interp;
    if (!Interpolate(t->keys, time, interp, t->loopWrap, anim, out)) return false;
    return out.kind != ValueKind::None;
}

bool position_track_interpolate(const assets::Animation& anim, int track, double time, Vector3& out) {
    Value v;
    if (!TransformTrack(anim, track, time, assets::TrackType::Position3D, v)) return false;
    out = {v.f[0], v.f[1], v.f[2]};
    return true;
}

bool rotation_track_interpolate(const assets::Animation& anim, int track, double time,
                                Quaternion& out) {
    Value v;
    if (!TransformTrack(anim, track, time, assets::TrackType::Rotation3D, v)) return false;
    out = {v.f[0], v.f[1], v.f[2], v.f[3]};
    return true;
}

bool scale_track_interpolate(const assets::Animation& anim, int track, double time, Vector3& out) {
    Value v;
    if (!TransformTrack(anim, track, time, assets::TrackType::Scale3D, v)) return false;
    out = {v.f[0], v.f[1], v.f[2]};
    return true;
}

// godot: animation.cpp:1454-1630
int track_find_key(const assets::Animation& anim, int track, double time, FindMode mode) {
    const Track* t = TrackAt(anim, track);
    if (!t) return -1;
    const int k = FindKey(t->keys, time);
    if (k < 0 || k >= static_cast<int>(t->keys.size())) return -1;
    const double keyTime = t->keys[static_cast<std::size_t>(k)].time;
    if (mode == FindMode::Approx && !is_equal_approx(keyTime, time)) return -1;
    if (mode == FindMode::Exact && keyTime != time) return -1;
    return k;
}

// godot: animation.cpp:2791-2966, 3160-3181
void track_get_key_indices_in_range(const assets::Animation& anim, int track, double time,
                                    double delta, LoopedFlag looped, std::vector<int>& out) {
    const Track* t = TrackAt(anim, track);
    if (!t || delta == 0.0) return;
    double fromTime = time - delta;
    double toTime = time;
    bool backward = false;
    if (fromTime > toTime) {
        backward = true;
        std::swap(fromTime, toTime);
    }
    const double length = anim.length;
    if (anim.loopMode == kLoopLinear) {
        if (fromTime > length || fromTime < 0.0) fromTime = fposmod(fromTime, length);
        if (toTime > length || toTime < 0.0) toTime = fposmod(toTime, length);
        if (fromTime > toTime) {
            const double animEnd = length + kCmpEpsilon;
            const double animStart = -kCmpEpsilon;
            if (!backward) {
                KeysInRange(t->keys, fromTime, animEnd, backward, out);
                KeysInRange(t->keys, animStart, toTime, backward, out);
            } else {
                KeysInRange(t->keys, animStart, toTime, backward, out);
                KeysInRange(t->keys, fromTime, animEnd, backward, out);
            }
            return;
        }
        if (looped != LoopedFlag::None) {
            if (!backward && is_equal_approx(fromTime, 0.0)) {
                const int edge = track_find_key(anim, track, 0.0, FindMode::Exact);
                if (edge >= 0) out.push_back(edge);
            } else if (backward && is_equal_approx(toTime, length)) {
                const int edge = track_find_key(anim, track, length, FindMode::Exact);
                if (edge >= 0) out.push_back(edge);
            }
        }
    } else {
        // LOOP_NONE. PORT: ping-pong takes this branch; no animation uses it.
        fromTime = std::clamp(fromTime, 0.0, length);
        toTime = std::clamp(toTime, 0.0, length);
    }
    KeysInRange(t->keys, fromTime, toTime, backward, out);
}

}  // namespace godot
