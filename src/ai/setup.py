"""Install optional, isolated local AI tools. No photos leave the computer."""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import urllib.request
import venv


def emit(message):
    print(json.dumps({"event": "progress", "message": message}), flush=True)


def valid(path, entry):
    if not path.is_file() or path.stat().st_size != entry["bytes"]:
        return False
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest() == entry["sha256"]


def install(home):
    home.mkdir(parents=True, exist_ok=True)
    with (home / "install.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        install_locked(home)


def install_locked(home):
    (home / "ready.json").unlink(missing_ok=True)
    manifest = json.loads(Path(__file__).with_name("models.json").read_text())
    emit("Preparing the local AI environment…")
    venv.EnvBuilder(with_pip=True, symlinks=True).create(home / "venv")
    subprocess.run([str(home / "venv/bin/python"), "-m", "pip", "install",
                    "--disable-pip-version-check", "--only-binary=:all:",
                    *manifest["packages"]], stdout=sys.stderr, check=True)
    (home / "models").mkdir(exist_ok=True)
    for index, entry in enumerate(manifest["models"], 1):
        target = home / "models" / entry["name"]
        if valid(target, entry):
            continue
        kind = "object selection" if entry["name"].startswith("sam2") else "object removal"
        emit(f"Downloading {kind} model ({index} of {len(manifest['models'])})…")
        part = target.with_suffix(".part")
        try:
            with urllib.request.urlopen(entry["url"], timeout=60) as response, part.open("wb") as out:
                size = 0
                while block := response.read(1024 * 1024):
                    size += len(block)
                    if size > entry["bytes"]:
                        raise ValueError("Unexpected model download size")
                    out.write(block)
            if not valid(part, entry):
                raise ValueError("Model verification failed: " + entry["name"])
            os.replace(part, target)
        finally:
            part.unlink(missing_ok=True)
    (home / "ready.json").write_text(json.dumps(manifest))
    print(json.dumps({"ok": True}), flush=True)


if __name__ == "__main__":
    try:
        install(Path(sys.argv[1]).resolve())
    except Exception as error:
        print(json.dumps({"ok": False, "error": str(error)}), flush=True)
        sys.exit(1)
