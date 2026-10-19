#include "parity/parity_dump.h"

#include "core/json_lite.h"

#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace parity {
namespace {

using namespace core::json;

std::string ReadAll(const std::string& path, bool& ok) {
    std::string text;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    ok = f != nullptr;
    if (!f) return text;
    char buf[65536];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    return text;
}

std::string Fmt(const char* f, double a, double b) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), f, a, b);
    return buf;
}

void AppendJsonString(std::string& out, const std::string& v) {
    out += '"';
    for (const char c : v) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out += c;
        }
    }
    out += '"';
}

}  // namespace

bool ReadDump(const std::string& path, Dump& out) {
    bool ok = false;
    const std::string text = ReadAll(path, ok);
    if (!ok) return false;
    const char* b = text.data();
    const char* e = b + text.size();
    out.scenario = ReadStringTopLevel(b, e, "scenario");

    const char* ab = nullptr;
    const char* ae = nullptr;
    if (FindArrayTopLevel(b, e, "rng", &ab, &ae)) ReadNumberArray(ab, ae, out.rng);
    if (FindArrayTopLevel(b, e, "events", &ab, &ae)) ReadStringArray(ab, ae, out.events);
    if (FindArrayTopLevel(b, e, "samples", &ab, &ae)) {
        std::vector<Span> objs;
        SplitObjects(ab, ae, objs);
        out.samples.reserve(objs.size());
        for (const Span& o : objs) {
            Sample s;
            s.id = ReadStringTopLevel(o.first, o.second, "id");
            // Node paths and on-screen text: escapes decoded, as everywhere else.
            s.s = ReadStringTopLevelDecoded(o.first, o.second, "s");
            const char* vb = nullptr;
            const char* ve = nullptr;
            if (FindArrayTopLevel(o.first, o.second, "v", &vb, &ve)) ReadNumberArray(vb, ve, s.v);
            out.samples.push_back(std::move(s));
        }
    }
    if (FindArrayTopLevel(b, e, "nodes", &ab, &ae)) {
        std::vector<Span> objs;
        SplitObjects(ab, ae, objs);
        for (const Span& o : objs) {
            NodeState s;
            s.path = ReadStringTopLevel(o.first, o.second, "path");
            const char* gb = nullptr;
            const char* ge = nullptr;
            if (FindArrayTopLevel(o.first, o.second, "g", &gb, &ge)) {
                std::vector<double> g;
                ReadNumberArray(gb, ge, g);
                for (std::size_t i = 0; i < 12 && i < g.size(); ++i) s.g[i] = g[i];
            }
            s.v = ReadBoolTopLevel(o.first, o.second, "v", true);
            s.vt = ReadBoolTopLevel(o.first, o.second, "vt", true);
            s.hasLayers = FindKeyTopLevel(o.first, o.second, "l") != nullptr;
            s.l = static_cast<uint32_t>(ReadNumberTopLevel(o.first, o.second, "l", 0.0));
            s.hasTransparency = FindKeyTopLevel(o.first, o.second, "t") != nullptr;
            s.t = ReadNumberTopLevel(o.first, o.second, "t", 0.0);
            const char* cb = nullptr;
            const char* ce = nullptr;
            if (FindArrayTopLevel(o.first, o.second, "a", &cb, &ce)) {
                std::vector<double> c;
                ReadNumberArray(cb, ce, c);
                s.hasAlbedo = c.size() == 4;
                for (std::size_t i = 0; i < 4 && i < c.size(); ++i) s.a[i] = c[i];
            }
            out.nodes.push_back(std::move(s));
        }
    }
    return true;
}

std::vector<std::string> Compare(const Dump& ref, const Dump& act, std::size_t limit) {
    std::vector<std::string> diffs;
    const auto add = [&](std::string s) {
        if (diffs.size() < limit) diffs.push_back(std::move(s));
    };

    if (ref.rng.size() != act.rng.size()) {
        add("rng: godot drew " + std::to_string(ref.rng.size()) + ", port " +
            std::to_string(act.rng.size()));
    }
    for (std::size_t i = 0; i < ref.rng.size() && i < act.rng.size(); ++i) {
        if (std::fabs(ref.rng[i] - act.rng[i]) > 1e-9) {
            add("rng[" + std::to_string(i) + "]: " +
                Fmt("godot %.17g, port %.17g", ref.rng[i], act.rng[i]));
        }
    }

    if (ref.events.size() != act.events.size()) {
        add("events: godot has " + std::to_string(ref.events.size()) + ", port has " +
            std::to_string(act.events.size()));
    }
    for (std::size_t i = 0; i < ref.events.size() || i < act.events.size(); ++i) {
        const std::string r = i < ref.events.size() ? ref.events[i] : "<none>";
        const std::string a = i < act.events.size() ? act.events[i] : "<none>";
        if (r != a) add("events[" + std::to_string(i) + "]: godot '" + r + "', port '" + a + "'");
    }

    std::unordered_map<std::string, const Sample*> sampleById;
    for (const Sample& s : act.samples) sampleById[s.id] = &s;
    for (const Sample& r : ref.samples) {
        const auto it = sampleById.find(r.id);
        if (it == sampleById.end()) {
            add(r.id + ": missing in port");
            continue;
        }
        const Sample& a = *it->second;
        // The name half first: it says WHAT the scripts decided, and a wrong
        // one usually explains the numbers that follow it.
        if (r.s != a.s) {
            add(r.id + ": godot '" + r.s + "', port '" + a.s + "'");
            continue;
        }
        if (a.v.size() != r.v.size()) {
            add(r.id + ": godot has " + std::to_string(r.v.size()) + " components, port " +
                std::to_string(a.v.size()));
            continue;
        }
        for (std::size_t i = 0; i < r.v.size(); ++i) {
            if (std::fabs(r.v[i] - a.v[i]) > 1e-4) {
                add(r.id + "[" + std::to_string(i) + "]: " +
                    Fmt("godot %.6f, port %.6f", r.v[i], a.v[i]));
                break;
            }
        }
    }
    if (ref.samples.size() != act.samples.size()) {
        add("samples: godot " + std::to_string(ref.samples.size()) + ", port " +
            std::to_string(act.samples.size()));
    }

    std::unordered_map<std::string, const NodeState*> byPath;
    for (const NodeState& n : act.nodes) byPath[n.path] = &n;
    for (const NodeState& r : ref.nodes) {
        const auto it = byPath.find(r.path);
        if (it == byPath.end()) {
            add(r.path + ": missing in port");
            continue;
        }
        const NodeState& a = *it->second;
        for (int i = 0; i < 12; ++i) {
            if (std::fabs(r.g[i] - a.g[i]) > 1e-4) {
                add(r.path + ": global[" + std::to_string(i) + "] " +
                    Fmt("godot %.6f, port %.6f", r.g[i], a.g[i]));
                break;
            }
        }
        if (r.v != a.v) add(r.path + ": visible differs");
        if (r.vt != a.vt) add(r.path + ": visible_in_tree differs");
        if (r.hasLayers && (!a.hasLayers || r.l != a.l)) add(r.path + ": layers differ");
        if (r.hasTransparency && (!a.hasTransparency || std::fabs(r.t - a.t) > 1e-4)) {
            add(r.path + ": transparency differs");
        }
        if (r.hasAlbedo) {
            bool same = a.hasAlbedo;
            for (int i = 0; same && i < 4; ++i) same = std::fabs(r.a[i] - a.a[i]) <= 1e-4;
            if (!same) add(r.path + ": albedo differs" + (a.hasAlbedo ? "" : " (missing in port)"));
        }
    }
    if (ref.nodes.size() != act.nodes.size()) {
        add("nodes: godot captured " + std::to_string(ref.nodes.size()) + ", port " +
            std::to_string(act.nodes.size()));
    }
    return diffs;
}

}  // namespace parity

namespace parity {

bool WriteDump(const std::string& path, const Dump& d) {
    std::string out = "{\"scenario\":";
    AppendJsonString(out, d.scenario);
    out += ",\"events\":[";
    for (std::size_t i = 0; i < d.events.size(); ++i) {
        if (i) out += ',';
        AppendJsonString(out, d.events[i]);
    }
    out += "],\"samples\":[";
    char num[32];
    for (std::size_t i = 0; i < d.samples.size(); ++i) {
        const Sample& sm = d.samples[i];
        if (i) out += ',';
        out += "{\"id\":";
        AppendJsonString(out, sm.id);
        out += ",\"s\":";
        AppendJsonString(out, sm.s);
        out += ",\"v\":[";
        for (std::size_t k = 0; k < sm.v.size(); ++k) {
            if (k) out += ',';
            std::snprintf(num, sizeof num, "%.9g", sm.v[k]);
            out += num;
        }
        out += "]}";
    }
    out += "]}\n";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    return std::fclose(f) == 0 && ok;
}

}  // namespace parity
