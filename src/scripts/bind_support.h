// What the generated bindings (src/scripts/bindings.cpp) are written against:
// a reader over one node's "exports" object from the runtime scene export, a
// binder that turns node paths into typed pointers, and the driver that
// attaches every translated script to its node.
//
// Export value shapes (tools/godot/export_runtime_scene.gd _value()):
//   int / float / bool / string        plain JSON
//   Vector3                            {"v3":[x,y,z]}
//   Node                               {"node":"path/from/scene/root"}
//   Resource file (audio, scene)       {"res":"res://..."}
//   BaseMaterial3D                     {"res":..., "material":{"albedo":[..],"transparency":n}}
//   scripted Resource                  {"resource":"RoundClass","fields":{...}}
//   null                               null  (kept in arrays so indices hold)
#pragma once

#include "godot/node.h"
#include "godot/script.h"

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace godot { class PackedScene; }

namespace scripts {

struct ResourceBase {
    virtual ~ResourceBase() = default;
};

// Every Resource instance the bindings create, for the life of the process.
// PORT: Godot shares one Resource between every property referencing it; the
// export inlines each reference, so each reference becomes its own instance.
std::vector<std::unique_ptr<ResourceBase>>& ResourcePool();

class ExportReader {
public:
    ExportReader() = default;
    // `object` spans a JSON object including its braces; "" or "null" is empty.
    explicit ExportReader(std::string_view object);

    bool empty() const { return obj_.empty(); }

    int         integer(const char* key, int fallback) const;
    double      number(const char* key, double fallback) const;
    bool        boolean(const char* key, bool fallback) const;
    std::string string(const char* key) const;
    godot::Vector3 vector3(const char* key, godot::Vector3 fallback) const;
    godot::Vector2 vector2(const char* key, godot::Vector2 fallback) const;

    std::string res_path(const char* key) const;   // "res://..." or ""
    std::string res_name(const char* key) const;   // the file name: "shell latch1.ogg"
    godot::SurfaceMaterial material(const char* key) const;
    std::string node_path(const char* key) const;  // "" for null or absent
    ExportReader resource_fields(const char* key) const;

    std::vector<std::string> strings(const char* key) const;
    std::vector<std::string> res_names(const char* key) const;
    std::vector<std::string> res_paths(const char* key) const;   // "res://..." each, "" for null
    std::vector<godot::Vector3> vector3s(const char* key) const;
    std::vector<bool> booleans(const char* key) const;
    std::vector<std::string> node_paths(const char* key) const;
    std::vector<ExportReader> resource_array(const char* key) const;

private:
    std::string_view obj_;
};

class Binder {
public:
    // Paths resolve from `root`: the scene root for main, the instance root
    // for an instanced scene. Failures are appended to `errors` as
    // "<scriptClass>.<field>: <path> ...".
    Binder(godot::Node* root, std::string scriptClass, std::vector<std::string>* errors)
        : root_(root), class_(std::move(scriptClass)), errors_(errors) {}

    godot::Node* resolve(const std::string& path) const;

    template <class T>
    void node(const ExportReader& ex, const char* key, T*& out) {
        const std::string path = ex.node_path(key);
        out = path.empty() ? nullptr : As<T>(resolve(path));
        if (!path.empty() && !out) fail(key, path);
    }

    template <class T>
    void nodes(const ExportReader& ex, const char* key, std::vector<T*>& out) {
        out.clear();
        for (const std::string& path : ex.node_paths(key)) {
            T* p = path.empty() ? nullptr : As<T>(resolve(path));
            if (!path.empty() && !p) fail(key, path);
            out.push_back(p);
        }
    }

    // `@export var x : PackedScene`: the scene registered at the export's
    // res:// path (godot::PackedScene::load must have run first).
    void scene(const ExportReader& ex, const char* key, godot::PackedScene*& out);

    template <class T>
    T* resource(const ExportReader& ex, const char* key, void (*read)(const ExportReader&, T&)) {
        const ExportReader fields = ex.resource_fields(key);
        return fields.empty() ? nullptr : Make(fields, read);
    }

    template <class T>
    void resources(const ExportReader& ex, const char* key, std::vector<T*>& out,
                   void (*read)(const ExportReader&, T&)) {
        out.clear();
        for (const ExportReader& fields : ex.resource_array(key)) {
            out.push_back(fields.empty() ? nullptr : Make(fields, read));
        }
    }

private:
    template <class T>
    static T* As(godot::Node* n) {
        if constexpr (std::is_base_of_v<godot::Script, T>) return godot::script_cast<T>(n);
        else return godot::cast_to<T>(n);
    }

    template <class T>
    static T* Make(const ExportReader& fields, void (*read)(const ExportReader&, T&)) {
        auto r = std::make_unique<T>();
        read(fields, *r);
        T* raw = r.get();
        ResourcePool().push_back(std::move(r));
        return raw;
    }

    void fail(const char* key, const std::string& path);

    godot::Node* root_;
    std::string class_;
    std::vector<std::string>* errors_;
};

struct BindResult {
    int created = 0;   // scripts attached
    int bound = 0;     // scripts whose fields were filled
    std::vector<std::string> errors;
};

// Generated (bindings.cpp): registers every translated script by .gd file name.
void RegisterScripts();
// Generated: fills `script`'s exported fields. False for a class it does not know.
bool BindFieldsFor(godot::Script& script, const ExportReader& ex, Binder& binder);

// Attaches a script to every node under `root` (inclusive) whose export names
// a registered .gd file, then binds all their fields (a second pass, so a
// field may point at a script created later in tree order).
BindResult BindScripts(godot::Node* root);

// PackedScene's script binder (installed by RegisterScripts): BindScripts on a
// freshly instantiated subtree. Its errors accumulate in InstanceBindErrors().
void BindInstanceScripts(godot::Node* instanceRoot);
std::vector<std::string>& InstanceBindErrors();

}  // namespace scripts
