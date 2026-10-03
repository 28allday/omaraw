#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Digital cinema looks: linear Rec.2020 -> Rec.709/Cineon -> stock -> display.

Offline developer baker; no camera-negative simulation or third-party LUTs.
Uses our existing extracted stock data and documented spectral approximations.
The LAD and display-white anchors are rendering choices, not a physical-film
accuracy claim. Still-negative models and imported camera profiles are separate.
"""
import argparse
import json
from pathlib import Path

import numpy as np
import print_chain as pc

S = pc.colour.RGB_COLOURSPACES["sRGB"]  # Rec.709 primaries, linear calculations
WY = S.matrix_RGB_to_XYZ[1]
MAX_PD = 1023 * .002
STOCKS = ("kodak-2383", "kodak-2393", "fuji-3510", "fuji-3513di",
          "fuji-3521xd", "fuji-3523xd", "agfa-cp30", "kodak-2302")


def cineon_codes(linear):
    """Full-range Cineon: black 95, linear 1 at 685; not video legal range."""
    b = 10 ** ((95 - 685) / 300)
    return np.clip(685 + 300 * np.log10(np.maximum(linear, 0) * (1-b) + b), 0, 1023)


def gamut_map(rgb):
    """Move out-of-gamut colours towards their luminance, preserving that Y.

    The largest common chroma factor that fits all three display channels.
    In-gamut pixels are unchanged. This avoids independent channel clipping.
    """
    y = np.clip(rgb @ WY, 0, 1)
    delta = rgb - y[:, None]
    scale = np.ones(len(y))
    for c in range(3):
        d = delta[:, c]
        pos, neg = d > 1e-12, d < -1e-12
        scale[pos] = np.minimum(scale[pos], (1-y[pos]) / d[pos])
        scale[neg] = np.minimum(scale[neg], -y[neg] / d[neg])
    return y[:, None] + np.clip(scale, 0, 1)[:, None] * delta


class DigitalFilm:
    def __init__(self, name):
        if name not in STOCKS:
            raise ValueError(f"Not a cinema stock: {name}")
        self.name = name
        self.mono = name == "kodak-2302"
        stock = pc.Stock(name)
        self.matrix = pc.colour.matrix_RGB_to_RGB(pc.BT2020, S).T
        if self.mono:
            # Digital monochrome input is scene luminance, not a simulated
            # negative's blue-sensitive dye response.
            self.matrix = np.repeat((self.matrix @ WY)[:, None], 3, axis=1)
            self.curve = stock.curves["sensitometric"]["7min"]
            self.aim = np.ones(3)
            curves = [self.curve] * 3
        else:
            donor = pc.Stock(pc.DYE_STAND_IN[name]) if name in pc.DYE_STAND_IN else stock
            self.emulsion = pc.Emulsion(stock, dyes_from=donor, status=pc.STATUS_A)
            if name == "kodak-2383":
                stock.curves["sensitometric"] = pc.Stock("kodak-2383-v3").curves["sensitometric"]
            self.aim = np.asarray(pc.LAD[name])[::-1]  # model B,G,R order
            self.emulsion.calibrate_to(self.aim, pc.NEUTRAL_DENSITY.get(donor.name, 1.0))
            curves = [stock.curves["sensitometric"][c] for c in self.emulsion.layers]
        # Tiny reversals from tracing a plotted curve are not film behaviour.
        # Enforce non-decreasing density locally in this digital model only.
        self.curve_adjustment = max(float(np.max(np.maximum.accumulate(c.y)-c.y)) for c in curves)
        for c in curves:
            c.y = np.maximum.accumulate(c.y)
        # H-387 intermediate-negative convention: CV445 is PD .890.
        # No negative fit or stock-dependent input density gains here.
        self.offset = np.array([self._inverse(c, a) for c, a in zip(curves, self.aim)]) + .89
        self.peak_density = self.densities(np.full((1, 3), MAX_PD))[0]
        self.white = self._project(self.peak_density[None, :])[0]
        lad = self._project(self.densities(np.full((1, 3), .89)))[0] / self.white
        # Map the digital upper code to display white and the LAD to visual
        # density 1 (Y=.1). A single luminance exponent retains colour ratios;
        # it does not neutralise the entire grey axis or fit a reference LUT.
        self.exponent = float(np.log(.1) / np.log(lad @ WY))
        if not (np.all(self.white > 0) and np.isfinite(self.exponent) and .25 < self.exponent < 4):
            raise ValueError(f"Invalid display anchors for {name}")

    @staticmethod
    def _inverse(curve, density):
        x = np.linspace(curve.x[0], curve.x[-1], 8192)
        return np.interp(density, np.maximum.accumulate(curve(x)), x)

    def densities(self, pd_bgr):
        exposure = -np.asarray(pd_bgr) + self.offset
        if self.mono:
            return self.curve(exposure)
        return self.emulsion.densities(exposure)

    def _project(self, density_bgr):
        if self.mono:
            return np.repeat(10 ** -np.mean(density_bgr, axis=1, keepdims=True), 3, axis=1)
        spectra = self.emulsion.transmittance(density_bgr)
        light = pc.XENON
        xyz = (spectra * light) @ pc.CMFS / (pc.CMFS[:, 1] * light).sum()
        white = pc.xyz_of(10 ** -self.emulsion.dmin, light)
        xyz /= white[1]
        xyz = pc.colour.adaptation.chromatic_adaptation_VonKries(
            xyz, white / white[1], pc.colour.xy_to_XYZ(S.whitepoint), transform="Bradford")
        return pc.colour.XYZ_to_RGB(xyz, S, illuminant=S.whitepoint)

    def display(self, density_bgr):
        rgb = self._project(density_bgr) / self.white
        y = np.maximum(rgb @ WY, 1e-15)
        rgb *= (np.clip(y, 0, 1) ** self.exponent / y)[:, None]
        return pc.colour.RGB_to_RGB(gamut_map(rgb), S, pc.BT2020)

    def render_codes(self, codes, trim=(0, 0, 0), gain=(1, 1, 1)):
        """Original Cineon RGB codes, for Kodak LAD checks (no re-encoding)."""
        rgb_pd = np.clip(np.asarray(codes).reshape(-1, 3), 0, 1023) * .002
        rgb_pd = rgb_pd * np.asarray(gain) - np.asarray(trim)
        return self.display(self.densities(rgb_pd[:, ::-1])).reshape(np.shape(codes))

    def render(self, linear2020, **kwargs):
        shape = np.shape(linear2020)
        codes = cineon_codes(np.asarray(linear2020).reshape(-1, 3) @ self.matrix)
        return self.render_codes(codes, **kwargs).reshape(shape)

    def bake(self, path, size=65):
        """OMPT4: A2 encodes Cineon, A3 is density identity.

        M converts scene primaries, B1 holds stock curves, B2 holds LINEAR
        Rec.709 display colour. Convex interpolation keeps the display gamut;
        logarithmic interpolation of Rec.2020 could exceed it and lose colour
        near the boundary. No new runtime dependency or GPU API.
        """
        n = 4096
        t, u = np.linspace(0, 1, size), np.linspace(0, 1, n)
        b, g, r = np.meshgrid(t, t, t, indexing="ij")
        grid = np.stack([r.ravel(), g.ravel(), b.ravel()], axis=1)
        lo, hi, dmax, llmin = -6.0, 3.0, 6.0, 0.0
        a2 = np.repeat((cineon_codes(10 ** (lo + u * (hi-lo))) / 1023)[:, None], 3, axis=1)
        a3 = grid
        # Concentrate cube nodes near highlight densities, where light changes
        # fastest, and put the upper-code white exactly on the cube origin.
        # B1/B2 share this shaper; runtime interpolation needs no extra step.
        density = self.densities(np.repeat((u * MAX_PD)[:, None], 3, axis=1))
        b1 = np.sqrt(np.clip((density-self.peak_density) / (dmax-self.peak_density), 0, 1))[:, ::-1]
        b2 = np.empty_like(grid)
        for i in range(0, len(grid), 8192):
            density = self.peak_density + grid[i:i+8192, ::-1] ** 2 * (dmax-self.peak_density)
            b2[i:i+8192] = pc.colour.RGB_to_RGB(self.display(density), pc.BT2020, S)
        arrays = (a2, a3, b1, b2)
        assert all(np.isfinite(a).all() for a in arrays)
        with Path(path).open("wb") as f:
            f.write(b"OMPT")
            f.write(np.array([4, size, n], dtype="<u4").tobytes())
            f.write(np.array([1, 0, lo, hi, MAX_PD, MAX_PD, dmax, llmin], dtype="<f4").tobytes())
            f.write(self.matrix.astype("<f4").tobytes())
            for a in arrays:
                f.write(np.round(np.clip(a, 0, 1) * 65535).astype("<u2").tobytes())

    def metadata(self):
        return dict(stock=self.name, input="scene-linear Rec.2020 -> Rec.709 primaries / Cineon Film Log",
                    output="display-linear Rec.2020; SDR Rec.709 gamut", camera_negative=False,
                    lad_code=445, lad_Y=.1, peak_code=1023, peak_Y=1,
                    display_white_rgb709=self.white.tolist(), display_exponent=self.exponent,
                    maximum_monotonic_curve_adjustment=self.curve_adjustment,
                    fitted_to_third_party_LUT=False,
                    spectral_donor=pc.DYE_STAND_IN.get(self.name))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--size", type=int, choices=[33, 65], default=65)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    metadata = []
    for name in STOCKS:
        film = DigitalFilm(name)
        film.bake(args.out / (name + "-digital.ompt"), args.size)
        metadata.append(film.metadata())
        print(name, "baked", flush=True)
    (args.out / "digital-film.json").write_text(json.dumps(metadata, indent=2) + "\n")
