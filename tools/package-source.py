#!/usr/bin/env python3
"""Snapshot Git-tracked working files and render the Arch package recipe.

New files must be staged first. Ignored/private files, build output and the
development engine are never included. Dirty snapshots record their diff hash.
"""
import gzip
import hashlib
import io
from pathlib import Path
import os
import re
import subprocess
import sys
import tarfile

root = Path(__file__).resolve().parent.parent
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)


def git(*args):
    return subprocess.check_output(["git", "-C", str(root), *args])


# Silently omitting a new module or embedded model makes a broken release.
# Keep the allowlist at Git's index boundary; never sweep private/untracked
# working directories into a distributable archive.
untracked = [name for name in git("ls-files", "--others", "--exclude-standard", "-z").decode().split("\0")
             if name.startswith(("src/", "data/", "patches/", "tests/", "tools/"))]
if untracked:
    raise SystemExit("Required source files are not staged; review and stage them before packaging:\n"
                     + "\n".join(sorted(untracked)))

template = (root / "pkgbuild/PKGBUILD.in").read_text()
version = re.search(r"^pkgver=([\w.]+)$", template, re.MULTILINE).group(1)
# The package and the application must agree on what version this is.
app_version = (root / "VERSION").read_text().strip()
if re.sub(r"-(alpha|beta|rc)\.", r"\1", app_version) != version:
    raise SystemExit(f"pkgbuild/PKGBUILD.in says {version} but VERSION says {app_version}")
prefix = f"omaraw-{version}"
epoch = int(os.environ.get("SOURCE_DATE_EPOCH", git("log", "-1", "--format=%ct").decode().strip()))
revision = git("rev-parse", "HEAD").decode().strip()
diff = git("diff", "--binary", "HEAD", "--")
info = (f"version={version}\nrevision={revision}\ndirty={'yes' if diff else 'no'}\n"
        f"diff_sha256={hashlib.sha256(diff).hexdigest()}\nsource_date_epoch={epoch}\n").encode()
archive = out / f"{prefix}.tar.gz"
temporary = archive.with_suffix(".tmp")
with temporary.open("wb") as raw, gzip.GzipFile(filename="", fileobj=raw, mode="wb", mtime=0) as compressed:
    with tarfile.open(fileobj=compressed, mode="w", format=tarfile.GNU_FORMAT) as tar:
        files = sorted(set(git("ls-files", "-z").decode().split("\0")) - {""})
        for name in files:
            path = root / name
            if not path.exists() and not path.is_symlink():
                continue  # A tracked deletion in the working snapshot.
            entry = tar.gettarinfo(str(path), arcname=f"{prefix}/{name}")
            entry.uid = entry.gid = 0
            entry.uname = entry.gname = "root"
            entry.mtime = epoch
            entry.mode = 0o755 if entry.mode & 0o111 else 0o644
            if entry.isfile():
                with path.open("rb") as contents:
                    tar.addfile(entry, contents)
            else:
                tar.addfile(entry)
        entry = tarfile.TarInfo(f"{prefix}/SOURCE-INFO")
        entry.size, entry.mtime, entry.mode = len(info), epoch, 0o644
        entry.uname = entry.gname = "root"
        tar.addfile(entry, io.BytesIO(info))
temporary.replace(archive)
checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
assert template.count("@OMARAW_SHA256@") == 1
install_script = (root / "pkgbuild/omaraw.install").read_bytes()
(out / "omaraw.install").write_bytes(install_script)
assert template.count("@INSTALL_SHA256@") == 1
(out / "PKGBUILD").write_text(template.replace("@OMARAW_SHA256@", checksum)
                             .replace("@INSTALL_SHA256@", hashlib.sha256(install_script).hexdigest()))
print(f"Source snapshot: {archive}\nSHA256: {checksum}\nRevision: {revision}" + (" (working changes included)" if diff else ""))
