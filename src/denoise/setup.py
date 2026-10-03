"""Install the optional denoise runtime independently of selection/removal."""
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


def convert(source, destination, fixed_tile=False):
    import numpy as np
    import onnx
    from onnx import TensorProto, numpy_helper

    def tensor(t):
        if t.data_type == TensorProto.FLOAT16:
            t.CopyFrom(numpy_helper.from_array(numpy_helper.to_array(t).astype(np.float32), t.name))

    def graph(g):
        for t in g.initializer:
            tensor(t)
        for v in list(g.input) + list(g.output) + list(g.value_info):
            if v.type.HasField("tensor_type") and v.type.tensor_type.elem_type == TensorProto.FLOAT16:
                v.type.tensor_type.elem_type = TensorProto.FLOAT
        for node in g.node:
            for attr in node.attribute:
                if node.op_type == "Cast" and attr.name == "to" and attr.i == TensorProto.FLOAT16:
                    attr.i = TensorProto.FLOAT
                if attr.HasField("t"):
                    tensor(attr.t)
                for t in attr.tensors:
                    tensor(t)
                if attr.HasField("g"):
                    graph(attr.g)
                for nested in attr.graphs:
                    graph(nested)
    model = onnx.load(source)
    graph(model.graph)
    if fixed_tile:
        for value in (model.graph.input[0], model.graph.output[0]):
            for dim, size in zip(value.type.tensor_type.shape.dim, (1, 3, 256, 256)):
                dim.ClearField("dim_param")
                dim.dim_value = size
        model = onnx.shape_inference.infer_shapes(model)
    onnx.checker.check_model(model)
    onnx.save(model, destination)


def install(home):
    home.mkdir(parents=True, exist_ok=True)
    with (home / "install.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        (home / "ready.json").unlink(missing_ok=True)
        manifest = json.loads(Path(__file__).with_name("models.json").read_text())
        emit("Preparing local AI denoise…")
        venv.EnvBuilder(with_pip=True, symlinks=True).create(home / "venv")
        python = str(home / "venv/bin/python")
        subprocess.run([python, "-m", "pip", "install", "--disable-pip-version-check",
                        "--only-binary=:all:", *manifest["packages"]], stdout=sys.stderr, check=True)
        (home / "models").mkdir(exist_ok=True)
        for entry in manifest["models"]:
            target = home / "models" / entry["name"]
            if valid(target, entry):
                continue
            source = entry["source"]
            download = home / "models" / source["name"]
            if not valid(download, source):
                emit(f"Downloading {entry.get('label', 'denoise model')} ({round(source['bytes']/1_000_000)} MB)…")
                part = download.with_suffix(".part")
                try:
                    with urllib.request.urlopen(source["url"], timeout=60) as response, part.open("wb") as out:
                        size = 0
                        while block := response.read(1024 * 1024):
                            size += len(block)
                            if size > source["bytes"]:
                                raise ValueError("Unexpected model size")
                            out.write(block)
                    if not valid(part, source):
                        raise ValueError("Denoise model verification failed")
                    os.replace(part, download)
                finally:
                    part.unlink(missing_ok=True)
            emit("Preparing a portable float32 model…")
            part = target.with_suffix(".part")
            try:
                mode = "--convert-fixed" if entry.get("fixedTile") else "--convert"
                subprocess.run([python, __file__, mode, str(download), str(part)], check=True)
                if not valid(part, entry):
                    raise ValueError("Converted model verification failed")
                os.replace(part, target)
            finally:
                part.unlink(missing_ok=True)
        part = home / "ready.part"
        part.write_text(json.dumps(manifest))
        os.replace(part, home / "ready.json")
        print(json.dumps({"ok": True}), flush=True)


if __name__ == "__main__":
    try:
        if sys.argv[1] in ("--convert", "--convert-fixed"):
            convert(sys.argv[2], sys.argv[3], fixed_tile=sys.argv[1] == "--convert-fixed")
        else:
            install(Path(sys.argv[1]).resolve())
    except Exception as error:
        print(json.dumps({"ok": False, "error": str(error)}), flush=True)
        sys.exit(1)
