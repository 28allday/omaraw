#!/usr/bin/env python3
"""Print the compiler flags libdarktable was built with, for src/engine/bridge.c.

usage: dt-flags.py <darktable-src-dir> cflags [darktable-build-dir]
Reads the build's compile_commands.json and echoes every -I/-D/-isystem flag
of one core translation unit, so the bridge sees the same config.h and
include paths as the library it links.
"""
import json, shlex, sys
from pathlib import Path

if len(sys.argv) not in (3, 4) or sys.argv[2] != "cflags":
    sys.exit(__doc__)
src = sys.argv[1]
build = Path(sys.argv[3]) if len(sys.argv) == 4 else Path(src) / "_build"
db = json.loads((build / "compile_commands.json").read_text())
for e in db:
    if e["file"].endswith("/src/common/darktable.c"):
        # A package engine may have been built in a /build bubblewrap mount.
        # Relocate its source/build include roots for local app development.
        original_src = e["file"].removesuffix("/src/common/darktable.c")
        original_build = e["directory"]
        def relocate(path):
            for old, new in ((original_src, str(Path(src).resolve())),
                             (original_build, str(build.resolve()))):
                if path == old or path.startswith(old + "/"):
                    return new + path[len(old):]
            return path
        toks = e["arguments"] if "arguments" in e else shlex.split(e["command"])
        out, i = [], 0
        while i < len(toks):
            t = toks[i]
            if t == "-isystem" and i + 1 < len(toks):
                out += [t, relocate(toks[i + 1])]; i += 2; continue
            if t.startswith(("-I", "-D")) and t != "-DNDEBUG":
                out.append("-I" + relocate(t[2:]) if t.startswith("-I") else t)
            i += 1
        print(shlex.join(out))
        break
else:
    sys.exit("No darktable core translation unit in compile_commands.json")
