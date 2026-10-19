#include "scripts/bind_support.h"

#include "core/json_lite.h"
#include "godot/packed_scene.h"
#include "godot/runtime_scene_loader.h"

#include <cstdlib>

namespace scripts {
namespace {

using core::json::IsWs;

// Past the value starting at `p` (object, array, string or literal).
const char* SkipValue(const char* p, const char* e) {
    if (p >= e) return e;
    if (*p == '"') {
        for (++p; p < e && *p != '"'; ++p) {
            if (*p == '\\') ++p;
        }
        return p < e ? p + 1 : e;
    }
    if (*p == '{' || *p == '[') {
        int depth = 0;
        for (; p < e; ++p) {
            if (*p == '"') {
                p = SkipValue(p, e) - 1;
                continue;
            }
            if (*p == '{' || *p == '[') ++depth;
            if (*p == '}' || *p == ']') {
                if (--depth == 0) return p + 1;
            }
        }
        return e;
    }
    while (p < e && *p != ',' && *p != '}' && *p != ']' && !IsWs(*p)) ++p;
    return p;
}

// The value of a TOP-LEVEL `key` in the object [b, e): [vb, ve), whitespace trimmed.
bool TopValue(std::string_view obj, const char* key, const char** vb, const char** ve) {
    if (obj.empty()) return false;
    const char* b = obj.data();
    const char* e = b + obj.size();
    const char* v = core::json::FindKeyTopLevel(b, e, key);
    if (!v) return false;
    while (v < e && IsWs(*v)) ++v;
    *vb = v;
    *ve = SkipValue(v, e);
    return v < e;
}

// Calls fn(vb, ve) for each element of the array value at [ab, ae).
template <class Fn>
void ForEachElement(const char* ab, const char* ae, Fn fn) {
    if (ab >= ae || *ab != '[') return;
    const char* p = ab + 1;
    const char* end = ae - 1;   // the closing bracket
    while (p < end) {
        while (p < end && (IsWs(*p) || *p == ',')) ++p;
        if (p >= end) break;
        const char* ve = SkipValue(p, end);
        fn(p, ve);
        p = ve;
    }
}

bool IsNull(const char* vb, const char* ve) {
    return ve - vb == 4 && std::string_view(vb, 4) == "null";
}

std::string DecodeAt(const char* vb, const char* ve) {
    std::string out;
    if (vb < ve && *vb == '"') core::json::DecodeString(vb, ve, out);
    return out;
}

std::string FileName(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool ReadV3(std::string_view obj, godot::Vector3& out) {
    const char* vb = nullptr;
    const char* ve = nullptr;
    if (!TopValue(obj, "v3", &vb, &ve)) return false;
    std::vector<double> n;
    core::json::ReadNumberArray(vb + 1, ve - 1, n);
    if (n.size() != 3) return false;
    out = {static_cast<float>(n[0]), static_cast<float>(n[1]), static_cast<float>(n[2])};
    return true;
}

}  // namespace

std::vector<std::unique_ptr<ResourceBase>>& ResourcePool() {
    static std::vector<std::unique_ptr<ResourceBase>> pool;
    return pool;
}

ExportReader::ExportReader(std::string_view object) {
    std::size_t i = 0;
    while (i < object.size() && IsWs(object[i])) ++i;
    if (i < object.size() && object[i] == '{') obj_ = object.substr(i);
}

int ExportReader::integer(const char* key, int fallback) const {
    const char* vb = nullptr;
    const char* ve = nullptr;
    if (!TopValue(obj_, key, &vb, &ve) || IsNull(vb, ve)) return fallback;
    return static_cast<int>(std::strtol(std::string(vb, ve).c_str(), nullptr, 10));
}

double ExportReader::number(const char* key, double fallback) const {
    const char* vb = nullptr;
    const char* ve = nullptr;
    if (!TopValue(obj_, key, &vb, &ve) || IsNull(vb, ve)) return fallback;
    return std::strtod(std::string(vb, ve).c_str(), nullptr);
}

bool ExportReader::boolean(const char* key, bool fallback) const {
    const char* vb = nullptr;
    const char* ve = nullptr;
    if (!TopValue(obj_, key, &vb, &ve)) return fallback;
    const std::string_view v(vb, static_cast<std::size_t>(ve - vb));
    if (v == "true") return true;
    if (v == "false") return false;
    return fallback;
}

std::string ExportReader::string(const char* key) const {
    const char* vb = nullptr;
    const char* ve = nullptr;
    return TopValue(obj_, key, &vb, &ve) ? DecodeAt(vb, ve) : std::string();
}

godot::Vector3 ExportReader::vector3(const char* key, godot::Vector3 fallback) const {
    const char* vb = nullptr;
    const char* ve = nullptr;
    if (!TopValue(obj_, key, &vb, &ve) || *vb != '{') return fallback;
    godot::Vector3 v = fallback;
    ReadV3(std::string_view(vb, static_cast<std::size_t>(ve - vb)), v);
    return v;
}

godot::Vector2 ExportReader::vector2(const char* key, godot::Vector2 fallback) const {
    const char* vb = nullptr;
    const char* ve = nullptr;
    if (!TopValue(obj_, key, &vb, &ve) || *vb != '{') return fallback;
    const std::string_view obj(vb, static_cast<std::size_t>(ve - vb));
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (!TopValue(obj, "v2", &ab, &ae)) return fallback;
    std::vector<double> n;
    core::json::ReadNumberArray(ab + 1, ae - 1, n);
    if (n.size() != 2) return fallback;
    return {static_cast<float>(n[0]), static_cast<float>(n[1])};
}

std::string ExportReader::res_path(const char* key) const {
    const char* vb = nullptr;
    const char* ve = nullptr;
    if (!TopValue(obj_, key, &vb, &ve) || *vb != '{') return std::string();
    return ExportReader(std::string_view(vb, static_cast<std::size_t>(ve - vb))).string("res");
}

std::string ExportReader::res_name(const char* key) const {
    return FileName(res_path(key));
}

godot::SurfaceMaterial ExportReader::material(const char* key) const {
    godot::SurfaceMaterial m;
    const char* vb = nullptr;
    const char* ve = nullptr;
    if (!TopValue(obj_, key, &vb, &ve) || *vb != '{') return m;
    const std::string_view outer(vb, static_cast<std::size_t>(ve - vb));
    const char* mb = nullptr;
    const char* me = nullptr;
    if (!TopValue(outer, "material", &mb, &me) || *mb != '{') return m;
    return godot::ReadMaterialJson(std::string_view(mb, static_cast<std::size_t>(me - mb)));
}

std::string ExportReader::node_path(const char* key) const {
    const char* vb = nullptr;
    const char* ve = nullptr;
    if (!TopValue(obj_, key, &vb, &ve) || *vb != '{') return std::string();
    return ExportReader(std::string_view(vb, static_cast<std::size_t>(ve - vb))).string("node");
}

ExportReader ExportReader::resource_fields(const char* key) const {
    const char* vb = nullptr;
    const char* ve = nullptr;
    if (!TopValue(obj_, key, &vb, &ve) || *vb != '{') return ExportReader();
    const std::string_view res(vb, static_cast<std::size_t>(ve - vb));
    const char* fb = nullptr;
    const char* fe = nullptr;
    if (!TopValue(res, "fields", &fb, &fe) || *fb != '{') return ExportReader();
    return ExportReader(std::string_view(fb, static_cast<std::size_t>(fe - fb)));
}

std::vector<std::string> ExportReader::strings(const char* key) const {
    std::vector<std::string> out;
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (!TopValue(obj_, key, &ab, &ae)) return out;
    ForEachElement(ab, ae, [&](const char* vb, const char* ve) { out.push_back(DecodeAt(vb, ve)); });
    return out;
}

std::vector<std::string> ExportReader::res_names(const char* key) const {
    std::vector<std::string> out;
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (!TopValue(obj_, key, &ab, &ae)) return out;
    ForEachElement(ab, ae, [&](const char* vb, const char* ve) {
        out.push_back(*vb == '{'
                          ? FileName(ExportReader(std::string_view(vb, static_cast<std::size_t>(ve - vb))).string("res"))
                          : std::string());
    });
    return out;
}

std::vector<std::string> ExportReader::res_paths(const char* key) const {
    std::vector<std::string> out;
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (!TopValue(obj_, key, &ab, &ae)) return out;
    ForEachElement(ab, ae, [&](const char* vb, const char* ve) {
        out.push_back(*vb == '{' ? ExportReader(std::string_view(vb, static_cast<std::size_t>(ve - vb))).string("res")
                                 : std::string());
    });
    return out;
}

std::vector<godot::Vector3> ExportReader::vector3s(const char* key) const {
    std::vector<godot::Vector3> out;
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (!TopValue(obj_, key, &ab, &ae)) return out;
    ForEachElement(ab, ae, [&](const char* vb, const char* ve) {
        godot::Vector3 v{};
        ReadV3(std::string_view(vb, static_cast<std::size_t>(ve - vb)), v);
        out.push_back(v);
    });
    return out;
}

std::vector<bool> ExportReader::booleans(const char* key) const {
    std::vector<bool> out;
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (!TopValue(obj_, key, &ab, &ae)) return out;
    ForEachElement(ab, ae, [&](const char* vb, const char* ve) {
        out.push_back(std::string_view(vb, static_cast<std::size_t>(ve - vb)) == "true");
    });
    return out;
}

std::vector<std::string> ExportReader::node_paths(const char* key) const {
    std::vector<std::string> out;
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (!TopValue(obj_, key, &ab, &ae)) return out;
    ForEachElement(ab, ae, [&](const char* vb, const char* ve) {
        out.push_back(*vb == '{'
                          ? ExportReader(std::string_view(vb, static_cast<std::size_t>(ve - vb))).string("node")
                          : std::string());
    });
    return out;
}

std::vector<ExportReader> ExportReader::resource_array(const char* key) const {
    std::vector<ExportReader> out;
    const char* ab = nullptr;
    const char* ae = nullptr;
    if (!TopValue(obj_, key, &ab, &ae)) return out;
    ForEachElement(ab, ae, [&](const char* vb, const char* ve) {
        if (*vb != '{') {
            out.emplace_back();
            return;
        }
        const std::string_view res(vb, static_cast<std::size_t>(ve - vb));
        const char* fb = nullptr;
        const char* fe = nullptr;
        out.push_back(TopValue(res, "fields", &fb, &fe) && *fb == '{'
                          ? ExportReader(std::string_view(fb, static_cast<std::size_t>(fe - fb)))
                          : ExportReader());
    });
    return out;
}

godot::Node* Binder::resolve(const std::string& path) const {
    if (!root_) return nullptr;
    if (path == "." || path.empty()) return root_;
    godot::Node* n = root_;
    std::size_t start = 0;
    while (n && start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::string_view seg(path.data() + start,
                                   (slash == std::string::npos ? path.size() : slash) - start);
        godot::Node* next = nullptr;
        if (seg == "..") {
            next = n->get_parent();
        } else if (seg != ".") {
            for (int i = 0; i < n->get_child_count(); ++i) {
                if (n->get_child(i)->get_name() == seg) {
                    next = n->get_child(i);
                    break;
                }
            }
        } else {
            next = n;
        }
        n = next;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return n;
}

void Binder::scene(const ExportReader& ex, const char* key, godot::PackedScene*& out) {
    const std::string path = ex.res_path(key);
    out = path.empty() ? nullptr : godot::PackedScene::find(path);
    if (!path.empty() && !out) fail(key, path);
}

void Binder::fail(const char* key, const std::string& path) {
    if (errors_) {
        errors_->push_back(class_ + "." + key + ": " + path +
                           " did not resolve to the field's type (or the scene is not loaded)");
    }
}

namespace {

void CreateScripts(godot::Node* n, BindResult& r) {
    if (!n->get_script() && !n->get_script_file().empty()) {
        if (std::unique_ptr<godot::Script> s = godot::create_script(n->get_script_file())) {
            n->set_script(std::move(s));
            ++r.created;
        }
    }
    for (int i = 0; i < n->get_child_count(); ++i) CreateScripts(n->get_child(i), r);
}

void BindFields(godot::Node* root, godot::Node* n, BindResult& r) {
    if (godot::Script* s = n->get_script()) {
        Binder b(root, s->get_class_name(), &r.errors);
        if (BindFieldsFor(*s, ExportReader(n->get_exports_json()), b)) ++r.bound;
    }
    for (int i = 0; i < n->get_child_count(); ++i) BindFields(root, n->get_child(i), r);
}

}  // namespace

BindResult BindScripts(godot::Node* root) {
    BindResult r;
    if (!root) return r;
    CreateScripts(root, r);
    BindFields(root, root, r);
    return r;
}

std::vector<std::string>& InstanceBindErrors() {
    static std::vector<std::string> errors;
    return errors;
}

void BindInstanceScripts(godot::Node* instanceRoot) {
    BindResult r = BindScripts(instanceRoot);
    for (std::string& e : r.errors) InstanceBindErrors().push_back(std::move(e));
}

}  // namespace scripts
