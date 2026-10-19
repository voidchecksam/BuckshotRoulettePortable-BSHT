#!/usr/bin/env python3
"""Generates the mechanical half of the GDScript translation.

    python tools/gen_script_bindings.py [File ...]

For every File named on the command line, writes src/scripts/<File>.h as a
skeleton IF IT DOES NOT EXIST: the header block the scripts_mirror_gdscript
guard wants, every @export field typed and in source order, and one
`// PORT: omitted <func> (not translated yet)` line per func. The translator
replaces those lines with declarations as each function lands.

Always regenerates:
  src/scripts/forward.h    forward declarations of every translated class
  src/scripts/resources.h  RoundClass, CameraSocket, TrackInstance, PlayerData
  src/scripts/bindings.cpp RegisterScripts() and one BindFields_<Class> per
                           translated class, filling fields from the runtime
                           scene export through scripts::ExportReader/Binder.

"Translated" means src/scripts/<File>.h exists. Bodies are never generated:
they are written by hand, statement by statement, from the .gd.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The game's scripts: the reference game (bsht-extract --unpack-to).
GD_DIR = os.path.join(os.environ.get("BSHT_REFERENCE_GAME",
                                     os.path.join(ROOT, "external", "game-v2.2", "game")), "scripts")
OUT = os.path.join(ROOT, "src", "scripts")

# Godot class -> godot:: C++ type. AudioStreamPlayer2D is loaded as
# AudioStreamPlayer (the port does not model Node2D).
NATIVE = {
    "Node": "Node", "Node3D": "Node3D", "VisualInstance3D": "VisualInstance3D",
    "GeometryInstance3D": "GeometryInstance3D", "MeshInstance3D": "MeshInstance3D",
    "Label3D": "Label3D", "Camera3D": "Camera3D", "AnimationPlayer": "AnimationPlayer",
    "AudioStreamPlayer": "AudioStreamPlayer", "AudioStreamPlayer2D": "AudioStreamPlayer",
    "AudioStreamPlayer3D": "AudioStreamPlayer3D", "OmniLight3D": "OmniLight3D",
    "Control": "Control", "Label": "Label", "ColorRect": "ColorRect",
    "TextureRect": "TextureRect", "Button": "Button",
    "StaticBody3D": "StaticBody3D", "CollisionShape3D": "CollisionShape3D",
    "GPUParticles3D": "GPUParticles3D",   # godot/particles.h
    "WorldEnvironment": "WorldEnvironment",   # godot/node.h: the adjustments only
    # PORT: every CanvasItem the scripts reach is a Control (godot/control.h).
    "CanvasItem": "Control",
}
RESOURCES = ["RoundClass", "CameraSocket", "TrackInstance", "PlayerData",
             "ItemResource", "AmountResource"]

# Plain `var`s a resource carries at runtime. They are not @exports, so they
# are not in the scene data and parse_gd does not see them, but the scripts
# read and write them, so the struct must have them. Emitted before the `};`.
RESOURCE_VARS = {
    # ItemManager counts into these as items are dealt (ItemManager.gd:262, 369).
    "AmountResource": ["int amount_player = 0;", "int amount_dealer = 0;"],
}
# Resource paths kept as their file name: the sound bank and textures key on it.
RES_NAMES = {"AudioStream", "CompressedTexture2D"}

EXPORT_RE = re.compile(r"^@export(?:_multiline)?\s+var\s+(\w+)\s*(?::\s*([\w\[\]]+))?", re.M)
FUNC_RE = re.compile(r"^func\s+(\w+)\s*\(", re.M)
CLASS_RE = re.compile(r"^class_name\s+(\w+)(?:\s+extends\s+(\w+))?", re.M)
EXTENDS_RE = re.compile(r"^extends\s+(\w+)", re.M)


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


# Fields whose @export cannot be bound yet because the resource behind it does
# not exist in the port. Each entry is (class, field, why). They are emitted as
# PORT comments instead of bindings, so the field is visibly absent rather than
# silently unresolved; test_scripts_bindings fails on any unresolved binding.
# StandardMaterial3D exports a script writes to, bound as the shared resource
# (godot::MaterialLibrary) the scene's meshes draw with. Every other material
# export is read-only and stays a value. SegmentManager fades the cut barrel
# through `mat`, which is that segment's material_override.
SHARED_MATERIAL_FIELDS = {
    ("SegmentManager", "mat"),
    ("Endless", "display"),          # mat_round indicator main.tres (the rounds display)
    # v2.2 colours the shells' live/blank materials (ColorblindManager and
    # ShellEjectManager._ready); every shell draws with the same two.
    ("Colorblind", "mat_live"),
    ("Colorblind", "mat_blank"),
    ("Colorblind", "mat_emission"),
    ("ShellEjectManager", "mat_live"),
    ("ShellEjectManager", "mat_blank"),
    ("ShellExamine", "mat_live"),
    ("ShellExamine", "mat_blank"),
    ("ShellClass", "mat_live"),
    ("ShellClass", "mat_blank"),
}

# CompressedTexture2D exports a script assigns to a TextureRect's texture:
# bound as the res:// path, the key of the UI layout's textures.
UI_TEXTURE_FIELDS = {
    ("Checkmark", "checkmark_active"),
    ("Checkmark", "checkmark_ianctive"),
}

# CompressedTexture2D exports a script assigns to a material's albedo_texture:
# bound as the key the baker reads (godot::TextureKeyForResource), not the
# file name, so the renderer can find the variant baked for it.
TEXTURE_KEY_FIELDS = {
    ("Endless", "displayrounds"),
    ("CrtIcon", "texture_active"),
    ("CrtIcon", "texture_inactive"),
    # The four blood-splatter frames ShotgunShooting.BloodSplatter swaps in.
    ("ShotgunShooting", "splatters"),
}

UNAVAILABLE = {
    ("DeathManager", "scene_death"):
        "the scene is changed by path, with change_scene_to_file",
}


def parse_gd(file_stem):
    src = read(os.path.join(GD_DIR, file_stem + ".gd"))
    m = CLASS_RE.search(src)
    cls = m.group(1) if m else file_stem
    ext = (m.group(2) if m and m.group(2) else None)
    if not ext:
        e = EXTENDS_RE.search(src)
        ext = e.group(1) if e else "Node"
    exports = [(x.group(1), x.group(2)) for x in EXPORT_RE.finditer(src)]
    lines = src.splitlines()
    starts = [(src.count("\n", 0, f.start()) + 1, f.group(1)) for f in FUNC_RE.finditer(src)]
    funcs = []
    for i, (line, name) in enumerate(starts):
        end = (starts[i + 1][0] - 1) if i + 1 < len(starts) else len(lines)
        while end > line and not lines[end - 1].strip():
            end -= 1
        funcs.append((name, line, end))
    return {"file": file_stem, "class": cls, "extends": ext, "exports": exports,
            "funcs": funcs, "lines": len(lines)}


def all_classes():
    """class_name -> (file stem, extends) for every script."""
    out = {}
    for f in sorted(os.listdir(GD_DIR)):
        if not f.endswith(".gd"):
            continue
        info = parse_gd(f[:-3])
        out[info["class"]] = (info["file"], info["extends"])
    return out


def translated_files():
    if not os.path.isdir(OUT):
        return []
    out = []
    for f in sorted(os.listdir(OUT)):
        stem = f[:-2]
        if f.endswith(".h") and os.path.exists(os.path.join(GD_DIR, stem + ".gd")):
            out.append(stem)
    return out


class TypeMap:
    def __init__(self, classes, translated_classes):
        self.classes = classes
        self.translated = translated_classes

    def is_resource_class(self, t):
        return t in self.classes and self.classes[t][1] == "Resource"

    def kind(self, t):
        """(kind, cpp element type) for a scalar GDScript type, or None to skip."""
        simple = {"int": "int", "float": "double", "bool": "bool", "String": "std::string",
                  "Vector3": "godot::Vector3", "Vector2": "godot::Vector2"}
        if t in simple:
            return ("value_" + t, simple[t])
        if t in RES_NAMES:
            return ("res_name", "std::string")
        if t == "PackedScene":
            return ("scene", "godot::PackedScene*")
        if t == "StandardMaterial3D":
            return ("material", "godot::SurfaceMaterial")
        if t in NATIVE:
            return ("node", "godot::%s*" % NATIVE[t])
        if t in RESOURCES:
            return ("resource", "%s*" % t)
        if self.is_resource_class(t):
            return None
        if t in self.translated:
            return ("node", "%s*" % t)
        if t in self.classes:
            return ("node", "godot::Node*")
        return None

    def field(self, name, t, cls=None):
        """(cpp declaration, bind statement, skip comment)."""
        why_unavailable = UNAVAILABLE.get((cls, name))
        if why_unavailable:
            return (None, None, "// PORT: @export %s : %s (%s)" % (name, t, why_unavailable))
        if t is None:
            return (None, None, "// PORT: @export %s has no type annotation; not bound" % name)
        arr = re.match(r"Array\[(\w+)\]$", t)
        inner = arr.group(1) if arr else t
        k = self.kind(inner)
        if k is None:
            why = "resource not generated yet" if self.is_resource_class(inner) else "type not supported"
            return (None, None, "// PORT: @export %s : %s (%s)" % (name, t, why))
        kind, cpp = k
        if kind == "res_name" and (cls, name) in TEXTURE_KEY_FIELDS and arr:
            return ("std::vector<std::string> %s;  // the baker's texture keys (TextureKeyForResource)" % name,
                    's.%(n)s.clear();  // a re-bind replaces, never appends\n    '
                    'for (const std::string& r : ex.res_paths("%(n)s")) s.%(n)s.push_back(godot::TextureKeyForResource(r));'
                    % {"n": name}, None)
        if kind == "res_name" and (cls, name) in TEXTURE_KEY_FIELDS and not arr:
            return ("std::string %s;  // the baker's texture key (TextureKeyForResource)" % name,
                    's.%(n)s = godot::TextureKeyForResource(ex.res_path("%(n)s"));' % {"n": name}, None)
        if kind == "res_name" and (cls, name) in UI_TEXTURE_FIELDS and not arr:
            return ("std::string %s;  // res:// path, the UI layout's texture key" % name,
                    's.%(n)s = ex.res_path("%(n)s");' % {"n": name}, None)
        if kind == "material" and (cls, name) in SHARED_MATERIAL_FIELDS and not arr:
            return ("godot::SurfaceMaterial* %s = nullptr;  // shared resource (MaterialLibrary)" % name,
                    's.%(n)s = godot::MaterialLibrary::get(ex.res_path("%(n)s"), ex.material("%(n)s"));'
                    % {"n": name}, None)
        note = ""
        if kind == "node" and cpp == "godot::Node*" and inner not in NATIVE:
            note = "  // PORT: %s not translated yet" % inner
        if arr:
            decl = "std::vector<%s> %s;%s" % (cpp, name, note)
            binds = {
                "value_String": 's.%s = ex.strings("%s");',
                "value_Vector3": 's.%s = ex.vector3s("%s");',
                "value_bool": 's.%s = ex.booleans("%s");',
                "res_name": 's.%s = ex.res_names("%s");',
                "node": 'b.nodes(ex, "%s", s.%s);',
                "resource": 'b.resources(ex, "%s", s.%s, &Read%s);',
            }
            if kind not in binds:
                return (None, None, "// PORT: @export %s : %s (array kind not supported)" % (name, t))
            if kind == "node":
                bind = binds[kind] % (name, name)
            elif kind == "resource":
                bind = binds[kind] % (name, name, inner)
            else:
                bind = binds[kind] % (name, name)
            return (decl, bind, None)
        init = {"value_int": " = 0", "value_float": " = 0.0", "value_bool": " = false",
                "scene": " = nullptr", "node": " = nullptr", "resource": " = nullptr"}.get(kind, "")
        decl = "%s %s%s;%s" % (cpp, name, init, note)
        binds = {
            "value_int": 's.%(n)s = ex.integer("%(n)s", s.%(n)s);',
            "value_float": 's.%(n)s = ex.number("%(n)s", s.%(n)s);',
            "value_bool": 's.%(n)s = ex.boolean("%(n)s", s.%(n)s);',
            "value_String": 's.%(n)s = ex.string("%(n)s");',
            "value_Vector3": 's.%(n)s = ex.vector3("%(n)s", s.%(n)s);',
            "value_Vector2": 's.%(n)s = ex.vector2("%(n)s", s.%(n)s);',
            "res_name": 's.%(n)s = ex.res_name("%(n)s");',
            "scene": 'b.scene(ex, "%(n)s", s.%(n)s);',
            "material": 's.%(n)s = ex.material("%(n)s");',
            "node": 'b.node(ex, "%(n)s", s.%(n)s);',
            "resource": 's.%(n)s = b.resource(ex, "%(n)s", &Read%(t)s);',
        }
        return (decl, binds[kind] % {"n": name, "t": inner}, None)


def resource_fields(classes, res):
    info = parse_gd(classes[res][0])
    return info


def write_if_changed(path, text):
    if os.path.exists(path) and read(path) == text:
        return False
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return True


def gen_resources(classes, tm):
    out = ["// GENERATED by tools/gen_script_bindings.py from scripts/*.gd; do not edit.",
           "// The Resource classes the translated scripts use: every field, exact names,",
           "// source order. Instances live in scripts::ResourcePool (bind_support.h).",
           "#pragma once", "", '#include "godot/packed_scene.h"',
           '#include "scripts/bind_support.h"', "", "#include <string>",
           "#include <vector>", "", "namespace scripts {", ""]
    for res in RESOURCES:
        info = resource_fields(classes, res)
        out.append("// scripts/%s.gd" % info["file"])
        out.append("struct %s : ResourceBase {" % res)
        reads = []
        for name, t in info["exports"]:
            if t == "PackedScene":
                # A resource reader has no Binder, so the generic path below
                # would emit `(void)scene(...)`. Look the scene up in the same
                # registry Binder::scene uses, by the res:// path the export
                # records; PackedScene::load must have run first.
                out.append("    godot::PackedScene* %s = nullptr;" % name)
                reads.append('    r.%s = godot::PackedScene::find(ex.res_path("%s"));'
                             % (name, name))
                continue
            decl, bind, skip = tm.field(name, t)
            if skip:
                out.append("    " + skip)
                continue
            out.append("    " + decl)
            reads.append("    " + bind.replace("s.", "r.").replace("b.", "(void)"))
        for var in RESOURCE_VARS.get(res, []):
            out.append("    " + var)
        out.append("};")
        out.append("")
        out.append("inline void Read%s(const ExportReader& ex, %s& r) {" % (res, res))
        out.extend(reads)
        out.append("}")
        out.append("")
    out.append("}  // namespace scripts")
    return "\n".join(out) + "\n"


def gen_forward(translated_infos):
    out = ["// GENERATED by tools/gen_script_bindings.py; do not edit.",
           "// Every translated script class and generated resource, declared, so one",
           "// script's header can hold a pointer to another's without an include cycle.",
           "#pragma once", "", "namespace scripts {", ""]
    for res in RESOURCES:
        out.append("struct %s;" % res)
    for info in translated_infos:
        out.append("class %s;" % info["class"])
    out += ["", "}  // namespace scripts"]
    return "\n".join(out) + "\n"


def gen_skeleton(info, tm):
    out = ["// source: scripts/%s.gd (%d lines)" % (info["file"], info["lines"]),
           "// PORT deviations:",
           "//   (none yet)",
           "#pragma once", "", '#include "godot/animation_player.h"', '#include "godot/control.h"',
           '#include "godot/node.h"', '#include "godot/physics.h"',
           '#include "godot/script.h"', '#include "scripts/forward.h"', '#include "seq/task.h"', "",
           "#include <string>", "#include <vector>", "", "namespace scripts {", "",
           "class %s : public godot::Script {" % info["class"], "public:",
           '    static constexpr const char* kClassName = "%s";' % info["class"],
           "    const char* get_class_name() const override { return kClassName; }", ""]
    for name, t in info["exports"]:
        decl, _, skip = tm.field(name, t, info["class"])
        out.append("    " + (skip if skip else decl))
    out.append("")
    for name, a, b in info["funcs"]:
        out.append("    // PORT: omitted %s (not translated yet) -- gd: L%d-L%d" % (name, a, b))
    out += ["};", "", "}  // namespace scripts"]
    return "\n".join(out) + "\n"


def gen_bindings(translated_infos, tm):
    out = ["// GENERATED by tools/gen_script_bindings.py; do not edit.",
           "// Connects each translated script's @export fields to the runtime scene",
           "// export, and registers every translated script by its .gd file name.",
           '#include "godot/material.h"', '#include "godot/packed_scene.h"', '#include "godot/particles.h"', '#include "scripts/bind_support.h"',
           '#include "scripts/resources.h"']
    for info in translated_infos:
        out.append('#include "scripts/%s.h"' % info["file"])
    out += ["", "#include <memory>", "#include <string_view>", "", "namespace scripts {",
            "namespace {", ""]
    for info in translated_infos:
        out.append("void BindFields_%s(%s& s, const ExportReader& ex, Binder& b) {"
                   % (info["class"], info["class"]))
        used_b = False
        for name, t in info["exports"]:
            _, bind, skip = tm.field(name, t, info["class"])
            if skip:
                out.append("    " + skip)
                continue
            if "b." in bind:
                used_b = True
            out.append("    " + bind)
        if not used_b:
            out.append("    (void)b;")
        if not info["exports"]:
            out.append("    (void)s;")
            out.append("    (void)ex;")
        out += ["}", ""]
    out += ["}  // namespace", "", "void RegisterScripts() {"]
    for info in translated_infos:
        out.append('    godot::register_script_class("%s", [] { return std::unique_ptr<godot::Script>(std::make_unique<%s>()); });'
                   % (info["file"], info["class"]))
    out.append("    godot::PackedScene::set_script_binder(&BindInstanceScripts);")
    out += ["}", "", "bool BindFieldsFor(godot::Script& script, const ExportReader& ex, Binder& b) {",
            "    const std::string_view c = script.get_class_name();"]
    for info in translated_infos:
        out.append("    if (c == %s::kClassName) {" % info["class"])
        out.append("        BindFields_%s(static_cast<%s&>(script), ex, b);" % (info["class"], info["class"]))
        out.append("        return true;")
        out.append("    }")
    out += ["    return false;", "}", "", "}  // namespace scripts"]
    return "\n".join(out) + "\n"


def main(argv):
    classes = all_classes()
    os.makedirs(OUT, exist_ok=True)
    for stem in argv:
        if not os.path.exists(os.path.join(GD_DIR, stem + ".gd")):
            sys.exit("no scripts/%s.gd" % stem)
    wanted = set(argv) | set(translated_files())
    translated_infos = [parse_gd(s) for s in sorted(wanted)]
    tm = TypeMap(classes, {i["class"] for i in translated_infos})
    for info in translated_infos:
        path = os.path.join(OUT, info["file"] + ".h")
        if not os.path.exists(path):
            write_if_changed(path, gen_skeleton(info, tm))
            print("skeleton  src/scripts/%s.h" % info["file"])
    for name, text in (("forward.h", gen_forward(translated_infos)),
                       ("resources.h", gen_resources(classes, tm)),
                       ("bindings.cpp", gen_bindings(translated_infos, tm))):
        changed = write_if_changed(os.path.join(OUT, name), text)
        print("%s src/scripts/%s" % ("wrote    " if changed else "unchanged", name))
    print("translated: %s" % ", ".join(i["file"] for i in translated_infos))


if __name__ == "__main__":
    main(sys.argv[1:])
