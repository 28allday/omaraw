#!/usr/bin/env python3
"""Stage the complete, offline AI runtime from checksum-pinned package sources."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

root = Path(__file__).resolve().parent.parent
sources, stage = map(lambda p: Path(p).resolve(), sys.argv[1:3])
lock = json.loads((root / "pkgbuild/ai-sources.json").read_text())
abi = lock["python"]
for entry in lock["sources"]:
    path = sources / entry["name"]
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if path.stat().st_size != entry["bytes"] or digest != entry["sha256"]:
        raise SystemExit(f"AI source verification failed: {path.name}")

base = stage / "usr/lib/omaraw"
runtime = base / "ai-runtime"
if runtime.exists():
    shutil.rmtree(runtime)
runtime.mkdir(parents=True)
# Keep the interpreter private too: an Arch system-Python upgrade must not
# invalidate the included extension wheels or trigger a runtime download.
interpreter = next(e for e in lock["sources"] if e["kind"] == "interpreter")
with tempfile.TemporaryDirectory() as temporary:
    extracted = Path(temporary)
    subprocess.run(["bsdtar", "-xf", str(sources / interpreter["name"]), "-C", temporary,
                    f"usr/bin/python{abi}", f"usr/lib/libpython{abi}.so.1.0", f"usr/lib/python{abi}"], check=True)
    shutil.copytree(extracted / f"usr/lib/python{abi}", runtime / f"lib/python{abi}")
    (runtime / "bin").mkdir()
    shutil.copyfile(extracted / f"usr/bin/python{abi}", runtime / f"bin/python{abi}")
    (runtime / f"bin/python{abi}").chmod(0o755)
    shutil.copyfile(extracted / f"usr/lib/libpython{abi}.so.1.0", runtime / f"lib/libpython{abi}.so.1.0")
(runtime / "bin/python").symlink_to(f"python{abi}")
subprocess.run(["patchelf", "--set-rpath", "$ORIGIN/../lib", str(runtime / f"bin/python{abi}")], check=True)
stdlib = runtime / f"lib/python{abi}"
for name in ("test", "idlelib", "tkinter", "ensurepip"):
    shutil.rmtree(stdlib / name, ignore_errors=True)
for path in stdlib.glob("config-*"):
    shutil.rmtree(path)
for pattern in ("_test*", "_ctypes_test*", "_tkinter*"):
    for path in (stdlib / "lib-dynload").glob(pattern):
        path.unlink()
site = runtime / f"lib/python{abi}/site-packages"
wheels = [str(sources / e["name"]) for e in lock["sources"] if e["kind"] == "wheel"]
subprocess.run([sys.executable, "-m", "pip", "install", "--no-index", "--no-deps",
                "--python-version", abi, "--no-compile", "--disable-pip-version-check",
                "--target", str(site), *wheels], check=True)
shutil.rmtree(site / "bin", ignore_errors=True)
for path in site.glob("*.dist-info/direct_url.json"):
    path.unlink()
for path in runtime.rglob("__pycache__"):
    shutil.rmtree(path)

notices = stage / "usr/share/licenses/omaraw/ai-runtime"
notices.mkdir(parents=True, exist_ok=True)
shutil.copyfile(root / "pkgbuild/ai-sources.json", notices / "sources.json")
python_source = next(e for e in lock["sources"] if e.get("project") == "Python" and e["kind"] == "source")
with tarfile.open(sources / python_source["name"]) as archive:
    licence = next(m for m in archive if m.name.endswith("/LICENSE") and m.name.count("/") == 1)
    (notices / "LICENSE.Python").write_bytes(archive.extractfile(licence).read())
for distribution in site.glob("*.dist-info"):
    target = notices / distribution.name
    target.mkdir(exist_ok=True)
    for item in distribution.iterdir():
        if item.name.lower().startswith(("license", "notice", "copying")):
            if item.is_dir():
                shutil.copytree(item, target / item.name, dirs_exist_ok=True)
            else:
                shutil.copyfile(item, target / item.name)

for group in ("ai", "denoise"):
    home = base / group
    if home.exists():
        shutil.rmtree(home)
    (home / "scripts").mkdir(parents=True)
    (home / "models").mkdir()
    (home / "venv").symlink_to("../ai-runtime", target_is_directory=True)
    for name in ("worker.py", "models.json", "LICENSES.txt"):
        shutil.copyfile(root / "src" / group / name, home / "scripts" / name)
    manifest = json.loads((home / "scripts/models.json").read_text())
    for model in manifest["models"]:
        entry = next(e for e in lock["sources"] if e.get("group") == group and e.get("model") == model["name"])
        source, target = sources / entry["name"], home / "models" / model["name"]
        if "source" in model:
            mode = "--convert-fixed" if model.get("fixedTile") else "--convert"
            subprocess.run([str(runtime / "bin/python"), "-I", "-B", str(root / "src/denoise/setup.py"),
                            mode, str(source), str(target)], check=True)
        else:
            shutil.copyfile(source, target)
        with target.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if target.stat().st_size != model["bytes"] or digest != model["sha256"]:
            raise SystemExit(f"Packaged model verification failed: {model['name']}")
    shutil.copyfile(home / "scripts/models.json", home / "ready.json")
    (home / "bundled").touch()
    (home / "install.lock").touch()
print("All AI models and runtime dependencies staged for offline use.")
