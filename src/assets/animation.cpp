#include "assets/animation.h"

#include "core/json_lite.h"

#include <cmath>
#include <cstdio>

namespace assets {

namespace {

using core::json::FindArray;
using core::json::ReadNumber;
using core::json::ReadNumberArray;
using core::json::ReadString;
using core::json::Span;
using core::json::SplitObjects;

ValueKind KindFromTag(const std::string& tag) {
    if (tag == "f")   return ValueKind::Float;
    if (tag == "b")   return ValueKind::Bool;
    if (tag == "i")   return ValueKind::Int;
    if (tag == "v2")  return ValueKind::Vec2;
    if (tag == "v3")  return ValueKind::Vec3;
    if (tag == "q")   return ValueKind::Quat;
    if (tag == "c")   return ValueKind::Color;
    if (tag == "s")   return ValueKind::Str;
    if (tag == "nil") return ValueKind::None;
    return ValueKind::Unknown;
}

int ComponentsFor(ValueKind k) {
    switch (k) {
    case ValueKind::Vec2:  return 2;
    case ValueKind::Vec3:  return 3;
    case ValueKind::Quat:
    case ValueKind::Color: return 4;
    default:               return 1;
    }
}

// Reads the "k"/"v" pair the exporter writes. `v` may be a scalar, a bool, a
// string or an array depending on the kind, which is exactly why the kind is
// tagged rather than inferred.
Value ReadValue(const char* begin, const char* end) {
    Value out;
    out.kind = KindFromTag(ReadString(begin, end, "k"));

    switch (out.kind) {
    case ValueKind::Bool: {
        const bool b = core::json::ReadBool(begin, end, "v", false);
        out.f[0] = b ? 1.0f : 0.0f;
        break;
    }
    case ValueKind::Int:
    case ValueKind::Float:
        out.f[0] = static_cast<float>(ReadNumber(begin, end, "v", 0.0));
        break;
    case ValueKind::Str:
    case ValueKind::Unknown:
        out.s = ReadString(begin, end, "v");
        break;
    case ValueKind::Vec2:
    case ValueKind::Vec3:
    case ValueKind::Quat:
    case ValueKind::Color: {
        const char* ab = nullptr;
        const char* ae = nullptr;
        if (FindArray(begin, end, "v", &ab, &ae)) {
            std::vector<double> v;
            ReadNumberArray(ab, ae, v);
            const int n = ComponentsFor(out.kind);
            for (int i = 0; i < n && i < static_cast<int>(v.size()); ++i) {
                out.f[i] = static_cast<float>(v[static_cast<std::size_t>(i)]);
            }
        }
        break;
    }
    case ValueKind::None:
        break;
    }
    return out;
}

}  // namespace

bool AnimationLibrary::LoadFromString(const char* json) {
    players_.clear();
    stats_ = AnimationStats{};
    if (!json) return false;

    const char* begin = json;
    const char* end = json + std::char_traits<char>::length(json);

    // A version-1 export has no update modes, loop wrap or key transitions and
    // would play as plausible, wrong motion. Refuse it.
    if (core::json::ReadIntTopLevel(begin, end, "format", 0) != kAnimationFormat) return false;

    const char* pb = nullptr;
    const char* pe = nullptr;
    if (!FindArray(begin, end, "players", &pb, &pe)) return false;

    std::vector<Span> playerObjs;
    SplitObjects(pb, pe, playerObjs);
    if (playerObjs.empty()) return false;

    for (const Span& po : playerObjs) {
        AnimationPlayerData player;
        // TOP LEVEL: a player's span contains its animations, whose tracks
        // also have "path" keys. Godot sorts keys alphabetically, so
        // "animations" precedes "path" and a plain scan finds a track's path.
        player.path = core::json::ReadStringTopLevel(po.first, po.second, "path");
        player.root = core::json::ReadStringTopLevel(po.first, po.second, "root");
        player.autoplay =
            core::json::ReadStringTopLevel(po.first, po.second, "autoplay");

        const char* ab = nullptr;
        const char* ae = nullptr;
        if (FindArray(po.first, po.second, "animations", &ab, &ae)) {
            std::vector<Span> animObjs;
            SplitObjects(ab, ae, animObjs);

            for (const Span& ao : animObjs) {
                // Same hazard: an animation's span contains its tracks.
                Animation anim;
                anim.name = core::json::ReadStringTopLevel(ao.first, ao.second, "name");
                anim.length = static_cast<float>(
                    core::json::ReadNumberTopLevel(ao.first, ao.second, "length", 0.0));
                anim.step = static_cast<float>(
                    core::json::ReadNumberTopLevel(ao.first, ao.second, "step", 0.0));
                anim.loopMode =
                    core::json::ReadIntTopLevel(ao.first, ao.second, "loop", 0);

                const char* tb = nullptr;
                const char* te = nullptr;
                if (FindArray(ao.first, ao.second, "tracks", &tb, &te)) {
                    std::vector<Span> trackObjs;
                    SplitObjects(tb, te, trackObjs);

                    for (const Span& to : trackObjs) {
                        Track track;
                        const int type =
                            core::json::ReadIntTopLevel(to.first, to.second, "type", 255);
                        switch (type) {
                        case 0: track.type = TrackType::Value;      break;
                        case 1: track.type = TrackType::Position3D; break;
                        case 2: track.type = TrackType::Rotation3D; break;
                        case 3: track.type = TrackType::Scale3D;    break;
                        case 5: track.type = TrackType::Method;     break;
                        case 6: track.type = TrackType::Bezier;     break;
                        case 7: track.type = TrackType::Audio;      break;
                        default:
                            track.type = TrackType::Unsupported;
                            ++stats_.unsupportedTracks;
                            break;
                        }

                        const int in =
                            core::json::ReadIntTopLevel(to.first, to.second, "interp", 1);
                        track.interp = (in == 0)   ? Interp::Nearest
                                       : (in == 2) ? Interp::Cubic
                                                   : Interp::Linear;
                        const int update =
                            core::json::ReadIntTopLevel(to.first, to.second, "update", 0);
                        track.updateMode = (update == 1)   ? UpdateMode::Discrete
                                           : (update == 2) ? UpdateMode::Capture
                                                           : UpdateMode::Continuous;
                        track.loopWrap =
                            core::json::ReadBoolTopLevel(to.first, to.second, "loop_wrap", true);

                        // A track's span contains its keys, which carry "t",
                        // "m", "k", "v" and no "path"; staying top-level is
                        // correct regardless.
                        track.path =
                            core::json::ReadStringTopLevel(to.first, to.second, "path");
                        const std::size_t colon = track.path.find(':');
                        if (colon == std::string::npos) {
                            track.nodePath = track.path;
                        } else {
                            track.nodePath = track.path.substr(0, colon);
                            track.property = track.path.substr(colon + 1);
                        }

                        const char* kb = nullptr;
                        const char* ke = nullptr;
                        if (FindArray(to.first, to.second, "keys", &kb, &ke)) {
                            std::vector<Span> keyObjs;
                            SplitObjects(kb, ke, keyObjs);

                            for (const Span& ko : keyObjs) {
                                Key key;
                                key.time = static_cast<float>(
                                    ReadNumber(ko.first, ko.second, "t", 0.0));
                                key.transition = static_cast<float>(
                                    ReadNumber(ko.first, ko.second, "tr", 1.0));

                                if (track.type == TrackType::Method) {
                                    key.method = ReadString(ko.first, ko.second, "m");
                                    const char* gb = nullptr;
                                    const char* ge = nullptr;
                                    if (FindArray(ko.first, ko.second, "a", &gb, &ge)) {
                                        std::vector<Span> argObjs;
                                        SplitObjects(gb, ge, argObjs);
                                        for (const Span& g : argObjs) {
                                            key.args.push_back(ReadValue(g.first, g.second));
                                        }
                                    }
                                    ++stats_.methodKeys;
                                } else if (track.type == TrackType::Audio) {
                                    key.stream = ReadString(ko.first, ko.second, "stream");
                                    key.startOffset = static_cast<float>(ReadNumber(ko.first, ko.second, "start", 0.0));
                                    key.endOffset = static_cast<float>(ReadNumber(ko.first, ko.second, "end", 0.0));
                                    ++stats_.audioKeys;
                                } else if (track.type == TrackType::Bezier) {
                                    key.value.kind = ValueKind::Float;
                                    key.value.f[0] = static_cast<float>(
                                        ReadNumber(ko.first, ko.second, "v", 0.0));
                                } else {
                                    key.value = ReadValue(ko.first, ko.second);
                                }

                                ++stats_.keys;
                                track.keys.push_back(std::move(key));
                            }
                        }

                        ++stats_.tracks;
                        if (!track.nodePath.empty()) ++stats_.resolvable;
                        anim.tracks.push_back(std::move(track));
                    }
                }

                ++stats_.animations;
                player.animations.push_back(std::move(anim));
            }
        }

        ++stats_.players;
        players_.push_back(std::move(player));
    }

    return true;
}

const AnimationPlayerData* AnimationLibrary::FindPlayer(const char* playerPath) const {
    if (!playerPath) return nullptr;
    for (const AnimationPlayerData& p : players_) {
        if (p.path == playerPath) return &p;
    }
    return nullptr;
}

const Animation* AnimationLibrary::Find(const char* playerPath,
                                        const char* animName) const {
    const AnimationPlayerData* p = FindPlayer(playerPath);
    if (!p || !animName) return nullptr;
    for (const Animation& a : p->animations) {
        if (a.name == animName) return &a;
    }
    return nullptr;
}

bool AnimationLibrary::LoadFromFile(const char* path) {
    players_.clear();
    stats_ = AnimationStats{};
    if (!path) return false;

    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;

    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); return false; }

    std::string buf;
    buf.resize(static_cast<std::size_t>(n));
    const std::size_t got = std::fread(&buf[0], 1, static_cast<std::size_t>(n), f);
    std::fclose(f);
    buf.resize(got);

    return LoadFromString(buf.c_str());
}

}  // namespace assets
