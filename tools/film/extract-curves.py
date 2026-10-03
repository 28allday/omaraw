#!/usr/bin/env python3
"""Pull the curves out of a film data sheet's vector plots.

Two modes.

  dump  <pdf> <page> <out.png>
      Draws every long path on the page with a number, and prints each
      path's number, colour, bounding box and point count, so a plot's
      curves and its frame can be identified by eye.

  extract <pdf> <page> <spec.json> <out-dir>
      spec.json names the plot frame (the path number of the rectangle,
      or an explicit bbox) and its data ranges, and maps path numbers to
      curve names. Writes one CSV per curve in data coordinates and a
      check plot beside it.

Points are in PDF user space (origin top-left, y down); the frame maps
linearly to the data ranges, which is how the sheets draw their axes.
"""
import json, sys, math
import pymupdf


def paths_on(page, min_points=6):
    """Every drawing on the page, split into its subpaths (one per pen lift),
    each as a polyline with cubic segments sampled."""
    out = []
    for i, d in enumerate(page.get_drawings()):
        subs, cur, last = [], [], None
        for it in d["items"]:
            kind = it[0]
            if kind == "re":
                r = it[1]; seg = [r.tl, r.tr, r.br, r.bl, r.tl]
            elif kind == "l":
                seg = [it[1], it[2]]
            elif kind == "c":
                p0, p1, p2, p3 = it[1], it[2], it[3], it[4]; seg = [p0]
                for k in range(1, 9):
                    t = k / 8
                    seg.append(pymupdf.Point((1-t)**3*p0.x + 3*(1-t)**2*t*p1.x + 3*(1-t)*t**2*p2.x + t**3*p3.x,
                                             (1-t)**3*p0.y + 3*(1-t)**2*t*p1.y + 3*(1-t)*t**2*p2.y + t**3*p3.y))
            else:
                continue
            if last is not None and (abs(seg[0].x - last.x) > 0.05 or abs(seg[0].y - last.y) > 0.05):
                subs.append(cur); cur = []
            cur.extend(seg if not cur else seg[1:]); last = seg[-1]
        if cur: subs.append(cur)
        for j, pts in enumerate(subs):
            if len(pts) >= min_points:
                xs = [q.x for q in pts]; ys = [q.y for q in pts]
                out.append({"id": f"{i}.{j}", "points": pts, "rect": pymupdf.Rect(min(xs), min(ys), max(xs), max(ys)),
                            "color": d.get("color"), "width": d.get("width")})
    return out


def calibrate(page, plot):
    """The plot's linear axes from its tick labels: x_ticks / y_ticks map
    the label text to its data value; the label's centre gives the pixel."""
    words = page.get_text("words")
    area = pymupdf.Rect(*plot["area"]) if "area" in plot else page.rect
    def centre(label, axis):
        hits = [w for w in words if w[4] == label and area.contains(pymupdf.Point((w[0]+w[2])/2, (w[1]+w[3])/2))]
        if not hits: raise SystemExit(f"tick label {label!r} not found in {area}")
        # The same figure can label both axes: x labels sit lowest, y labels leftmost.
        w = max(hits, key=lambda w: w[3]) if axis == "x" else min(hits, key=lambda w: w[0])
        return (w[0]+w[2])/2 if axis == "x" else (w[1]+w[3])/2
    # Sheets whose labels are drawn as outlines give pixel positions directly.
    xt = [tuple(t) for t in plot["x_px"]] if "x_px" in plot else [(centre(k, "x"), v) for k, v in plot["x_ticks"].items()]
    yt = [tuple(t) for t in plot["y_px"]] if "y_px" in plot else [(centre(k, "y"), v) for k, v in plot["y_ticks"].items()]
    (px0, vx0), (px1, vx1) = xt[0], xt[-1]; (py0, vy0), (py1, vy1) = yt[0], yt[-1]
    return lambda pt: (vx0 + (pt.x - px0) * (vx1 - vx0) / (px1 - px0), vy0 + (pt.y - py0) * (vy1 - vy0) / (py1 - py0))


def dump(pdf, pno, out_png):
    doc = pymupdf.open(pdf); page = doc[pno]
    ps = paths_on(page)
    pix = page.get_pixmap(dpi=150)
    import matplotlib; matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    img = np.frombuffer(pix.samples, dtype=np.uint8).reshape(pix.height, pix.width, pix.n)
    fig, ax = plt.subplots(figsize=(pix.width/100, pix.height/100), dpi=100)
    ax.imshow(img, extent=(0, page.rect.width, page.rect.height, 0))
    for p in ps:
        xs = [q.x for q in p["points"]]; ys = [q.y for q in p["points"]]
        ax.plot(xs, ys, lw=0.8)
        ax.text(xs[len(xs)//2], ys[len(ys)//2], str(p["id"]), fontsize=6, color="red", weight="bold")
        r = p["rect"]
        print(f'{p["id"]:>7}  n={len(p["points"]):4d}  colour={p["color"]}  bbox=({r.x0:.1f},{r.y0:.1f})-({r.x1:.1f},{r.y1:.1f})')
    ax.set_axis_off(); fig.savefig(out_png, dpi=100, bbox_inches="tight"); print("wrote", out_png)


def extract(pdf, pno, spec_path, out_dir):
    import os
    spec = json.load(open(spec_path))
    doc = pymupdf.open(pdf); page = doc[pno]
    ps = {p["id"]: p for p in paths_on(page, min_points=2)}
    os.makedirs(out_dir, exist_ok=True)
    import matplotlib; matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    for plot in spec["plots"]:
        to_data = calibrate(page, plot)
        fig, ax = plt.subplots(figsize=(6, 4.5))
        for name, pid in plot["curves"].items():
            ids = pid if isinstance(pid, list) else [pid]
            pts = []
            for i in ids: pts.extend(ps[i]["points"])
            data = [to_data(q) for q in pts]
            data.sort()
            # drop duplicate x from sampled curves
            clean = []
            for x, y in data:
                if not clean or abs(x - clean[-1][0]) > 1e-6: clean.append((x, y))
            shift = plot.get("x_shift", {}).get(name, 0.0)
            with open(os.path.join(out_dir, f'{plot["name"]}-{name}.csv'), "w") as f:
                f.write(f'# {plot["name"]} {name}; x={plot.get("x_label","x")} y={plot.get("y_label","y")}; source {pdf} page {pno+1} path {ids}; x shifted by {-shift}\n')
                for x, y in clean: f.write(f"{x - shift:.5f},{y:.5f}\n")
            ax.plot([x - shift for x, _ in clean], [y for _, y in clean], label=name)
            print(f'{plot["name"]}/{name}: {len(clean)} points, x {clean[0][0]-shift:.3f}..{clean[-1][0]-shift:.3f}, y {min(y for _,y in clean):.3f}..{max(y for _,y in clean):.3f}')
        ax.set_xlabel(plot.get("x_label", "x")); ax.set_ylabel(plot.get("y_label", "y")); ax.grid(True, lw=0.3); ax.legend(); ax.set_title(plot["name"])
        fig.savefig(os.path.join(out_dir, f'{plot["name"]}.png'), dpi=110, bbox_inches="tight"); plt.close(fig)


def ticks(pdf, pno, area):
    """Short axis tick segments inside area, so outline-labelled axes can be
    calibrated by hand: prints vertical ticks by x and horizontal ticks by y."""
    doc = pymupdf.open(pdf); page = doc[pno]; r = pymupdf.Rect(*area)
    vs, hs = [], []
    for d in page.get_drawings():
        for it in d["items"]:
            if it[0] != "l": continue
            a, b = it[1], it[2]
            if not (r.contains(a) and r.contains(b)): continue
            if abs(a.x - b.x) < 0.3 and 1.5 < abs(a.y - b.y) < 12: vs.append((round(a.x, 1), round(min(a.y, b.y), 1), round(abs(a.y - b.y), 1)))
            if abs(a.y - b.y) < 0.3 and 1.5 < abs(a.x - b.x) < 12: hs.append((round(a.y, 1), round(min(a.x, b.x), 1), round(abs(a.x - b.x), 1)))
    print("vertical ticks (x, top y, length):", sorted(set(vs)))
    print("horizontal ticks (y, left x, length):", sorted(set(hs)))
    ws = [w for w in page.get_text("words") if r.contains(pymupdf.Point((w[0]+w[2])/2, (w[1]+w[3])/2))]
    print("words:", [(w[4], round((w[0]+w[2])/2), round((w[1]+w[3])/2)) for w in ws][:60])


def frame(pdf, pno, area, dpi=300):
    """The plot frame of an outline-drawn plot, from a render: rows and
    columns that are dark along most of the area are the frame lines."""
    import numpy as np
    doc = pymupdf.open(pdf); page = doc[pno]; r = pymupdf.Rect(*area)
    pix = page.get_pixmap(dpi=dpi, clip=r, colorspace="gray")
    a = np.frombuffer(pix.samples, dtype=np.uint8).reshape(pix.height, pix.width) < 128
    rows = np.where(a.mean(axis=1) > 0.6)[0]; cols = np.where(a.mean(axis=0) > 0.6)[0]
    k = 72 / dpi
    print("dark rows (pdf y):", [round(r.y0 + y * k, 1) for y in rows][:20])
    print("dark cols (pdf x):", [round(r.x0 + x * k, 1) for x in cols][:20])


def listing(pdf, pno, spec_path):
    spec = json.load(open(spec_path)); doc = pymupdf.open(pdf); page = doc[pno]
    for plot in spec["plots"]:
        to_data = calibrate(page, plot); area = pymupdf.Rect(*plot["area"])
        print(f'== {plot["name"]}')
        for p in paths_on(page, min_points=6):
            if not area.contains(p["rect"]): continue
            data = [to_data(q) for q in p["points"]]
            xs = [d[0] for d in data]; ys = [d[1] for d in data]
            print(f'  {p["id"]:>7}  n={len(data):4d}  x {min(xs):8.3f}..{max(xs):8.3f}  y {min(ys):7.3f}..{max(ys):7.3f}  colour={p["color"]}')


if __name__ == "__main__":
    if sys.argv[1] == "list": listing(sys.argv[2], int(sys.argv[3]) - 1, sys.argv[4])
    elif sys.argv[1] == "frame": frame(sys.argv[2], int(sys.argv[3]) - 1, json.loads(sys.argv[4]))
    elif sys.argv[1] == "ticks": ticks(sys.argv[2], int(sys.argv[3]) - 1, json.loads(sys.argv[4]))
    elif sys.argv[1] == "dump": dump(sys.argv[2], int(sys.argv[3]) - 1, sys.argv[4])
    elif sys.argv[1] == "extract": extract(sys.argv[2], int(sys.argv[3]) - 1, sys.argv[4], sys.argv[5])
    else: print(__doc__)
