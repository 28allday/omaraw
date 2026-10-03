#!/usr/bin/env python3
"""Trace a coloured curve out of a scanned plot.

  trace-raster.py <image.png> <spec.json> <out-dir>

spec.json: {"name": ..., "x_range": [x_at_left_frame, x_at_right_frame],
            "y_range": [y_at_bottom_frame, y_at_top_frame],
            "frame": [x0, y0, x1, y1] (image pixels; omit to detect the
            outermost dark rows/columns), "gap": max columns to bridge,
            "curves": {"name": {"rgb": [r, g, b], "tol": 60}}}

For every column the matching pixels' median row is the curve; short gaps
(dashes) are bridged by interpolation. Writes CSVs and a check plot.
"""
import json, sys, os
import numpy as np
from PIL import Image


def detect_frame(a, threshold=200):
    """The outermost rows and columns that are mostly ink: the plot frame,
    which scanned sheets draw in a lighter grey than the curves."""
    dark = a.mean(axis=2) < threshold
    rows = np.where(dark.mean(axis=1) > 0.6)[0]; cols = np.where(dark.mean(axis=0) > 0.6)[0]
    return [int(cols.min()), int(rows.min()), int(cols.max()), int(rows.max())]


def main(image, spec_path, out_dir):
    spec = json.load(open(spec_path)); os.makedirs(out_dir, exist_ok=True)
    a = np.asarray(Image.open(image).convert("RGB")).astype(int)
    fx0, fy0, fx1, fy1 = spec.get("frame") or detect_frame(a)
    (xa, xb), (ya, yb) = spec["x_range"], spec["y_range"]
    print(f"frame px ({fx0},{fy0})-({fx1},{fy1})")
    import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(6, 4.5))
    for name, c in spec["curves"].items():
        rgb = np.array(c["rgb"]); tol = c.get("tol", 60)
        match = np.abs(a - rgb).sum(axis=2) < tol
        match[:fy0+1, :] = False; match[fy1:, :] = False; match[:, :fx0+1] = False; match[:, fx1:] = False
        if "exclude" in c:
            for ex0, ey0, ex1, ey1 in c["exclude"]: match[ey0:ey1, ex0:ex1] = False
        cols = np.arange(fx0, fx1 + 1); ys = np.full(len(cols), np.nan)
        for i, x in enumerate(cols):
            r = np.where(match[:, x])[0]
            if len(r): ys[i] = np.median(r)
        have = ~np.isnan(ys)
        if have.sum() < 10: print(f"{name}: too few pixels ({have.sum()})"); continue
        # bridge gaps up to `gap` columns; leave longer ones (the ends) empty
        gap = spec.get("gap", 40); filled = ys.copy(); idx = np.where(have)[0]
        for k in range(len(idx) - 1):
            i0, i1 = idx[k], idx[k+1]
            if 1 < i1 - i0 <= gap: filled[i0:i1+1] = np.interp(np.arange(i0, i1+1), [i0, i1], [ys[i0], ys[i1]])
        keep = ~np.isnan(filled)
        xs = xa + (cols[keep] - fx0) * (xb - xa) / (fx1 - fx0)
        yd = ya + (fy1 - filled[keep]) * (yb - ya) / (fy1 - fy0)
        # a light smoothing over the raster jitter
        if len(yd) > 9: yd = np.convolve(np.pad(yd, 4, mode="edge"), np.ones(9) / 9, mode="valid")
        with open(os.path.join(out_dir, f'{spec["name"]}-{name}.csv'), "w") as f:
            f.write(f'# {spec["name"]} {name}; traced from {image} by colour {c["rgb"]}\n')
            for x, y in zip(xs, yd): f.write(f"{x:.5f},{y:.5f}\n")
        ax.plot(xs, yd, label=name); print(f'{name}: {len(xs)} columns, x {xs[0]:.2f}..{xs[-1]:.2f}, y {yd.min():.2f}..{yd.max():.2f}')
    ax.grid(True, lw=0.3); ax.legend(); ax.set_title(spec["name"]); fig.savefig(os.path.join(out_dir, f'{spec["name"]}.png'), dpi=110, bbox_inches="tight")


if __name__ == "__main__": main(*sys.argv[1:4])
