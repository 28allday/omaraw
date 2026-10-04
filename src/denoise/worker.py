"""Offline Bayer/X-Trans denoise. Automatic CFA routing, float32 ONNX, linear DNG.

The CFA transform follows RawHandler (MIT, rymuelle). Malvar filters are
adapted from colour-demosaicing (BSD-3-Clause); see LICENSES.txt. OmaRAW adds
phase-preserving borders, streaming overlap, residual blending, headroom,
backend qualification and a separate, un-white-balanced linear DNG writer.
"""
import argparse
import fcntl
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

import numpy as np
import onnxruntime as ort
from PIL import Image, ImageCms
import rawpy
import tifffile

TILE, STEP = 256, 128
MAX_PIXELS = 110_000_000
XYZ_TO_2020 = np.array([[1.71666343, -.35567332, -.25336809],
                        [-.66667384, 1.61645574, .0157683],
                        [.01764248, -.04277698, .94224328]], np.float32)
XYZ_TO_SRGB = np.array([[3.2404542, -1.5371385, -.4985314],
                        [-.969266, 1.8760108, .041556],
                        [.0556434, -.2040259, 1.0572252]], np.float32)


def emit(**message):
    print(json.dumps(message), flush=True)


class ModelError(ValueError):
    """Missing or corrupt model weights require the local installer."""


def malvar(cfa):
    """RGGB Malvar 2004, unbounded float32. Only small haloed tiles enter."""
    green = np.array([[0, 0, -1, 0, 0], [0, 0, 2, 0, 0], [-1, 2, 4, 2, -1],
                      [0, 0, 2, 0, 0], [0, 0, -1, 0, 0]], np.float32) / 8
    cross = np.array([[0, 0, .5, 0, 0], [0, -1, 0, -1, 0], [-1, 4, 5, 4, -1],
                      [0, -1, 0, -1, 0], [0, 0, .5, 0, 0]], np.float32) / 8
    opposite = np.array([[0, 0, -1.5, 0, 0], [0, 2, 0, 2, 0], [-1.5, 0, 6, 0, -1.5],
                         [0, 2, 0, 2, 0], [0, 0, -1.5, 0, 0]], np.float32) / 8
    h, w = cfa.shape
    pad = np.pad(cfa, 2, mode="symmetric")

    def convolve(kernel):
        out = np.zeros_like(cfa)
        for y, x in zip(*np.nonzero(kernel)):
            out += kernel[y, x] * pad[y:y + h, x:x + w]
        return out
    g, horizontal, vertical, diagonal = [convolve(k) for k in (green, cross, cross.T, opposite)]
    out = np.empty((h, w, 3), np.float32)
    out[..., 1] = g
    out[0::2, 1::2, 1] = cfa[0::2, 1::2]
    out[1::2, 0::2, 1] = cfa[1::2, 0::2]
    for channel, parity in ((0, 0), (2, 1)):
        out[..., channel] = diagonal
        out[parity::2, parity::2, channel] = cfa[parity::2, parity::2]
        out[parity::2, 1-parity::2, channel] = horizontal[parity::2, 1-parity::2]
        out[1-parity::2, parity::2, channel] = vertical[1-parity::2, parity::2]
    return out


def reflect_indices(start, length, count):
    # Whole-sample reflection preserves CFA parity (unlike symmetric padding).
    indices = np.arange(start, start + length) % (2 * count - 2)
    return np.minimum(indices, 2 * count - 2 - indices)


def sensor_kind(pattern, num_colors, color_desc):
    """Identify the actual CFA, not the filename or camera manufacturer."""
    if pattern is None or num_colors != 3 or color_desc != b"RGBG":
        raise ValueError("AI denoise supports Bayer and X-Trans RAW photos, not monochrome or already-developed images.")
    colours = np.array(list(color_desc))[pattern]
    if pattern.shape == (2, 2) and sorted(colours.ravel().tolist()) == sorted(b"RGGB"):
        # Real Bayer has red and blue on opposite corners, not adjacent.
        red = np.argwhere(colours == ord("R"))[0]
        blue = np.argwhere(colours == ord("B"))[0]
        if np.all(red != blue):
            return "bayer"
    if pattern.shape == (6, 6):
        reference = np.array([list(row) for row in (b"GGRGGB", b"GGBGGR", b"BRGRBG", b"GGBGGR", b"GGRGGB", b"RBGBRG")])
        for rotation in range(4):
            for y in range(6):
                for x in range(6):
                    if np.array_equal(colours, np.roll(np.rot90(reference, rotation), (y, x), axis=(0, 1))):
                        return "xtrans"
    raise ValueError("This RAW colour-filter pattern is not supported by AI denoise.")


def model_entry(sensor):
    manifest = json.loads(Path(__file__).with_name("models.json").read_text())
    return next(e for e in manifest["models"] if e["sensor"] == sensor)


class Sensor:
    def __init__(self, path, camera_database=None, make="", model=""):
        self.raw = rawpy.imread(str(path))
        try:
            self.prepare(path, camera_database, make, model)
        except BaseException:
            self.raw.close()
            raise

    def prepare(self, path, camera_database, make, model):
        raw = self.raw
        pattern = raw.raw_pattern
        self.kind = sensor_kind(pattern, raw.num_colors, raw.color_desc)
        if raw.raw_image_visible.ndim != 2:
            raise ValueError("AI denoise needs Bayer or X-Trans sensor data, not an already-developed image.")
        self.h, self.w = raw.raw_image_visible.shape
        self.mosaic = raw.raw_image_visible
        self.calibration_tags = {}
        if abs(raw.sizes.pixel_aspect - 1) > 1e-6 or (raw.sizes.iheight, raw.sizes.iwidth) != (self.h, self.w):
            raise ValueError("This sensor needs a geometric reconstruction that AI denoise does not yet support.")
        if min(self.h, self.w) < 16 or self.h * self.w > MAX_PIXELS:
            raise ValueError("AI denoise supports RAW photos from 16 pixels to 110 megapixels.")
        # DNG opcode processing belongs to the RAW decoder. LibRaw does not
        # implement all of it; do not silently lose lens shading/calibration.
        if Path(path).suffix.lower() == ".dng":
            with tifffile.TiffFile(path) as dng:
                for page in dng.pages:
                    pages = [page] + (list(page.pages) if page.pages else [])
                    for p in pages:
                        if any(tag in p.tags for tag in (51008, 51009, 51022, 52525, 52544)):
                            raise ValueError("This DNG uses calibration maps/opcodes that AI denoise does not yet preserve. Use the original camera RAW.")
                        repeat = p.tags[50713].value if 50713 in p.tags else (1, 1)
                        if any(key in p.tags for key in (50715, 50716)) or any(v > 2 for v in repeat):
                            raise ValueError("This DNG uses spatial black-level calibration that AI denoise does not yet preserve.")
                        # The output is still in this camera's unbalanced RGB.
                        # Preserve supplied camera calibration and embedded
                        # profile metadata instead of replacing it with LibRaw's
                        # fallback camera table. Never copy CFA/storage tags.
                        for key in (50721, 50722, 50723, 50724, 50725, 50726, 50727,
                                    50728, 50729, 50730, 50778, 50779, 50931, 50932,
                                    50936, 50937, 50938, 50939, 50940, 50941, 50964,
                                    50965, 50981, 50982, 51107, 51108, 51109, 51110):
                            if key in p.tags:
                                tag = p.tags[key]
                                self.calibration_tags[key] = (key, int(tag.dtype), tag.count, tag.value, False)
        if self.kind == "bayer":
            colours = np.array(list(raw.color_desc))[pattern]
            self.ry, self.rx = map(int, np.argwhere(colours == ord("R"))[0])
        self.pattern = pattern
        self.xyz_to_camera = np.asarray(raw.rgb_xyz_matrix[:3], np.float64)
        if not np.any(self.xyz_to_camera):
            # Cameras without a LibRaw table entry may have their own D65 DNG matrix.
            for matrix, light in ((50722, 50779), (50721, 50778)):
                if matrix in self.calibration_tags and self.calibration_tags.get(light, (0, 0, 0, 0))[3] == 21:
                    values = np.asarray(self.calibration_tags[matrix][3]).reshape(-1, 2)
                    self.xyz_to_camera = (values[:, 0] / values[:, 1]).reshape(3, 3)
                    break
        if not np.any(self.xyz_to_camera) and camera_database and Path(path).suffix.lower() != ".dng":
            # Use the app's existing RawSpeed camera database for newer bodies
            # missing from the Python LibRaw wheel (e.g. X-E5). Exact identity
            # only: never substitute a related camera's calibration.
            for camera in ET.parse(camera_database).getroot().findall("Camera"):
                if camera.get("make", "").casefold() == make.strip().casefold() and camera.get("model", "").casefold() == model.strip().casefold():
                    matrix = camera.find("ColorMatrices/ColorMatrix[@planes='3']")
                    if matrix is not None:
                        values = {int(row.get("plane")): [float(v)/10000 for v in row.text.split()] for row in matrix.findall("ColorMatrixRow")}
                        self.xyz_to_camera = np.asarray([values[i] for i in range(3)], np.float64)
                        break
        if not np.isfinite(self.xyz_to_camera).all() or np.linalg.cond(self.xyz_to_camera) > 100:
            raise ValueError("The camera colour calibration is missing or unsuitable for AI denoise.")
        self.to_model = (XYZ_TO_2020 @ np.linalg.inv(self.xyz_to_camera)).astype(np.float32)
        self.to_camera = np.linalg.inv(self.to_model).astype(np.float32)
        self.output_matrix = self.xyz_to_camera
        self.output_gain = np.ones(3, np.float32)
        self.highlight_bias = None
        self.output_geometry = None
        wb = np.asarray(raw.camera_whitebalance[:3], np.float64)
        if not np.isfinite(wb).all() or min(wb) <= 0:
            raise ValueError("Camera white balance is missing. This RAW is not supported yet.")
        self.neutral = wb[1] / wb
        self.orientation = {0: 1, 1: 2, 2: 4, 3: 3, 4: 5, 5: 8, 6: 6, 7: 7}.get(raw.sizes.flip, 1)
        self.rgb = None
        if self.kind == "xtrans":
            emit(event="progress", message="Preparing X-Trans sensor data…")
            # Match RawForge's X-Trans training domain: unbalanced camera RGB,
            # LibRaw interpolation, no tone/gamma/auto exposure, divided by the
            # sensor white level. This is deliberately separate from Bayer's
            # packed-CFA Rec.2020 transform. Keep the full demosaic uint16.
            self.white = float(raw.white_level)
            if not math.isfinite(self.white) or self.white <= max(raw.black_level_per_channel):
                raise ValueError("Invalid sensor black/white levels.")
            self.rgb = raw.postprocess(user_wb=[1, 1, 1, 1], output_color=rawpy.ColorSpace.raw,
                                       demosaic_algorithm=rawpy.DemosaicAlgorithm(3), no_auto_bright=True,
                                       use_camera_wb=False, use_auto_wb=False, gamma=(1, 1),
                                       user_flip=0, output_bps=16, no_auto_scale=True)
            if self.rgb.shape != (self.h, self.w, 3):
                raise ValueError("Unexpected X-Trans reconstruction dimensions.")
            self.to_model = self.to_camera = np.eye(3, dtype=np.float32)

    def configure_output(self, calibration):
        """Keep inference in its training domain; match the editor at output.

        LibRaw's ADC maximum can differ from the camera saturation used by the
        editor (e.g. R5 C: 16383 versus 14008). Using it as the DNG's white point
        darkens the image and changes every subsequent tone-dependent effect.
        """
        if not calibration:
            return
        matrix = np.asarray(calibration.get("matrix"), np.float64)
        white = float(calibration.get("white", 0))
        highlight_bias = float(calibration.get("highlightBias", 0))
        black = np.asarray(self.raw.black_level_per_channel, np.float64)
        if (matrix.size != 9 or not np.isfinite(matrix).all()
                or not math.isfinite(white) or not max(black) < white <= 65535
                or not math.isfinite(highlight_bias) or not 0 <= highlight_bias <= 4):
            raise ValueError("Invalid source camera calibration for AI denoise.")
        matrix = matrix.reshape(3, 3)
        if np.linalg.cond(matrix) > 100:
            raise ValueError("Invalid source camera matrix for AI denoise.")
        numerator = self.raw.white_level - black if self.kind == "bayer" else np.full(4, self.white)
        gains = numerator / (white - black)
        # LibRaw numbers the two Bayer greens 1 and 3; the linear copy has
        # one green plane. Most sensors give them the same black calibration.
        green = (gains[1] + gains[3]) / 2 if self.kind == "bayer" else gains[1]
        self.output_gain = np.array([gains[0], green, gains[2]], np.float32)
        self.output_matrix = matrix
        self.highlight_bias = highlight_bias
        geometry = calibration.get("geometry")
        if geometry is not None:
            if (len(geometry) != 6 or any(not isinstance(v, int) for v in geometry)):
                raise ValueError("Invalid source sensor geometry for AI denoise.")
            w, h, left, top, right, bottom = geometry
            size = self.raw.sizes
            # Keep the native sensor canvas: mask/retouch coordinates refer to
            # that canvas even though the editor displays its cropped area.
            if (w != size.raw_width or h != size.raw_height or min(left, top, right, bottom) < 0
                    or left < size.left_margin or top < size.top_margin
                    or w-left-right < 1 or h-top-bottom < 1):
                raise ValueError("The editor and denoise decoder disagree on the active sensor area.")
            needed_w = max(self.w, w-right-size.left_margin)
            needed_h = max(self.h, h-bottom-size.top_margin)
            if needed_w != self.w or needed_h != self.h:
                # LibRaw's recommended visible crop can be narrower than the
                # editor's active area (Sony ZV-E1: eight columns and two rows).
                # Bayer decoding still supplies those samples on the identical
                # full sensor canvas. Include them instead of rejecting the
                # photograph or padding real image pixels with black. Keep the
                # visible origin, so CFA phase and mask coordinates are unchanged.
                if (self.kind != "bayer" or self.raw.raw_image.shape != (h, w)
                        or needed_w * needed_h > MAX_PIXELS):
                    raise ValueError("The editor and denoise decoder disagree on the active sensor area.")
                self.mosaic = self.raw.raw_image[size.top_margin:size.top_margin+needed_h,
                                                 size.left_margin:size.left_margin+needed_w]
                self.h, self.w = self.mosaic.shape
            self.output_geometry = geometry

    def tile(self, y, x, height=TILE, width=TILE):
        if self.kind == "xtrans":
            iy, ix = reflect_indices(y, height, self.h), reflect_indices(x, width, self.w)
            camera = self.rgb[np.ix_(iy, ix)].astype(np.float32) / self.white
            return camera, camera
        # Align the halo to the red sample without removing any source pixels.
        y0 = y - 4 - ((y - self.ry) % 2)
        x0 = x - 4 - ((x - self.rx) % 2)
        hh = (height + y - y0 + 5) // 2 * 2
        ww = (width + x - x0 + 5) // 2 * 2
        iy, ix = reflect_indices(y0, hh, self.h), reflect_indices(x0, ww, self.w)
        cfa = self.mosaic[np.ix_(iy, ix)].astype(np.float32)
        channels = self.pattern[np.ix_(iy % 2, ix % 2)]
        black = np.asarray(self.raw.black_level_per_channel, np.float32)[channels]
        white = float(self.raw.white_level)
        if white <= black.max():
            raise ValueError("Invalid sensor black/white levels.")
        cfa = (cfa - black) / (white - black)
        camera = malvar(cfa)
        packed = np.stack([cfa[0::2, 0::2], cfa[0::2, 1::2], cfa[1::2, 0::2], cfa[1::2, 1::2]], axis=-1)
        t = self.to_model
        matrix = np.array([[t[0, 0], t[0, 1]/2, t[0, 1]/2, t[0, 2]],
                           [t[1, 0], t[1, 1], 0, t[1, 2]],
                           [t[1, 0], 0, t[1, 1], t[1, 2]],
                           [t[2, 0], t[2, 1]/2, t[2, 1]/2, t[2, 2]]], np.float32)
        packed = packed @ matrix.T
        for index, (row, col) in enumerate(((0, 0), (0, 1), (1, 0), (1, 1))):
            cfa[row::2, col::2] = packed[..., index]
        model = malvar(cfa)
        dy, dx = y-y0, x-x0
        return camera[dy:dy+height, dx:dx+width], model[dy:dy+height, dx:dx+width]

    def preview(self, camera):
        # A neutral technical preview, before the user's creative edits.
        matrix = XYZ_TO_SRGB @ np.linalg.inv(self.output_matrix)
        matrix /= matrix.sum(axis=1)[:, None]
        linear = np.maximum((camera / self.neutral) @ matrix.T, 0)
        linear = np.clip(linear, 0, 1)
        srgb = np.where(linear <= .0031308, 12.92*linear, 1.055*linear**(1/2.4)-.055)
        im = Image.fromarray(np.rint(srgb*255).astype(np.uint8))
        transpose = {2: Image.Transpose.FLIP_LEFT_RIGHT, 3: Image.Transpose.ROTATE_180,
                     4: Image.Transpose.FLIP_TOP_BOTTOM, 5: Image.Transpose.TRANSPOSE,
                     6: Image.Transpose.ROTATE_270, 7: Image.Transpose.TRANSVERSE, 8: Image.Transpose.ROTATE_90}
        return im.transpose(transpose[self.orientation]) if self.orientation in transpose else im


class Inference:
    def __init__(self, home, backend, sample, iso, directory, sensor="bayer"):
        entry = model_entry(sensor)
        self.name = entry["label"]
        self.conditioned = entry["isoConditioned"]
        model = Path(home) / "models" / entry["name"]
        valid = False
        try:
            if model.stat().st_size == entry["bytes"]:
                with model.open("rb") as stream:
                    valid = hashlib.file_digest(stream, "sha256").hexdigest() == entry["sha256"]
        except OSError:
            pass
        if not valid:
            raise ModelError("An included denoise model is missing or damaged. Reinstall the OmaRAW package.")
        self.condition = np.array([[min(iso, 65535) / 6400]], np.float32)
        options = ort.SessionOptions()
        options.intra_op_num_threads = min(6, os.cpu_count() or 1)
        options.log_severity_level = 3
        self.cpu = ort.InferenceSession(str(model), sess_options=options, providers=["CPUExecutionProvider"])
        self.session, self.notice = self.cpu, "CPU"
        if backend == "cpu":
            return
        emit(event="progress", message="Checking GPU accuracy and speed…")
        try:
            reference = self.run_with(self.cpu, sample)
            times = []
            for _ in range(2):
                start = time.monotonic(); self.run_with(self.cpu, sample); times.append(time.monotonic()-start)
            cpu_time = min(times)
            # ORT's Linux hardware discovery misses Apple's platform GPU.
            # This enables EP discovery; Dawn still chooses the Vulkan adapter.
            if sys.platform == "linux" and os.uname().machine == "aarch64":
                try:
                    if b"apple," in Path("/proc/device-tree/compatible").read_bytes():
                        os.environ.setdefault("ORT_WEBGPU_EP_ALLOW_SOFTWARE_ADAPTER", "1")
                except OSError:
                    pass
            import onnxruntime_ep_webgpu as gpu
            ort.register_execution_provider_library("webgpu", gpu.get_library_path())
            device = next(d for d in ort.get_ep_devices() if d.ep_name == gpu.get_ep_name())
            options.add_provider_for_devices([device], {"ep.webgpuexecutionprovider.dawnBackendType": "Vulkan"})
            options.enable_profiling = True
            options.profile_file_prefix = str(Path(directory) / "gpu-profile")
            session = ort.InferenceSession(str(model), sess_options=options, providers=None)
            candidate = self.run_with(session, sample)
            # Even a black photograph must detect a broken all-zero GPU kernel.
            ramp = np.linspace(.05, .7, TILE*TILE*3, dtype=np.float32).reshape(TILE, TILE, 3)
            probe_reference = self.run_with(self.cpu, ramp)
            probe_candidate = self.run_with(session, ramp)
            times = []
            for _ in range(2):
                start = time.monotonic(); self.run_with(session, sample); times.append(time.monotonic()-start)
            profile = Path(session.end_profiling())
            events = json.loads(profile.read_text()); profile.unlink(missing_ok=True)
            accelerated = any(e.get("args", {}).get("op_name") == "Conv" and "WebGpu" in e.get("args", {}).get("provider", "").replace("WebGPU", "WebGpu") for e in events)
            if not np.allclose(reference, candidate, rtol=.002, atol=.0001) or not np.allclose(probe_reference, probe_candidate, rtol=.002, atol=.0001):
                self.notice = "CPU (GPU accuracy check failed)"
            elif not accelerated:
                self.notice = "CPU (GPU did not accelerate the model)"
            elif min(times) >= cpu_time * .9:
                self.notice = "CPU (faster than this GPU)"
            else:
                self.session, self.notice = session, "Vulkan (verified against CPU)"
        except Exception:
            self.notice = "CPU (Vulkan unavailable)"

    def run_with(self, session, pixels):
        pixels = np.clip(pixels, 0, 1)
        feeds = {"input": pixels.transpose(2, 0, 1)[None].copy()}
        if self.conditioned:
            feeds["cond"] = self.condition
        result = session.run(None, feeds)[0]
        if result.shape != (1, 3, TILE, TILE) or not np.isfinite(result).all():
            raise ValueError("AI denoise returned invalid pixels.")
        return result[0].transpose(1, 2, 0)

    def run(self, pixels):
        try:
            return self.run_with(self.session, pixels)
        except Exception:
            if self.session is self.cpu:
                raise
            self.session, self.notice = self.cpu, "CPU (GPU processing failed)"
            emit(event="progress", message="Continuing denoise on CPU…")
            return self.run_with(self.cpu, pixels)


def blended(camera, model_input, predicted, strength):
    # Add only the denoising residual to the normal camera-space demosaic.
    # Model clipping must not clip sensor highlights or negative interpolation.
    protect = np.clip((1 - model_input.max(axis=-1)) / .1, 0, 1)[..., None]
    return camera, (predicted - np.clip(model_input, 0, 1)) * protect * strength


def rows(sensor, inference, strength, region, progress=True):
    """Overlap-add one rolling strip, O(width * tile) memory, no tile list."""
    top, left, height, width = region
    window = np.sin(np.pi * (np.arange(TILE, dtype=np.float32) + .5) / TILE)**2
    wx = np.zeros(width, np.float32)
    for tx in range(-STEP, width, STEP):
        x0, x1 = max(tx, 0), min(tx+TILE, width)
        wx[x0:x1] += window[x0-tx:x1-tx]
    buffer = np.zeros((TILE, width, 3), np.float32)
    weight = np.zeros(TILE, np.float32)
    last_progress = time.monotonic()
    completed = 0
    total = len(range(-STEP, height, STEP)) * len(range(-STEP, width, STEP))
    for ty in range(-STEP, height, STEP):
        origin = max(0, ty)
        y0, y1 = max(0, ty), min(ty+TILE, height)
        sy, ey = y0-ty, y1-ty
        wy = window[sy:ey]
        for tx in range(-STEP, width, STEP):
            camera, model = sensor.tile(top+ty, left+tx)
            if strength:
                camera, residual = blended(camera, model, inference.run(model), strength)
                camera = camera + residual @ sensor.to_camera.T
            camera = camera * sensor.output_gain
            x0, x1 = max(tx, 0), min(tx+TILE, width)
            sx, ex = x0-tx, x1-tx
            buffer[y0-origin:y1-origin, x0:x1] += camera[sy:ey, sx:ex] * wy[:, None, None] * window[None, sx:ex, None]
            completed += 1
            if progress and time.monotonic() - last_progress >= 5:
                emit(event="progress", progress=completed/total, message=f"Denoising… {round(100*completed/total)}%")
                last_progress = time.monotonic()
        weight[y0-origin:y1-origin] += wy
        if ty < 0:
            continue
        count = min(STEP, height-ty)
        yield (buffer[:count] / (weight[:count, None, None] * wx[None, :, None])).copy()
        buffer[:STEP] = buffer[STEP:]; buffer[STEP:] = 0
        weight[:STEP] = weight[STEP:]; weight[STEP:] = 0
        if progress:
            emit(event="progress", progress=completed/total, message=f"Denoising… {round(100*completed/total)}%")


def rational(values, signed=False):
    values = np.asarray(values).ravel()
    return tuple(v for x in values for v in (int(round(float(x)*1_000_000)), 1_000_000))


def write_dng(path, sensor, strips, request):
    def tag(code, dtype, count, value):
        return code, dtype, count, value, False
    tags = [tag(50706, "B", 4, (1, 4, 0, 0)), tag(50707, "B", 4, (1, 4, 0, 0)),
            tag(50708, "s", 0, (request.get("make", "") + " " + request.get("model", "")).strip() or "OmaRAW linear camera"),
            tag(271, "s", 0, request.get("make", "")), tag(272, "s", 0, request.get("model", "")),
            tag(274, "H", 1, sensor.orientation), tag(50714, "2I", 1, (0, 1)),
            tag(50717, "I", 1, 1), tag(50718, "2I", 2, (1, 1, 1, 1)),
            tag(50719, "I", 2, (0, 0)), tag(50720, "I", 2, (sensor.w, sensor.h)),
            tag(50721, "2i", 9, rational(sensor.output_matrix, True)),
            tag(50728, "2I", 3, rational(sensor.neutral)), tag(50778, "H", 1, 21),
            tag(50827, "s", 0, Path(request["image"]).name)]
    tags = [t for t in tags if t[0] not in sensor.calibration_tags] + list(sensor.calibration_tags.values())
    if sensor.highlight_bias is not None:
        # Camera maker-note tone modes affect the editor's exposure stage.
        # Record their compensation in the DNG instead of transplanting maker
        # notes with unsafe RAW offsets. OmaRAW reads it at the same stage.
        tags = [t for t in tags if t[0] != 50730]
        tags.append(tag(50730, "2i", 1, rational([sensor.highlight_bias], True)))
    if 50729 in sensor.calibration_tags and 50728 not in sensor.calibration_tags:
        tags = [t for t in tags if t[0] != 50728]  # AsShotWhiteXY excludes AsShotNeutral.
    width, height = sensor.w, sensor.h
    if request.get("action") == "save" and sensor.output_geometry is not None:
        width, height, left, top, right, bottom = sensor.output_geometry
        tags = [t for t in tags if t[0] not in (50719, 50720, 50829)]
        tags += [tag(50829, "I", 4, (top, left, height-bottom, width-right)),
                 tag(50719, "I", 2, (0, 0)), tag(50720, "I", 2, (width-left-right, height-top-bottom))]
        strips = sensor_canvas(strips, sensor, width, height)
    tifffile.imwrite(path, data=strips, shape=(height, width, 3), dtype=np.float32,
                     photometric="rgb", planarconfig="contig", rowsperstrip=STEP, metadata=None,
                     software="OmaRAW AI denoise (RawForge float32)", extratags=tags)
    # tifffile treats unknown photometrics as one colour plus extra samples.
    # Write three colour planes, then replace the same-sized SHORT tag in place.
    with tifffile.TiffFile(path, mode="r+") as dng:
        dng.pages[0].tags[262].overwrite(34892)


def sensor_canvas(strips, sensor, width, height):
    """Pad masked sensor borders without retaining a second full image."""
    top, left = sensor.raw.sizes.top_margin, sensor.raw.sizes.left_margin
    rows_written = 0
    buffer = np.zeros((STEP, width, 3), np.float32)
    used = 0
    count = top
    while count:
        n = min(count, STEP-used); used += n; count -= n
        if used == STEP:
            yield buffer.copy(); rows_written += STEP; buffer.fill(0); used = 0
    for pixels in strips:
        start = 0
        while start < len(pixels):
            n = min(len(pixels)-start, STEP-used)
            buffer[used:used+n, left:left+sensor.w] = pixels[start:start+n]
            used += n; start += n
            if used == STEP:
                yield buffer.copy(); rows_written += STEP; buffer.fill(0); used = 0
    while rows_written + used < height:
        n = min(height-rows_written-used, STEP-used); used += n
        if used == STEP:
            yield buffer.copy(); rows_written += STEP; buffer.fill(0); used = 0
    if used:
        yield buffer[:used].copy()


def process(home, request):
    strength = float(request.get("strength", .6))
    iso = float(request.get("iso", 0))
    if not math.isfinite(strength) or not 0 <= strength <= 1:
        raise ValueError("Denoise strength must be between 0 and 1.")
    if not math.isfinite(iso) or not 0 <= iso <= 10_000_000:
        raise ValueError("ISO must be zero (from photo) or a positive value up to 10000000.")
    if request.get("backend", "auto") not in ("auto", "cpu"):
        raise ValueError("Denoise backend must be auto or cpu.")
    directory = Path(request["directory"])
    directory.mkdir(parents=True, exist_ok=True)
    emit(event="progress", message="Reading the original sensor data…")
    sensor = Sensor(request["image"], request.get("cameraDatabase"), request.get("make", ""), request.get("model", ""))
    try:
        sensor.configure_output(request.get("calibration"))
        model = model_entry(sensor.kind)
        emit(event="sensor", sensor=sensor.kind, model=model["label"])
        if sensor.kind == "bayer" and iso < 1:
            raise ValueError("This Bayer photo has no usable ISO. Set the ISO override for AI denoise.")
        x, y = float(request.get("x", .5)), float(request.get("y", .5))
        if not all(math.isfinite(v) and 0 <= v <= 1 for v in (x, y)):
            raise ValueError("Preview position must be between 0 and 1.")
        height, width = min(512, sensor.h), min(512, sensor.w)
        top = max(0, min(int(y*sensor.h)-height//2, sensor.h-height))
        left = max(0, min(int(x*sensor.w)-width//2, sensor.w-width))
        # Keep inference on the full-frame grid, then extract the precise
        # requested area. Dragging must not jump in STEP-pixel increments.
        aligned_top, aligned_left = top - top % STEP, left - left % STEP
        dy, dx = top - aligned_top, left - aligned_left
        _, sample = sensor.tile(aligned_top, aligned_left)
        inference = Inference(home, request.get("backend", "auto"), sample, iso, directory, sensor.kind) if strength else None
        if request["action"] == "preview":
            region = (aligned_top, aligned_left, height + dy, width + dx)
            before = np.concatenate(list(rows(sensor, None, 0, region, False)))[dy:dy+height, dx:dx+width]
            after = np.concatenate(list(rows(sensor, inference, strength, region)))[dy:dy+height, dx:dx+width]
            icc = ImageCms.ImageCmsProfile(ImageCms.createProfile("sRGB")).tobytes()
            sensor.preview(before).save(directory / "before.png", icc_profile=icc)
            sensor.preview(after).save(directory / "after.png", icc_profile=icc)
            if request.get("developPreview"):
                # Preserve unclipped camera-linear pixels for the application's
                # normal develop engine. Display edits never enter inference.
                crop = copy.copy(sensor)
                crop.h, crop.w = height, width
                for name, pixels in (("before", before), ("after", after)):
                    write_dng(directory / (name + ".dng"), crop,
                              (pixels[i:i+STEP] for i in range(0, height, STEP)), request)
            result = {"before": str(directory / "before.png"), "after": str(directory / "after.png"),
                      "width": width, "height": height, "left": left, "top": top,
                      "sensorWidth": sensor.w, "sensorHeight": sensor.h, "orientation": sensor.orientation}
        elif request["action"] == "save":
            path = directory / "denoised.dng"
            write_dng(path, sensor, rows(sensor, inference, strength, (0, 0, sensor.h, sensor.w)), request)
            result = {"path": str(path), "width": sensor.w, "height": sensor.h}
        else:
            raise ValueError("Unknown denoise operation.")
        result.update(backend=inference.notice if inference else "CPU (strength zero)", iso=iso,
                      sensor=sensor.kind, model=model["label"])
        emit(ok=True, **result)
    finally:
        sensor.raw.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--home", required=True)
    parser.add_argument("--request", required=True)
    args = parser.parse_args()
    try:
        mode = "rb" if (Path(args.home) / "bundled").is_file() else "a"
        with (Path(args.home) / "install.lock").open(mode) as lock:
            fcntl.flock(lock, fcntl.LOCK_SH | fcntl.LOCK_NB)
            process(args.home, json.loads(Path(args.request).read_text()))
    except ModelError as error:
        emit(ok=False, error=str(error), repair_required=True)
        sys.exit(1)
    except Exception as error:
        emit(ok=False, error=str(error))
        sys.exit(1)
