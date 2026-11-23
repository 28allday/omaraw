#!/usr/bin/env python3
"""Record and check the system libraries each OmaRAW release package links.

A release can carry more than one package per architecture: Omarchy stable and
Omarchy edge (current Arch) can ship different library series at the same time
(OpenCV 5.0 and 5.1, for example). The LIBRARIES release asset lists, for every
package, each system library it loads and the Arch package that provides it.
install.sh uses it to pick the package that loads on the user's system, and the
library watch uses it to notice when a repository moves past every package.

  release-libraries.py list PACKAGE=MIRROR...     print a LIBRARIES manifest
  release-libraries.py watch LIBRARIES TARGET...  report, per target, a package
                                                   whose libraries it still has

MIRROR is a pacman Server line such as https://example.org/$repo/os/$arch.
TARGET is ARCH|LABEL|MIRROR. The files those mirrors serve are only read.
"""
import io
import re
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

REPOSITORIES = ("core", "extra")
PACKAGE = re.compile(r"^omaraw-.+-(x86_64|aarch64)(-[A-Za-z0-9.]+)?\.pkg\.tar\.zst$")
NEEDED = re.compile(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]")
HEADER = """\
# OmaRAW release libraries: package, system library it loads, the Arch package
# that provides that library, and that package's version when it was built.
"""


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": "omaraw-release-libraries"})
    with urllib.request.urlopen(request, timeout=300) as response:
        return response.read()


def provided(mirror, architecture):
    """Map each library file name under usr/lib to (package, version)."""
    found, nested = {}, {}
    for repository in REPOSITORIES:
        url = mirror.replace("$repo", repository).replace("$arch", architecture)
        url = f"{url.rstrip('/')}/{repository}.files"
        with tarfile.open(fileobj=io.BytesIO(fetch(url)), mode="r:*") as database:
            entries = {}
            for member in database:
                if not member.isfile() or "/" not in member.name:
                    continue
                directory, kind = member.name.rsplit("/", 1)
                if kind in ("desc", "files"):
                    entries.setdefault(directory, {})[kind] = database.extractfile(member).read().decode()
        for entry in entries.values():
            fields = dict(re.findall(r"%([A-Z0-9]+)%\n(.*?)(?:\n\n|\n?$)", entry.get("desc", ""), re.S))
            name, version = fields.get("NAME"), fields.get("VERSION")
            if not name or not version:
                continue
            for path in entry.get("files", "").splitlines():
                if not path.startswith("usr/lib/") or path.endswith("/"):
                    continue
                library = path.rsplit("/", 1)[1]
                if ".so" not in library:
                    continue
                target = found if path.count("/") == 2 else nested
                target.setdefault(library, (name, version))
    # Libraries in subdirectories only count when no package has them directly.
    return {**nested, **found}


def needed(package):
    """System libraries the app and engine load, as check-libraries sees them."""
    listing = subprocess.run(["tar", "--zstd", "-tf", str(package)], check=True,
                             capture_output=True, text=True).stdout.splitlines()
    private = {Path(path).name for path in listing
               if path.startswith("usr/lib/omaraw/") and not path.endswith("/")}
    wanted = [path for path in listing
              if path == "usr/bin/omaraw"
              or (path.startswith("usr/lib/omaraw/engine/") and ".so" in Path(path).name
                  and not path.endswith("/"))]
    if "usr/bin/omaraw" not in wanted:
        raise SystemExit(f"{package.name} has no usr/bin/omaraw")
    libraries = set()
    with tempfile.TemporaryDirectory(prefix="omaraw-libraries.") as work:
        subprocess.run(["tar", "--zstd", "-xf", str(package), "-C", work, "--no-same-owner",
                        "--", *wanted], check=True)
        for path in wanted:
            file = Path(work, path)
            if file.is_symlink() or not file.is_file():
                continue
            with file.open("rb") as handle:
                if handle.read(4) != b"\x7fELF":
                    continue
            dynamic = subprocess.run(["readelf", "-dW", str(file)], check=True,
                                     capture_output=True, text=True).stdout
            libraries.update(NEEDED.findall(dynamic))
    return sorted(library for library in libraries if library not in private)


def manifest(arguments):
    rows, indexes = [], {}
    for argument in arguments:
        path, separator, mirror = argument.partition("=")
        package = Path(path)
        match = PACKAGE.match(package.name)
        if not separator or not mirror or not match:
            raise SystemExit(f"Expected PACKAGE=MIRROR for an OmaRAW package, got {argument!r}")
        architecture = match.group(1)
        key = (mirror, architecture)
        if key not in indexes:
            indexes[key] = provided(mirror, architecture)
        unknown = []
        for library in needed(package):
            owner = indexes[key].get(library)
            if owner is None:
                unknown.append(library)
            else:
                rows.append(f"{package.name} {library} {owner[0]} {owner[1]}")
        if unknown:
            raise SystemExit(f"{package.name}: no package in {mirror} provides {' '.join(unknown)}"
                             " (build from that mirror again before writing the manifest)")
    sys.stdout.write(HEADER + "".join(row + "\n" for row in rows))


def read_manifest(path):
    packages = {}
    for line in Path(path).read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        name, library, owner, version = line.split()
        packages.setdefault(name, []).append((library, owner, version))
    return packages


def watch(path, targets):
    packages = read_manifest(path)
    if not packages:
        raise SystemExit(f"{path} lists no packages")
    for target in targets:
        architecture, label, mirror = target.split("|", 2)
        available = provided(mirror, architecture)
        best = None
        for name, libraries in packages.items():
            match = PACKAGE.match(name)
            if not match or match.group(1) != architecture:
                continue
            missing = [(library, owner) for library, owner, _ in libraries if library not in available]
            if best is None or len(missing) < len(best[1]):
                best = (name, missing)
        if best is None:
            print(f"PROBLEM\t{label}\tno {architecture} package in this release")
        elif not best[1]:
            print(f"OK\t{label}\t{best[0]}")
        else:
            gone = ", ".join(f"{library} ({owner} {available_version(available, owner)})"
                             for library, owner in best[1])
            print(f"PROBLEM\t{label}\tclosest is {best[0]}; the repositories no longer have {gone}")


def available_version(available, owner):
    versions = {version for name, version in available.values() if name == owner}
    return "now " + ", ".join(sorted(versions)) if versions else "no longer provided"


def main(argv):
    if len(argv) >= 2 and argv[0] == "list":
        manifest(argv[1:])
    elif len(argv) >= 3 and argv[0] == "watch":
        watch(argv[1], argv[2:])
    else:
        sys.stderr.write(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
