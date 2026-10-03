"""The film data as functions: a stock's curves loaded from data/film and
resampled onto common grids, so the print chain can integrate them.

    stock = Stock("kodak-2383")
    stock.density("red", log_exposure)          # Status A/M density
    stock.sensitivity("cyan-forming", nm)       # log10 sensitivity
    stock.dye("cyan", nm)                       # spectral density per unit
    stock.dmin(nm)                              # the base + fog spectrum

Wavelengths are nanometres on WAVELENGTHS (380..780 step 5); log exposure is
log10 lux-seconds as the sheet plotted it (relative for the Fuji stocks).
Outside a curve's plotted span the value is held at the end: a sheet's
plot ends where the film stops changing.
"""
import csv, os
import numpy as np

ROOT = os.path.join(os.path.dirname(__file__), "..", "..", "data", "film")
WAVELENGTHS = np.arange(380.0, 781.0, 5.0)


def read_curve(path):
    xs, ys = [], []
    with open(path) as f:
        for line in f:
            if line.startswith("#") or not line.strip(): continue
            x, y = line.split(","); xs.append(float(x)); ys.append(float(y))
    xs, ys = np.array(xs), np.array(ys)
    order = np.argsort(xs)
    return xs[order], ys[order]


class Curve:
    def __init__(self, path):
        self.x, self.y = read_curve(path)
    def __call__(self, x):
        return np.interp(x, self.x, self.y)          # held at the ends
    @property
    def span(self):
        return float(self.x[0]), float(self.x[-1])


class Stock:
    def __init__(self, name):
        self.name = name; self.dir = os.path.join(ROOT, name)
        if not os.path.isdir(self.dir): raise FileNotFoundError(self.dir)
        self.curves = {}
        for f in sorted(os.listdir(self.dir)):
            if f.endswith(".csv"):
                kind, _, which = f[:-4].partition("-"); self.curves.setdefault(kind, {})[which] = Curve(os.path.join(self.dir, f))
    def has(self, kind, which=None):
        return kind in self.curves and (which is None or which in self.curves[kind])
    def density(self, layer, log_exposure):
        return self.curves["sensitometric"][layer](log_exposure)
    def sensitivity(self, layer, nm=WAVELENGTHS):
        return self.curves["sensitivity"][layer](nm)
    def dye(self, dye, nm=WAVELENGTHS):
        return self.curves["dyes"][dye](nm)
    def dmin(self, nm=WAVELENGTHS):
        d = self.curves["dyes"]
        if "minimum-density" in d: return d["minimum-density"](nm)
        return np.zeros_like(np.asarray(nm, dtype=float))
    def __repr__(self):
        return f"Stock({self.name}: " + ", ".join(f"{k}[{','.join(v)}]" for k, v in self.curves.items()) + ")"


if __name__ == "__main__":
    import sys
    for name in sys.argv[1:] or sorted(os.listdir(ROOT)):
        s = Stock(name); print(s)
        for kind, curves in s.curves.items():
            for which, c in curves.items(): print(f"   {kind}/{which}: x {c.span[0]:.2f}..{c.span[1]:.2f}")
