#!/usr/bin/env python3
"""Validate an installed package in a disposable offline build environment."""
import hashlib
import json
from pathlib import Path
import platform
import re
import struct
import subprocess
import sys
import time
import zlib

work = Path(sys.argv[1]).resolve()
work.mkdir(parents=True, exist_ok=False)
(work / "photos").mkdir()
(work / "runtime").mkdir(mode=0o700)
env = dict(PATH="/usr/bin", LANG="C.UTF-8",
           XDG_CONFIG_HOME=str(work / "config"), XDG_DATA_HOME=str(work / "data"),
           XDG_CACHE_HOME=str(work / "cache"), XDG_RUNTIME_DIR=str(work / "runtime"),
           QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
           OMA_GPU="cpu", OMA_GPU_UI="software", OMP_NUM_THREADS="2", OMP_THREAD_LIMIT="2")
machine = {"x86_64": 62, "aarch64": 183}[platform.machine()]
# The private patched engine must win even when standalone darktable exists.
dynamic = subprocess.check_output(["readelf", "-d", "/usr/bin/omaraw"], env=env, text=True)
runpath = re.search(r"\((?:RUNPATH|RPATH)\).*\[(.*?)\]", dynamic)
assert runpath and runpath[1].split(":")[0] == "/usr/lib/omaraw/engine/lib/darktable", dynamic
resolved = subprocess.check_output(["ldd", "/usr/bin/omaraw"], env=env, text=True)
assert re.search(r"libdarktable\.so => /usr/lib/omaraw/engine/lib/darktable/libdarktable\.so\s", resolved), resolved
elf_count = 0
for path in [Path("/usr/bin/omaraw"), *Path("/usr/lib/omaraw").rglob("*")]:
    if not path.is_file() or path.is_symlink():
        continue
    with path.open("rb") as stream:
        if stream.read(4) != b"\x7fELF":
            continue
        stream.seek(0)
        data = stream.read()
    assert data[4:6] == bytes([2, 1]) and struct.unpack_from("<H", data, 18)[0] == machine, path
    if machine == 183:
        offset = struct.unpack_from("<Q", data, 32)[0]
        size, count = struct.unpack_from("<HH", data, 54)
        for i in range(count):
            kind, flags, file_offset, address, physical, file_size, memory_size, alignment = struct.unpack_from("<IIQQQQQQ", data, offset + i * size)
            if kind == 1:
                assert alignment >= 16384 and file_offset % 16384 == address % 16384, path
    check_env = dict(env)
    if path.parent.name.endswith(".libs"):
        check_env["LD_LIBRARY_PATH"] = str(path.parent)
    check = subprocess.run(["ldd", str(path)], env=check_env, capture_output=True, text=True)
    assert check.returncode == 0 and "not found" not in check.stdout, (path, check.stdout, check.stderr)
    elf_count += 1

def chunk(kind, data):
    return struct.pack("!I", len(data)) + kind + data + struct.pack("!I", zlib.crc32(kind + data))

# A simple coloured subject on a plain background, generated without external
# photos or a model download, also gives segmentation a foreground to select.
size = 256
pixels = bytearray()
for y in range(size):
    pixels.append(0)
    for x in range(size):
        pixels.extend((210, 70, 30) if (x - 128) ** 2 + (y - 128) ** 2 < 70 ** 2 else (30, 90, 170))
(work / "photos/subject.png").write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack("!2I5B", size, size, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))
command = ["/usr/bin/omaraw", "--headless", "--catalog", str(work / "catalog.db")]

def invoke(name, *args):
    result = subprocess.run(command + list(args), env=env, capture_output=True, text=True, timeout=600)
    (work / (name + ".json")).write_text(result.stdout)
    (work / (name + ".log")).write_text(result.stderr)
    assert result.returncode == 0, (name, result.returncode, result.stdout, result.stderr)
    answer = json.loads(result.stdout)
    assert answer.get("ok"), answer
    return answer

invoke("import", "--new-catalog", "import", str(work / "photos"))
photo = invoke("list", "list")["photos"][0]["id"]
state = invoke("inspect", "inspect")
assert state["ai"]["installed"] and state["denoise"]["installed"]
(work / "operations.json").write_text(json.dumps({"photo": photo, "ops": [{"op": "engine.setParam", "args": ["exposure", "exposure", 0.4]}]}))
invoke("apply", "apply", str(work / "operations.json"))
formats = ("jpeg", "tiff", "png", "webp", "avif", "jpegxl", "psd")
for fmt in formats:
    folder = work / fmt
    answer = invoke("export-" + fmt, "export", str(folder), "--format", fmt)
    assert answer["done"] == 1 and answer["failed"] == 0, answer
    files = list(folder.iterdir())
    assert len(files) == 1, files
    decoded = subprocess.check_output(["magick", str(files[0]) + "[0]", "-format", "%w %h %#", "info:"], env=env, text=True)
    assert decoded.split()[:2] == [str(size), str(size)] and len(decoded.split()[2]) == 64, decoded

# Load every bundled model with the package's private interpreter. This also
# exercises native ARM extensions independently of the system Python ABI.
model_check = r'''
import ctypes, hashlib, json
from pathlib import Path
import numpy, onnx, onnxruntime, rawpy, PIL, tifffile
import onnxruntime_ep_webgpu
ctypes.CDLL(onnxruntime_ep_webgpu.get_library_path())
options = onnxruntime.SessionOptions()
options.intra_op_num_threads = 2
options.inter_op_num_threads = 1
count = 0
for group in ('ai', 'denoise'):
    root = Path('/usr/lib/omaraw') / group
    for model in json.loads((root / 'ready.json').read_text())['models']:
        path = root / 'models' / model['name']
        assert path.stat().st_size == model['bytes']
        assert hashlib.file_digest(path.open('rb'), 'sha256').hexdigest() == model['sha256']
        session = onnxruntime.InferenceSession(str(path), sess_options=options, providers=['CPUExecutionProvider'])
        assert session.get_inputs() and session.get_outputs()
        del session
        count += 1
print(json.dumps({'models_loaded': count, 'python': __import__('sys').version}))
'''
result = subprocess.run(["/usr/lib/omaraw/ai-runtime/bin/python", "-I", "-B", "-c", model_check], env=env, capture_output=True, text=True, timeout=300)
(work / "models.log").write_text(result.stderr)
assert result.returncode == 0, result.stderr
models = json.loads(result.stdout)
assert models["models_loaded"] == 5, models
(work / "mask-operations.json").write_text(json.dumps({"photo": photo, "ops": [
    {"op": "ai.start", "args": ["mask"]}, {"op": "ai.point", "args": [0.5, 0.5]}, {"op": "ai.acceptMask"}]}))
invoke("mask", "apply", str(work / "mask-operations.json"))
assert invoke("mask-inspect", "inspect", "--photo", str(photo))["develop"]["locals"]
with (work / "gui.log").open("w") as log:
    gui = subprocess.Popen(["/usr/bin/omaraw", "--catalog", str(work / "catalog.db")], env=env, stdout=log, stderr=log)
    try:
        time.sleep(6)
        assert gui.poll() is None, "GUI exited before loading"
    finally:
        gui.terminate()
        try:
            gui.wait(timeout=10)
        except subprocess.TimeoutExpired:
            gui.kill()
            gui.wait()
assert not re.search(r"qrc:/\S+\.qml:\d+|Could not load the OmaRAW interface", (work / "gui.log").read_text())
receipt = {"ok": True, "architecture": platform.machine(), "elf_files": elf_count,
           "arm_16k_load_alignment": machine == 183, "exports_fully_decoded": list(formats),
           "models": models, "editable_ai_selection": True, "gui_startup": True,
           "hardware_gpu_test": "not exercised on a hosted runner"}
(work / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
print(json.dumps(receipt, indent=2))
