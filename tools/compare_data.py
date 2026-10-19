"""Compare two data folders for the bsht-extract gate.

JSON is compared by value: floats to 1e-5, or one part in a million for
large float32 values. Node names Godot generates in runtime_*.json
(@Node3D@12) are matched by order, since they change between runs.
.rgba files report the largest channel difference. Everything else is
compared byte for byte.

    python tools/compare_data.py <expected dir> <actual dir> [--names=a,b]
"""
import json
import math
import re
import sys
from pathlib import Path

AUTO = re.compile(r"@[A-Za-z0-9_]+@\d+")


def auto_names(text):
    seen = {}
    return AUTO.sub(lambda m: seen.setdefault(m.group(0), f"@auto@{len(seen)}"), text)


def same(a, b, path="$"):
    if isinstance(a, float) or isinstance(b, float):
        if isinstance(a, (int, float)) and isinstance(b, (int, float)) and math.isclose(a, b, rel_tol=1e-6, abs_tol=1e-5):
            return []
        return [f"{path}: {a!r} != {b!r}"]
    if type(a) is not type(b):
        return [f"{path}: type {type(a).__name__} != {type(b).__name__}"]
    if isinstance(a, dict):
        out = [f"{path}: key {k!r} only in expected" for k in sorted(set(a) - set(b))]
        out += [f"{path}: key {k!r} only in actual" for k in sorted(set(b) - set(a))]
        for k in sorted(set(a) & set(b)):
            out += same(a[k], b[k], f"{path}.{k}")
        return out
    if isinstance(a, list):
        if len(a) != len(b):
            return [f"{path}: length {len(a)} != {len(b)}"]
        out = []
        for i, (x, y) in enumerate(zip(a, b)):
            out += same(x, y, f"{path}[{i}]")
        return out
    return [] if a == b else [f"{path}: {a!r} != {b!r}"]


def compare(exp, act):
    if exp.suffix == ".json":
        ta, tb = exp.read_text(encoding="utf-8"), act.read_text(encoding="utf-8")
        if exp.name.startswith("runtime_"):
            ta, tb = auto_names(ta), auto_names(tb)
        return same(json.loads(ta), json.loads(tb))[:20]
    a, b = exp.read_bytes(), act.read_bytes()
    if exp.suffix == ".rgba":
        if len(a) != len(b):
            return [f"size {len(a)} != {len(b)}"]
        worst = max((abs(x - y) for x, y in zip(a, b)), default=0)
        return [] if worst == 0 else [f"max channel difference {worst}"]
    return [] if a == b else ["bytes differ"]


def main():
    exp_dir, act_dir = Path(sys.argv[1]), Path(sys.argv[2])
    names = None
    if len(sys.argv) > 3 and sys.argv[3].startswith("--names="):
        names = sys.argv[3].split("=", 1)[1].split(",")
    files = names or sorted(p.name for p in exp_dir.iterdir() if p.is_file())
    bad = 0
    for name in files:
        exp, act = exp_dir / name, act_dir / name
        if not act.exists():
            print(f"MISSING {name}")
            bad += 1
            continue
        diffs = compare(exp, act)
        if diffs:
            bad += 1
            print(f"DIFF    {name}")
            for d in diffs:
                print(f"          {d}")
        else:
            print(f"same    {name}")
    extra = sorted({p.name for p in act_dir.iterdir() if p.is_file()} - set(files)) if names is None else []
    for name in extra:
        print(f"EXTRA   {name}")
    print(f"{len(files) - bad}/{len(files)} match, {len(extra)} extra")
    return 1 if bad or extra else 0


if __name__ == "__main__":
    sys.exit(main())
