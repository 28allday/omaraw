"""One serial JSON-line worker, with bounded model inputs and a CPU baseline.

SAM outputs binary selections, not fine hair/transparency mattes. LaMa works
in encoded sRGB8; the rendered source's 16-bit pixels outside the final removal
mask are preserved through a reversible sRGB-to-linear float conversion into
the saved DNG. The worker performs no network operations.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import time
import os
import shutil
import tempfile
import ctypes
import errno
import struct

import numpy as np
import onnxruntime as ort
from PIL import Image, ImageFilter
import tifffile

MAX_PIXELS = 110_000_000
Image.MAX_IMAGE_PIXELS = MAX_PIXELS


def write_rendered_dng(path, pixels):
    """Store the baked rendering as linear RGB, with an explicit DNG profile.

    This is a rendered copy, not recovered camera sensor data. The model still
    receives its trained sRGB input. Float storage avoids quantizing shadows a
    second time when removing the sRGB transfer curve from the 16-bit source.
    """
    height, width = pixels.shape[:2]
    xyz_to_srgb = [3.2404542, -1.5371385, -.4985314,
                   -.9692660, 1.8760108, .0415560,
                   .0556434, -.2040259, 1.0572252]
    matrix = tuple(v for x in xyz_to_srgb for v in (round(x * 10000000), 10000000))
    tag = lambda code, dtype, count, value: (code, dtype, count, value, False)
    tags = [tag(50706, 'B', 4, (1, 4, 0, 0)), tag(50707, 'B', 4, (1, 4, 0, 0)),
            tag(50708, 's', 0, 'OmaRAW rendered sRGB'),
            tag(271, 's', 0, 'OmaRAW'), tag(272, 's', 0, 'Rendered image'), tag(274, 'H', 1, 1),
            tag(50714, '2I', 1, (0, 1)), tag(50717, 'I', 1, 1),
            tag(50718, '2I', 2, (1, 1, 1, 1)), tag(50719, 'I', 2, (0, 0)),
            tag(50720, 'I', 2, (width, height)), tag(50721, '2i', 9, matrix),
            tag(50728, '2I', 3, (1, 1, 1, 1, 1, 1)), tag(50730, '2i', 1, (0, 1)),
            tag(50778, 'H', 1, 21), tag(50936, 's', 0, 'OmaRAW baked appearance'),
            tag(50940, 'f', 4, (0., 0., 1., 1.)), tag(51110, 'I', 1, 1)]
    def strips():
        for row in range(0, height, 128):
            encoded = pixels[row:row+128].astype(np.float32) / 65535
            yield np.where(encoded <= .04045, encoded / 12.92,
                           ((encoded + .055) / 1.055) ** 2.4).astype(np.float32)
    tifffile.imwrite(path, data=strips(), shape=(height, width, 3), dtype=np.float32,
                     photometric='rgb', planarconfig='contig', rowsperstrip=128,
                     metadata=None, software='OmaRAW AI removal', extratags=tags)
    # The library otherwise treats LinearRaw as one plane plus extra samples.
    with tifffile.TiffFile(path, mode='r+') as dng:
        dng.pages[0].tags[262].overwrite(34892)


class ModelError(ValueError):
    """A missing/corrupt model can be repaired by the local installer."""


def removal_masks(selection, margin):
    """Grow in preview coordinates, so coverage scales with export resolution.

    The blend lives in the added margin. Selected object pixels are always
    fully replaced, never blended back into the generated background.
    """
    if not math.isfinite(margin) or not 0 <= margin <= .03:
        raise ValueError("Invalid removal margin")
    selection = selection.convert("L").point(lambda v: 255 if v > 127 else 0)
    grow = int(max(selection.size) * margin + .5)
    hard = selection.filter(ImageFilter.MaxFilter(grow * 2 + 1)) if grow else selection
    if grow:
        # A one-pixel filter is a no-op. Pillow 12.3's expand(0) can
        # divide by zero in native code, killing the worker outright.
        core = selection.filter(ImageFilter.MaxFilter((grow // 2) * 2 + 1)) if grow >= 2 else selection
        alpha = np.maximum(np.array(selection), np.array(core.filter(ImageFilter.GaussianBlur(max(.5, grow / 3)))))
        alpha[np.array(hard) == 0] = 0
        return hard, Image.fromarray(alpha)
    return hard, hard


def context_box(box, size):
    """An undistorted neighbourhood with enough context to rebuild the hole."""
    x0, y0, x1, y1 = box
    width, height = size
    side = int(math.ceil(max(512, max(size) * .32, max(x1 - x0, y1 - y0) * 2.5)))
    left = max(0, min((x0 + x1 - side) // 2, width - side))
    top = max(0, min((y0 + y1 - side) // 2, height - side))
    return left, top, min(width, left + side), min(height, top + side)


def box_mean(values, radius):
    """Constant-cost local mean; double precision avoids cumulative striping."""
    pad = ((radius, radius), (radius, radius)) + ((0, 0),) * (values.ndim - 2)
    integral = np.pad(values, pad, mode="edge").cumsum(0, dtype=np.float64).cumsum(1)
    integral = np.pad(integral, ((1, 0), (1, 0)) + ((0, 0),) * (values.ndim - 2))
    side = 2 * radius + 1
    return ((integral[side:, side:] - integral[:-side, side:]
             - integral[side:, :-side] + integral[:-side, :-side]) / (side * side)).astype(np.float32)


def refine_selection(photo, selection):
    """RGB guided filtering, restricted to a narrow band of the existing edge.

    Colour guidance follows He et al., Guided Image Filtering, ECCV 2010,
    equations 14-16. Keep fractional edge coverage for the zoomed overlay;
    the editable path follows its 50% contour. This is not a hair matte.
    Overlapping strips bound the temporary covariance/solve allocations.
    """
    if photo.size != selection.size or max(photo.size) > 4096:
        raise ValueError("Edge refinement needs a matching photo and selection, at most 4096 pixels per side.")
    coverage = np.array(selection.convert("L"), np.float32) / 255
    selected = coverage >= .5
    if not selected.any():
        raise ValueError("Select an object before refining its edges.")
    radius = max(3, min(32, round(max(photo.size) / 128)))
    neighbours = box_mean(selected, radius)
    band = (neighbours > 0) & (neighbours < 1)
    del neighbours
    rgb = np.asarray(photo.convert("RGB"), np.float32) / 255
    result = coverage.copy()
    height = selected.shape[0]
    for top in range(0, height, 128):
        bottom = min(top + 128, height)
        if not band[top:bottom].any():
            continue
        # Both local averages reach radius pixels, so keep twice that halo.
        start, end = max(0, top - 2 * radius), min(height, bottom + 2 * radius)
        guide = rgb[start:end]
        mask = coverage[start:end]
        mean_rgb = box_mean(guide, radius)
        mean_mask = box_mean(mask, radius)
        covariance = np.empty((*mask.shape, 3, 3), np.float32)
        for i in range(3):
            for j in range(i, 3):
                value = box_mean(guide[..., i] * guide[..., j], radius) - mean_rgb[..., i] * mean_rgb[..., j]
                covariance[..., i, j] = covariance[..., j, i] = value
            covariance[..., i, i] += .001
        cross = box_mean(guide * mask[..., None], radius) - mean_rgb * mean_mask[..., None]
        a = np.linalg.solve(covariance, cross[..., None])[..., 0]
        b = mean_mask - (a * mean_rgb).sum(axis=2)
        guided = (box_mean(a, radius) * guide).sum(axis=2) + box_mean(b, radius)
        if not np.isfinite(guided).all():
            raise ValueError("Edge refinement produced invalid pixels.")
        region = slice(top - start, bottom - start)
        # Without a photographic edge, retain the original instead of merely
        # smoothing the mask or erasing thin painted details on a flat region.
        contrast = np.trace(covariance, axis1=2, axis2=3) - .003
        adjust = band[top:bottom] & (contrast[region] > .0004)
        edge = np.clip((guided[region] - .4) / .2, 0, 1)
        result[top:bottom] = np.where(adjust, edge, coverage[top:bottom])
    # An ambiguous tiny selection must not disappear as a side effect.
    if not (result >= .5).any():
        result = coverage
    return Image.fromarray(np.rint(result * 255).astype(np.uint8))


class Inference:
    def __init__(self, home, backend):
        self.home = Path(home)
        self.backend = backend
        self.sessions = {}
        self.verified = set()
        self.embedding = None
        self.image_key = None
        self.device = None
        self.notice = "CPU"
        self.manifest = json.loads(Path(__file__).with_name("models.json").read_text())
        if backend == "vulkan":
            try:
                import onnxruntime_ep_webgpu as gpu
                ort.register_execution_provider_library("webgpu", gpu.get_library_path())
                self.device = next(d for d in ort.get_ep_devices() if d.ep_name == gpu.get_ep_name())
                # Dawn chooses the adapter; get_ep_devices is not a reliable
                # adapter selector. Do not claim a particular GPU here.
                self.notice = "Vulkan (WebGPU)"
            except Exception as error:
                self.notice = "CPU (Vulkan unavailable: " + str(error) + ")"

    def session(self, name, cpu=False):
        key = (name, bool(cpu or self.device is None))
        if key in self.sessions:
            return self.sessions[key]
        path = self.home / "models" / (name + ".onnx")
        if name not in self.verified:
            entry = next(m for m in self.manifest["models"] if m["name"] == path.name)
            try:
                with path.open("rb") as stream:
                    digest = hashlib.file_digest(stream, "sha256").hexdigest()
                valid = path.stat().st_size == entry["bytes"] and digest == entry["sha256"]
            except OSError:
                valid = False
            if not valid:
                raise ModelError("An included AI model is missing or damaged. Reinstall the OmaRAW package.")
            self.verified.add(name)
        options = ort.SessionOptions()
        options.intra_op_num_threads = 6
        options.log_severity_level = 3
        if not key[1]:
            options.add_provider_for_devices([self.device], {
                "ep.webgpuexecutionprovider.dawnBackendType": "Vulkan"})
        session = ort.InferenceSession(str(path), sess_options=options,
                                      providers=["CPUExecutionProvider"] if key[1] else None)
        self.sessions[key] = session
        return session

    def mask(self, request):
        path = Path(request["image"])
        stat = path.stat()
        key = (str(path.resolve()), stat.st_mtime_ns, stat.st_size)
        points = request.get("points", [])
        box = request.get("box", [])
        if not (1 <= len(points) + bool(box) <= 64):
            raise ValueError("Select an object first (up to 64 prompts).")
        coords, labels = [], []
        for p in points:
            if len(p) != 3 or p[2] not in (0, 1):
                raise ValueError("Invalid selection point")
            coords.append(p[:2]); labels.append(p[2])
        if box:
            if len(box) != 4 or box[0] >= box[2] or box[1] >= box[3]:
                raise ValueError("Draw a box around the object.")
            coords.extend([box[:2], box[2:]]); labels.extend([2, 3])
        if any(not math.isfinite(v) or v < 0 or v > 1 for p in coords for v in p):
            raise ValueError("Selection points must be inside the photo.")
        with Image.open(path) as im:
            if im.width * im.height > 4_000_000:
                raise ValueError("Mask previews must be at most four megapixels.")
            size = im.size
            if key != self.image_key:
                rgb = np.array(im.convert("RGB").resize((1024, 1024), Image.Resampling.BILINEAR), np.float32) / 255
                rgb = (rgb - np.array([.485, .456, .406], np.float32)) / np.array([.229, .224, .225], np.float32)
                self.embedding = self.session("sam2.encoder").run(None, {"image": rgb.transpose(2, 0, 1)[None].copy()})
                self.image_key = key
        feeds = dict(zip(["high_res_feats_0", "high_res_feats_1", "image_embed"], self.embedding))
        feeds.update(point_coords=np.array([coords], np.float32) * 1024,
                     point_labels=np.array([labels], np.float32),
                     mask_input=np.zeros((1, 1, 256, 256), np.float32), has_mask_input=np.zeros(1, np.float32))
        masks, scores = self.session("sam2.decoder").run(None, feeds)
        if not np.isfinite(masks).all() or not np.isfinite(scores).all():
            raise RuntimeError("The inference backend returned invalid pixels.")
        selected = np.array(Image.fromarray(masks[0, int(scores[0].argmax())]).resize(size, Image.Resampling.BILINEAR)) > 0
        if request.get("invert", False):
            selected = ~selected
        if not selected.any():
            raise ValueError("No object found. Try a box or another point.")
        Image.fromarray(selected.astype(np.uint8) * 255).save(request["output"])
        return {"path": request["output"], "backend": self.notice, "coverage": float(selected.mean())}

    def repair(self, request):
        """Return a sparse native-linear patch, never a flattened photograph.

        The reversible model-view transform is used only on the context sent
        to LaMa. No source pixels outside the repair are saved or quantised.
        """
        header = struct.Struct('<8sII64s9f')
        path = Path(request['image'])
        with path.open('rb') as source:
            data = source.read(header.size)
        if len(data) != header.size:
            raise ValueError('Truncated AI source')
        magic, width, height, parent, *coefficients = header.unpack(data)
        if magic != b'ORESRC01' or not 0 < width * height <= MAX_PIXELS or path.stat().st_size != header.size + width * height * 16:
            raise ValueError('Invalid AI source')
        matrix = np.array(coefficients, np.float64).reshape(3, 3)
        if not np.isfinite(matrix).all() or abs(np.linalg.det(matrix)) < 1e-10:
            raise ValueError('Invalid AI source colour transform')
        src = np.memmap(path, dtype='<f4', mode='r', offset=header.size, shape=(height, width, 4))
        with Image.open(request['mask']) as opened:
            if opened.size != (width, height):
                raise ValueError('AI selection does not match its source')
            selection = opened.convert('L').point(lambda v: 255 if v > 127 else 0)
        small = selection.copy()
        small.thumbnail((1600, 1600), Image.Resampling.BOX)
        mask, blend = removal_masks(small, float(request.get('margin', .005)))
        mask = mask.resize((width, height), Image.Resampling.NEAREST)
        # Preserve thin source-space selections when reducing the growth mask.
        from PIL import ImageChops
        mask = ImageChops.lighter(mask, selection)
        box = mask.getbbox()
        if not box:
            raise ValueError('Select something to remove first')
        x0, y0, x1, y1 = box
        if (x1-x0)*(y1-y0) > width*height*.5:
            raise ValueError('Select a smaller distraction with surrounding image detail')
        left, top, right, bottom = context_box(box, (width, height))
        linear = np.asarray(src[top:bottom, left:right, :3]) @ matrix.T
        if not np.isfinite(linear).all():
            raise ValueError('Non-finite AI source pixels')
        scale = 1 / max(.05, float(np.percentile(np.maximum(linear[::8, ::8], 0), 95)))
        positive = np.maximum(linear * scale, 0)
        mapped = positive / (1 + positive)
        encoded = np.where(mapped <= .0031308, mapped*12.92, 1.055*mapped**(1/2.4)-.055)
        photo = Image.fromarray(np.rint(np.clip(encoded, 0, 1)*255).astype(np.uint8))
        del linear, positive, mapped, encoded
        photo.thumbnail((512, 512), Image.Resampling.LANCZOS)
        hole = (np.array(mask.crop((left, top, right, bottom)).resize(photo.size, Image.Resampling.BOX)) > 0).astype(np.float32)
        pw, ph = photo.size
        padding = ((0, 512-ph), (0, 512-pw))
        rgb = np.pad(np.array(photo, np.float32)/255, (*padding, (0, 0)), mode='symmetric')
        hole = np.pad(hole, padding, mode='symmetric')
        result = self.session('lama', cpu=True).run(None, {
            'image': rgb.transpose(2, 0, 1)[None].copy(), 'mask': hole[None, None]})[0][0].transpose(1, 2, 0)[:ph, :pw]
        if not np.isfinite(result).all():
            raise ValueError('The removal model returned invalid pixels')
        # Resize directly to the saved patch, avoiding a full context-sized
        # generated buffer. Model output is encoded RGB in the range 0..255.
        sample_box = ((x0-left)*pw/(right-left), (y0-top)*ph/(bottom-top),
                      (x1-left)*pw/(right-left), (y1-top)*ph/(bottom-top))
        generated = np.stack([np.array(Image.fromarray(result[..., c]).resize((x1-x0, y1-y0),
                     Image.Resampling.BICUBIC, box=sample_box)) for c in range(3)], axis=-1)/255
        generated = np.clip(generated, 0, 254.5/255)
        mapped = np.where(generated <= .04045, generated/12.92, ((generated+.055)/1.055)**2.4)
        native = (mapped / (1-mapped) / scale) @ np.linalg.inv(matrix).T
        bw, bh = blend.size
        alpha = np.array(blend.resize((x1-x0, y1-y0), Image.Resampling.BILINEAR,
                        box=(x0*bw/width, y0*bh/height, x1*bw/width, y1*bh/height)), np.float32)/255
        alpha[np.array(selection.crop(box)) > 127] = 1
        alpha *= np.array(mask.crop(box)) > 0
        payload = np.concatenate((native, alpha[..., None]), axis=2).astype('<f4')
        if not np.isfinite(payload).all():
            raise ValueError('Invalid generated repair')
        output = Path(request['output'])
        with output.open('wb') as target:
            target.write(struct.pack('<8s6I64s', b'OREPAIR1', width, height, x0, y0, x1-x0, y1-y0, parent))
            payload.tofile(target)
        return {'ok': True, 'path': str(output), 'backend': 'CPU'}

    def remove(self, request):
        # Portable, qualified CPU baseline; no vendor-specific runtime.
        with tifffile.TiffFile(request["image"]) as tiff:
            page = tiff.pages[0]
            if page.imagewidth * page.imagelength > MAX_PIXELS:
                raise ValueError("Removal currently supports photos up to 110 megapixels.")
            src = page.asarray()
        if src.dtype != np.uint16 or src.ndim != 3 or src.shape[2] != 3:
            raise ValueError("Removal needs a rendered 16-bit sRGB TIFF.")
        height, width = src.shape[:2]
        with Image.open(request["mask"]) as im:
            if im.width * im.height > 4_000_000:
                raise ValueError("Removal selections must be at most four megapixels.")
            selection = im.convert("L").point(lambda v: 255 if v > 127 else 0)
            mask, blend = removal_masks(selection, float(request.get("margin", .005)))
        mask = mask.resize((width, height), Image.Resampling.NEAREST)
        box = mask.getbbox()
        if not box:
            raise ValueError("Select something to remove first.")
        x0, y0, x1, y1 = box
        if (x1 - x0) * (y1 - y0) > width * height * .5:
            raise ValueError("Select a smaller distraction. This tool needs surrounding image detail.")
        crop = context_box(box, (width, height))
        left, top, right, bottom = crop
        rgb = (src[top:bottom, left:right] // 257).astype(np.uint8)
        local_mask = mask.crop(crop)
        inference_size = (right - left, bottom - top)
        photo = Image.fromarray(rgb)
        photo.thumbnail((512, 512), Image.Resampling.LANCZOS)
        model_mask = local_mask.resize(photo.size, Image.Resampling.BOX)
        # Preserve thin marks when downsampling. White means removed in LaMa.
        hole = (np.array(model_mask) > 0).astype(np.float32)
        pw, ph = photo.size
        padding = ((0, 512 - ph), (0, 512 - pw))
        rgb = np.pad(np.array(photo, np.float32) / 255, (*padding, (0, 0)), mode="symmetric")
        hole = np.pad(hole, padding, mode="symmetric")
        raw = self.session("lama", cpu=True).run(None, {
            "image": rgb.transpose(2, 0, 1)[None].copy(), "mask": hole[None, None]})[0][0].transpose(1, 2, 0)[:ph, :pw]
        if not np.isfinite(raw).all():
            raise RuntimeError("The removal model returned invalid pixels.")
        # Resize float channels separately to retain sub-byte model precision.
        generated = np.stack([np.array(Image.fromarray(raw[..., c]).resize(inference_size, Image.Resampling.BICUBIC))
                              for c in range(3)], axis=-1) * 257
        hard = np.array(local_mask) > 127
        # Resize only the required part of the small preview blend mask.
        bw, bh = blend.size
        alpha = np.array(blend.resize(inference_size, Image.Resampling.BILINEAR,
                         box=(left * bw / width, top * bh / height, right * bw / width, bottom * bh / height)), np.float32) / 255
        # Bilinear enlargement also softens the original selection boundary,
        # especially at zero/one-pixel coverage. Keep that object fully gone.
        core = np.array(selection.resize((width, height), Image.Resampling.NEAREST).crop(crop)) > 127
        alpha[core] = 1
        alpha *= hard
        region = src[top:bottom, left:right]
        # Boolean indexed assignment guarantees bit-for-bit preservation
        # outside hard, independent of floating-point roundoff.
        blended = np.rint(region.astype(np.float32) * (1 - alpha[..., None]) + generated * alpha[..., None])
        region[hard] = np.clip(blended[hard], 0, 65535).astype(np.uint16)
        write_rendered_dng(request["output"], src)
        preview = Image.fromarray((src // 257).astype(np.uint8))
        preview.thumbnail((1600, 1600), Image.Resampling.LANCZOS)
        preview.save(request["preview"])
        return {"path": request["output"], "preview": request["preview"], "backend": "CPU", "width": width, "height": height}

    def run(self, request):
        action = request.get("action")
        if action == "refine":
            with Image.open(request["image"]) as photo, Image.open(request["mask"]) as selection:
                if max(photo.size + selection.size) > 4096:
                    raise ValueError("Edge refinement sources must be at most 4096 pixels per side.")
                # Both images describe the same developed frame. Allow only
                # the rounding from bounded preview dimensions, not a crop.
                if abs(photo.width * selection.height - photo.height * selection.width) > 2 * max(photo.size + selection.size):
                    raise ValueError("The refinement source does not match the selection geometry.")
                aligned = selection.convert("L").resize(photo.size, Image.Resampling.BILINEAR)
                refined = refine_selection(photo, aligned)
                changed = int(np.count_nonzero(np.array(refined) != np.array(aligned)))
            refined.save(request["output"])
            return {"path": request["output"], "changed_pixels": changed}
        if action == "mask":
            try:
                return self.mask(request)
            except (RuntimeError, ort.capi.onnxruntime_pybind11_state.Fail, ort.capi.onnxruntime_pybind11_state.RuntimeException):
                if self.device is None:
                    raise
                self.device = None
                self.sessions.clear(); self.embedding = None; self.image_key = None
                self.notice = "CPU (Vulkan failed)"
                return self.mask(request)
        if action == "render_copy":
            pixels = tifffile.imread(request["image"])
            if pixels.dtype != np.uint16 or pixels.ndim != 3 or pixels.shape[2] != 3 or pixels.shape[0]*pixels.shape[1] > MAX_PIXELS:
                raise ValueError("Invalid rendered DNG source")
            write_rendered_dng(request["output"], pixels)
            return {"ok": True, "path": request["output"]}
        if action == "repair":
            return self.repair(request)
        if action == "remove":
            return self.remove(request)
        if action == "save":
            target = Path(request["output"])
            if target.suffix.lower() != '.dng':
                raise ValueError('Choose a .dng filename.')
            # Publish the completed copy atomically without overwriting a
            # file another process may have created since the save dialog.
            fd, name = tempfile.mkstemp(prefix=".omaraw-ai-", suffix=".dng", dir=target.parent)
            try:
                with os.fdopen(fd, "wb") as out, open(request["image"], "rb") as source:
                    shutil.copyfileobj(source, out, 1024 * 1024)
                    out.flush(); os.fsync(out.fileno())
                if any(os.path.lexists(p) for p in [str(target) + ".xmp", *request.get("sidecars", [])]):
                    raise FileExistsError("A sidecar already uses that filename. Choose a new name.")
                try:
                    os.link(name, target)
                except OSError as error:
                    if error.errno not in (errno.EOPNOTSUPP, errno.EPERM):
                        raise
                    # FAT/exFAT photo drives do not support hard links.
                    # Linux renameat2 also publishes without replacement.
                    libc = ctypes.CDLL(None, use_errno=True)
                    rename = libc.renameat2
                    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
                    rename.restype = ctypes.c_int
                    if rename(-100, os.fsencode(name), -100, os.fsencode(target), 1):
                        code = ctypes.get_errno()
                        raise OSError(code, os.strerror(code), str(target))
            finally:
                Path(name).unlink(missing_ok=True)
            return {"path": str(target)}
        raise ValueError("Unknown AI operation")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--home", required=True)
    parser.add_argument("--backend", choices=("cpu", "vulkan"), default="cpu")
    args = parser.parse_args()
    worker = Inference(args.home, args.backend)
    for line in sys.stdin:
        request = {}
        start = time.monotonic()
        try:
            if len(line) > 65536:
                raise ValueError("AI request is too large")
            request = json.loads(line)
            if not isinstance(request, dict):
                request = {}
                raise ValueError("AI requests must be JSON objects")
            result = {"ok": True, **worker.run(request)}
        except ModelError as error:
            result = {"ok": False, "error": str(error), "repair_required": True}
        except Exception as error:
            result = {"ok": False, "error": str(error)}
        result.update(id=request.get("id"), seconds=round(time.monotonic() - start, 3))
        print(json.dumps(result), flush=True)


if __name__ == "__main__":
    main()
