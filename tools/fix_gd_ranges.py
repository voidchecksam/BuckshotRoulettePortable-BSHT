#!/usr/bin/env python3
"""Point the `gd: La-Lb` references in src/scripts at the reference game's scripts.

Each translated script names the lines of the GDScript it follows. A reference on
a declaration in X.h, or on the comment line just above `X::F(` in X.cpp, should
give the lines of `func F` (or of `var F`) in the reference game's X.gd.

    python tools/fix_gd_ranges.py [--check] [--game DIR] [--root DIR]

--check lists every reference that differs and exits 1 if any does; without it
the references are rewritten in place. References the tool cannot place (inside a
function body, or citing several declarations at once) are listed for a person to
check by hand; they do not fail the check.
--game defaults to $BSHT_REFERENCE_GAME or external/game-v2.2/game; without it
the tool exits 77 (CTest's skip code). --root is the tree whose src/scripts is
read (the repo; a test fixture for the negative test).
"""
import argparse
import os
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
REF = re.compile(r"gd: L(\d+)(?:-L(\d+))?")
CPP_DEF = re.compile(r"^[\w:<>,\s\*&]*?\b(\w+)::(\w+)\s*\(")
CALL_NAME = re.compile(r"(\w+)\s*\(")
FIELD_NAME = re.compile(r"(\w+)\s*(?:=|\{|;|\[)")
GD_FUNC = re.compile(r"^(?:static\s+)?func\s+(\w+)")
GD_VAR = re.compile(r"^(?:@\w+(?:\([^)]*\))?\s+)*(?:var|const|signal)\s+(\w+)")
OMITTED = re.compile(r"PORT: omitted (\w+)")
# C++ cannot await in _ready(), so its body runs as a coroutine of its own.
SPLIT_FROM = {"ReadyRoutine": "_ready", "Ready": "_ready"}


def gd_ranges(path):
    """name -> (first line, last line), 1-based, for each top-level func, var,
    const and signal. A declaration runs to the line before the next one; a
    closing bracket at the start of a line still belongs to it."""
    lines = path.read_text(encoding="utf-8").splitlines()
    out = {}
    top = [i for i, l in enumerate(lines)
           if l.strip() and not l[0].isspace() and not l.startswith("#") and l[0] not in "}])"]
    for k, i in enumerate(top):
        m = GD_FUNC.match(lines[i]) or GD_VAR.match(lines[i])
        if not m:
            continue
        end = (top[k + 1] if k + 1 < len(top) else len(lines)) - 1
        while end > i and (not lines[end].strip() or lines[end].lstrip().startswith("#")):
            end -= 1
        out.setdefault(m.group(1), (i + 1, end + 1))
    return out


def declared_name(code):
    """The function or field a header line declares, or None."""
    code = code.split("//")[0]
    if "(" in code and "operator" not in code:
        names = CALL_NAME.findall(code)
        return names[0] if names else None
    names = FIELD_NAME.findall(code)
    return names[0] if names else None


def is_comment(line):
    return line.lstrip().startswith("//")


def place(path, lines, i):
    """(name, is_function) for the reference on line i, or (None, False).

    On a declaration line: what it declares. On a comment line: the function
    defined (.cpp) or declared (.h) right below the comment block, and only
    when this is the block's one reference: a block that cites several
    ranges is describing more than the function."""
    line = lines[i]
    omitted = OMITTED.search(line)
    if omitted:
        return omitted.group(1), True
    if not is_comment(line):
        if path.suffix == ".h":
            code = line.split("//")[0]
            if ")" in code and "(" not in code:
                # the last line of a declaration split over several
                k = i - 1
                while k >= 0 and "(" not in lines[k].split("//")[0]:
                    k -= 1
                if k >= 0:
                    return declared_name(lines[k]), True
            return declared_name(line), "(" in code
        m = CPP_DEF.match(line)
        return (m.group(2), True) if m else (None, False)
    s = i
    while s > 0 and is_comment(lines[s - 1]):
        s -= 1
    j = i + 1
    while j < len(lines) and is_comment(lines[j]):
        j += 1
    if j >= len(lines) or sum(len(REF.findall(lines[k])) for k in range(s, j)) != 1:
        return None, False
    m = CPP_DEF.match(lines[j])
    if m:
        return m.group(2), True
    if path.suffix == ".h" and "(" in lines[j].split("//")[0]:
        return declared_name(lines[j]), True
    return None, False


def fmt(first, last):
    return f"L{first}" if first == last else f"L{first}-L{last}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--game")
    ap.add_argument("--root")
    args = ap.parse_args()
    root = pathlib.Path(args.root).resolve() if args.root else REPO
    game = pathlib.Path(args.game or os.environ.get("BSHT_REFERENCE_GAME") or REPO / "external/game-v2.2/game")
    scripts = game / "scripts"
    if not scripts.is_dir():
        print(f"no reference game scripts at {scripts}: skipped")
        return 77

    stale, unplaced, checked = [], [], 0
    for path in sorted((root / "src/scripts").glob("*.[hc]*")):
        gd = scripts / (path.stem + ".gd")
        if path.stem in ("bind_support", "bindings") or not gd.is_file():
            continue
        ranges = gd_ranges(gd)
        lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
        changed = False
        for i, line in enumerate(lines):
            for m in list(REF.finditer(line)):
                checked += 1
                where = f"{path.relative_to(root).as_posix()}:{i + 1}"
                name, function = place(path, [l.rstrip("\r\n") for l in lines], i)
                if name not in ranges and name in SPLIT_FROM:
                    name = SPLIT_FROM[name]
                if name not in ranges:
                    unplaced.append(f"{where}: {m.group(0)} ({name or 'not on a declaration'})")
                    continue
                want = fmt(*ranges[name])
                have = fmt(int(m.group(1)), int(m.group(2) or m.group(1)))
                old_span = int(m.group(2) or m.group(1)) - int(m.group(1))
                if not function and old_span != ranges[name][1] - ranges[name][0]:
                    # a field line citing a group of declarations
                    unplaced.append(f"{where}: {m.group(0)} ({name}: a group, {want} alone)")
                    continue
                if want != have:
                    stale.append(f"{where}: {name} is {want}, the comment says {have}")
                    lines[i] = lines[i].replace("gd: " + have, "gd: " + want, 1)
                    changed = True
        if changed and not args.check:
            path.write_text("".join(lines), encoding="utf-8", newline="")

    for s in stale:
        print(("stale    " if args.check else "fixed    ") + s)
    for u in unplaced:
        print("unplaced " + u)
    print(f"{checked} references, {len(stale)} {'stale' if args.check else 'fixed'}, {len(unplaced)} unplaced")
    # An unplaced reference (inside a body, or citing several declarations) is
    # listed for a person; only a placed one that disagrees fails the check.
    if args.check and stale:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
