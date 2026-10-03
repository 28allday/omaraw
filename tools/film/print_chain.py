#!/usr/bin/env python3
"""The print chain: a scene, photographed on a colour negative, printed on a
print stock, projected with a xenon lamp, seen. Built from the makers' data
in data/film (see docs/PRINT-STOCKS.md, section 2).

    chain = PrintChain("kodak-2383")
    rgb_out = chain.render(rgb_in)     # scene-linear Rec.2020 -> display-linear Rec.2020

Offline tool: it needs numpy and colour-science (the observer, the
illuminants and the working-space matrices). What ships is the table it
bakes, not this code.
"""
import os, sys, json
import numpy as np
import colour
from filmdata import Stock, WAVELENGTHS

NM = WAVELENGTHS
SHAPE = colour.SpectralShape(380, 780, 5)
CMFS = colour.MSDS_CMFS["CIE 1931 2 Degree Standard Observer"].copy().align(SHAPE).values      # 81 x 3
D65 = colour.SDS_ILLUMINANTS["D65"].copy().align(SHAPE).values
XENON = colour.SDS_ILLUMINANTS["D55"].copy().align(SHAPE).values      # stand-in for a xenon arc (about 5400 K)
PRINTER = colour.sd_blackbody(3200, SHAPE).values                     # tungsten printer lamp
CAMERA_LIGHT = colour.sd_blackbody(3200, SHAPE).values                # a tungsten-balanced negative's own light
BT2020 = colour.RGB_COLOURSPACES["ITU-R BT.2020"]

# Where the makers' densitometers look: narrow bands standing in for
# Status M (negatives) and Status A (prints) responsivities.
STATUS_M = {"blue": 445.0, "green": 535.0, "red": 655.0}
STATUS_A = {"blue": 445.0, "green": 535.0, "red": 620.0}
LAYER_OF = {"blue": "yellow", "green": "magenta", "red": "cyan"}      # the dye a layer forms
FORMING = {"yellow": "yellow-forming", "magenta": "magenta-forming", "cyan": "cyan-forming"}

# Print aims (Status A) from each stock's sheet: what an 18% grey prints to.
LAD = {"kodak-2383": (1.09, 1.06, 1.03), "kodak-2393": (1.09, 1.06, 1.03), "agfa-cp30": (1.15, 1.05, 1.05),
       "fuji-3510": (1.10, 1.05, 1.05), "fuji-3513di": (1.10, 1.05, 1.05), "fuji-3521xd": (1.10, 1.05, 1.05), "fuji-3523xd": (1.10, 1.05, 1.05)}
# Stocks whose sheets carry no spectral data run on a documented stand-in.
DYE_STAND_IN = {"fuji-3510": "kodak-2383", "fuji-3513di": "kodak-2383", "fuji-3521xd": "kodak-2383", "fuji-3523xd": "kodak-2383"}
# The density at which each sheet's dye set forms its stated neutral: a
# unit of every dye is that neutral. Kodak normalise to a visual neutral of
# 1.0; Agfa plot the dyes of a neutral at D 3.00. Fuji stocks borrow Kodak's.
NEUTRAL_DENSITY = {"agfa-cp30": 3.0}
GREY = 0.18
NEG_GREY_LOG_E = -1.5      # 5219 sheet: 0 camera stops sits near log E -1.5 on its exposure axis


def band(nm_centre, width=20.0):
    w = np.exp(-0.5 * ((NM - nm_centre) / (width / 2.355)) ** 2); return w / w.sum()


def xyz_of(spectrum, illuminant):
    """XYZ of a reflectance/transmittance under an illuminant, Y of the
    illuminant itself = 1."""
    k = 1.0 / (CMFS[:, 1] * illuminant).sum()
    return k * (CMFS * (illuminant * spectrum)[:, None]).sum(axis=0)


class Emulsion:
    """One film: layers with spectral sensitivities, dyes with spectra, and
    the curves that turn log exposure into density."""
    def __init__(self, stock, dyes_from=None, status=STATUS_M, curve_names=("blue", "green", "red")):
        # dyes_from lends its spectral sensitivities and dyes to a stock whose
        # sheet has none; the tone curves are always the stock's own.
        self.stock = stock; self.dyes = dyes_from or stock; self.status = status; self.layers = list(curve_names)
        self.sens = {l: 10 ** self.dyes.sensitivity(FORMING[LAYER_OF[l]]) for l in self.layers}
        self.dye = {d: self.dyes.dye(d) for d in ("yellow", "magenta", "cyan")}
        self.dmin = self.dyes.dmin()
        # Status density of a unit of each dye in each band: the crosstalk
        # matrix K, so that measured (B,G,R) densities = K @ dye amounts.
        dyes = ["yellow", "magenta", "cyan"]
        self.K = np.array([[-np.log10((band(status[l]) * 10 ** -self.dye[d]).sum()) for d in dyes] for l in self.layers])
        self.Kinv = np.linalg.inv(self.K)
    def calibrate_to(self, neutral_status, neutral_density):
        """Scale the bands so a unit of every dye measures what the sheet
        says its neutral measures: the print aims (B,G,R) at the neutral's
        density. Stands in for the real Status responsivities."""
        scale = np.asarray(neutral_status) * neutral_density / (self.K @ np.ones(3))
        self.K = self.K * scale[:, None]; self.Kinv = np.linalg.inv(self.K)
    def exposures(self, spectra, light):
        """Layer exposures (arbitrary units) of spectra under a light."""
        return np.stack([(spectra * (light * self.sens[l])[None, :]).sum(axis=1) for l in self.layers], axis=1)
    def densities(self, log_e):
        """Status densities per layer from log exposures (n x 3)."""
        return np.stack([self.stock.density(l, log_e[:, i]) for i, l in enumerate(self.layers)], axis=1)
    def transmittance(self, status_d):
        """Spectral transmittance for measured (B,G,R) densities: dye amounts
        via K⁻¹, then the dyes' spectra plus the base."""
        amounts = np.clip(status_d @ self.Kinv.T, 0, None)
        spectral = amounts @ np.stack([self.dye[d] for d in ("yellow", "magenta", "cyan")]) + self.dmin[None, :]
        return 10 ** -spectral


class PrintChain:
    def __init__(self, print_stock, negative="kodak-5219", calibrate=True):
        self.name = print_stock
        self.neg = Emulsion(Stock(negative), status=STATUS_M)
        ps = Stock(print_stock)
        stand_in = DYE_STAND_IN.get(print_stock)
        self.print = Emulsion(ps, dyes_from=Stock(stand_in) if stand_in else None, status=STATUS_A)
        self.lad = np.array([LAD[print_stock][2], LAD[print_stock][1], LAD[print_stock][0]])   # (B,G,R) order
        self.print.calibrate_to(self.lad, NEUTRAL_DENSITY.get(stand_in or print_stock, 1.0))
        self.two_point = print_stock in DYE_STAND_IN
        if calibrate: self._fit_camera(); self._calibrate()

    camera_light = CAMERA_LIGHT; view_light = XENON; grey_log_e = NEG_GREY_LOG_E

    # -- the camera: working RGB -> negative layer exposures ---------------
    def _fit_camera(self):
        """A 3x3 from linear Rec.2020 to layer exposures, fitted over the
        colour checker: the scene is white balanced, so a neutral exposes
        the three layers equally, whatever the film's own balance."""
        cc = colour.SDS_COLOURCHECKERS["ColorChecker N Ohta"]
        refl = np.stack([sd.copy().align(SHAPE).values for sd in cc.values()])
        xyz = np.stack([xyz_of(r, D65) for r in refl])                     # white-balanced capture, D65 white
        rgb = colour.XYZ_to_RGB(xyz, BT2020, illuminant=BT2020.whitepoint)
        expo = self.neg.exposures(refl, self.camera_light)                  # the film under its own light
        expo = expo / self.neg.exposures(np.ones((1, len(NM))), self.camera_light)   # a perfect white = 1 in every layer
        # least squares rgb @ M = expo, then force a neutral to stay neutral
        M, *_ = np.linalg.lstsq(rgb, expo, rcond=None)
        self.M = M / (np.ones(3) @ M)[None, :]
        self.camera_residual = float(np.abs(rgb @ self.M - expo).mean())

    def negative_transmittance(self, rgb):
        expo = np.clip(rgb @ self.M, 1e-6, None)
        log_e = np.log10(expo / GREY) + self.grey_log_e                      # 18% grey at the film's normal exposure
        return self.neg.transmittance(self.neg.densities(log_e))

    def printing_density(self, t_neg):
        """What the print film sees through the negative: per print layer."""
        seen = self.print.exposures(t_neg, PRINTER); open_gate = self.print.exposures(np.ones((1, len(NM))), PRINTER)
        return -np.log10(seen / open_gate)

    WHITE = 0.9      # a white card, for the second calibration point

    def _offsets_for_grey(self):
        """Printer lights: the offsets that put an 18% grey on the LAD aim."""
        apd = self.printing_density(self.negative_transmittance(np.array([[GREY, GREY, GREY]])))[0]
        self.offset = np.zeros(3)
        for i, l in enumerate(self.print.layers):
            c = self.print.stock.curves["sensitometric"][l]
            xs = np.linspace(c.x[0], c.x[-1], 2000); ys = np.maximum.accumulate(c(xs))   # monotone, so it inverts
            self.offset[i] = np.interp(self.lad[i], ys, xs) + apd[i] * self.gain[i]

    def _calibrate(self):
        """The lab's printer lights from an 18% grey. A stock running on
        borrowed spectra gets a second point as well: a white card prints
        in the aim's proportions too, by a density gain per layer, since
        with its own dyes the film was designed to hold neutrals neutral
        and the borrowed set cannot be trusted to."""
        self.gain = np.ones(3); self._offsets_for_grey()
        if not self.two_point: return
        white = np.array([[self.WHITE] * 3]); ratio = self.lad / self.lad[1]        # (B,G,R) relative to green
        for _ in range(6):
            d = self.print_densities(white)[0]
            for i in (0, 2):                                                       # blue and red follow green
                target = d[1] * ratio[i]
                lo, hi = 0.5, 1.5
                for _ in range(40):                                                # bisection on this layer's gain
                    mid = (lo + hi) / 2; g = self.gain.copy(); g[i] = mid; saved = self.gain; self.gain = g
                    self._offsets_for_grey(); di = self.print_densities(white)[0][i]; self.gain = saved
                    if di > target: lo = mid     # more gain, less print density at white
                    else: hi = mid
                self.gain[i] = (lo + hi) / 2
            self._offsets_for_grey()

    # Subtractive density controls, on the negative side of the print: a
    # printer-light trim per layer (log exposure, + is more light so less
    # density) and a density gain per layer (1 = as printed). Both act on
    # the printing density the print film sees, so they sit between the two
    # tables the engine will carry (scene -> printing density -> screen).
    trim = np.zeros(3); gain = np.ones(3)      # per instance after calibration

    def print_log_exposure(self, apd):
        return -(apd * self.gain[None, :]) + self.offset[None, :] + self.trim[None, :]

    def print_densities(self, rgb):
        t_neg = self.negative_transmittance(np.asarray(rgb, dtype=float).reshape(-1, 3))
        return self.print.densities(self.print_log_exposure(self.printing_density(t_neg)))

    def render(self, rgb):
        """Scene-linear Rec.2020 -> display-linear Rec.2020, clear base = white."""
        t_print = self.print.transmittance(self.print_densities(rgb))
        xyz = np.stack([xyz_of(t, self.view_light) for t in t_print])
        white = xyz_of(10 ** -self.print.dmin, self.view_light)
        xyz = xyz / white[1]
        xyz = colour.adaptation.chromatic_adaptation_VonKries(xyz, white / white[1], colour.xy_to_XYZ(BT2020.whitepoint), transform="Bradford")
        return colour.XYZ_to_RGB(xyz, BT2020, illuminant=BT2020.whitepoint).reshape(np.shape(rgb))


def report(name, out_dir):
    import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
    os.makedirs(out_dir, exist_ok=True)
    ch = chain_for(name)
    stops = np.linspace(-6, 4, 41); grey = GREY * 2.0 ** stops
    d = ch.print_densities(np.stack([grey, grey, grey], axis=1)); out = ch.render(np.stack([grey, grey, grey], axis=1))
    fig, ax = plt.subplots(1, 2, figsize=(11, 4.2))
    for i, l in enumerate(("blue", "green", "red")): ax[0].plot(stops, d[:, i], color=l, label=f"print density {l}")
    ax[0].axhline(ch.lad[1], color="grey", lw=0.5); ax[0].axvline(0, color="grey", lw=0.5); ax[0].set_xlabel("scene stops from 18% grey"); ax[0].set_ylabel("Status A density"); ax[0].legend(); ax[0].grid(True, lw=0.3)
    for i, l in enumerate(("red", "green", "blue")): ax[1].plot(stops, np.log2(np.clip(out[:, i], 1e-6, None)), color=l, label=f"screen {l}")
    ax[1].plot(stops, stops - np.log2(1 / GREY) + np.log2(1 / GREY) * 0, "k--", lw=0.6, label="scene")
    ax[1].set_xlabel("scene stops from 18% grey"); ax[1].set_ylabel("log2 screen light (white = 0)"); ax[1].legend(); ax[1].grid(True, lw=0.3)
    fig.suptitle(f"{name}: grey ramp; camera fit residual {ch.camera_residual:.3f}; printer offsets {np.round(ch.offset, 2)}")
    ax[0].set_ylabel("print density")
    fig.savefig(os.path.join(out_dir, f"{name}-grey.png"), dpi=110, bbox_inches="tight"); plt.close(fig)
    # a colour checker, scene vs print, as sRGB swatches
    cc = colour.SDS_COLOURCHECKERS["ColorChecker N Ohta"]
    refl = np.stack([sd.copy().align(SHAPE).values for sd in cc.values()])
    xyz = np.stack([xyz_of(r, D65) for r in refl]); rgb = colour.XYZ_to_RGB(xyz, BT2020, illuminant=BT2020.whitepoint) * 0.18 / 0.18
    scene = rgb * (GREY / rgb[21, 1])                                       # patch 22 (neutral 5) = 18% grey
    printed = ch.render(scene)
    def to_srgb(lin): return np.clip(colour.RGB_to_RGB(np.clip(lin, 0, None), BT2020, colour.RGB_COLOURSPACES["sRGB"], apply_cctf_encoding=True), 0, 1)
    # the scene shown through a plain display transform: simple 2.2-ish via sRGB, grey mapped to 0.18 * 4
    s_scene = to_srgb(scene * 4.0); s_print = to_srgb(printed)
    tile = np.zeros((4 * 2 * 40, 6 * 40, 3))
    for k in range(24):
        r, c = divmod(k, 6); tile[r*80:r*80+40, c*40:(c+1)*40] = s_scene[k]; tile[r*80+40:r*80+80, c*40:(c+1)*40] = s_print[k]
    plt.imsave(os.path.join(out_dir, f"{name}-checker.png"), tile)
    g0 = int(np.argmin(np.abs(stops))); inner = (stops >= -4) & (stops <= 3)
    neutral_dev = np.abs(np.log2(np.clip(out[inner, 0], 1e-6, None)) - np.log2(np.clip(out[inner, 2], 1e-6, None))).max()
    print(f"{name}: offsets {np.round(ch.offset, 3)}, grey density (B,G,R) {np.round(d[g0], 3)} vs LAD {ch.lad}, R/B drift over -4..+3 stops {neutral_dev:.2f} stops, screen grey {np.round(out[g0], 3)}, white {np.round(out[-1], 3)}")




# -- baking and previews ------------------------------------------------------
LOG_MIN, LOG_MAX = -6.0, 6.0        # log2 of scene value / 0.18: 24 stops around grey, 0.375-stop nodes at 33


def shaper(v):
    """Scene-linear -> table coordinate 0..1: log2 around 18% grey."""
    return (np.log2(np.clip(v, 1e-6, None) / GREY) - LOG_MIN) / (LOG_MAX - LOG_MIN)


def unshaper(t):
    return GREY * 2.0 ** (LOG_MIN + t * (LOG_MAX - LOG_MIN))


def bake(name, out_path, size=33):
    """A size³ table over the log-shaped scene-linear Rec.2020 cube, values
    display-linear Rec.2020, written as a .cube with the shaper in the header."""
    ch = chain_for(name); t = np.linspace(0, 1, size)
    b, g, r = np.meshgrid(t, t, t, indexing="ij")                  # .cube order: red fastest
    grid = np.stack([r.ravel(), g.ravel(), b.ravel()], axis=1)
    out = ch.render(unshaper(grid))
    with open(out_path, "w") as f:
        f.write(f"TITLE \"OmaRAW print stock {name}\"\n# input: scene-linear Rec.2020 through shaper t = (log2(v/0.18) - {LOG_MIN}) / {LOG_MAX - LOG_MIN}\n")
        f.write(f"# output: display-linear Rec.2020, clear base = 1.0\nLUT_3D_SIZE {size}\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n")
        for row in out: f.write(f"{row[0]:.6f} {row[1]:.6f} {row[2]:.6f}\n")
    return ch


def preview(names, image_path, out_path, tile=360):
    """A contact sheet: one JPEG photograph, decoded as if it were the scene
    (sRGB -> linear, 18% grey assumed at sRGB 0.46), through each stock."""
    from PIL import Image
    im = Image.open(image_path).convert("RGB"); im.thumbnail((tile, tile))
    srgb = np.asarray(im).astype(float) / 255.0
    lin = colour.RGB_to_RGB(colour.models.eotf_sRGB(srgb), colour.RGB_COLOURSPACES["sRGB"], BT2020)
    scene = lin * (GREY / 0.18)                                      # an sRGB mid grey is the scene's 18% grey
    tiles = [colour.models.eotf_inverse_sRGB(np.clip(colour.RGB_to_RGB(scene * 0.9, BT2020, colour.RGB_COLOURSPACES["sRGB"]), 0, 1))]
    labels = ["scene (plain)"]
    for name in names:
        ch = chain_for(name); out = ch.render(scene.reshape(-1, 3)).reshape(scene.shape)
        tiles.append(colour.models.eotf_inverse_sRGB(np.clip(colour.RGB_to_RGB(out, BT2020, colour.RGB_COLOURSPACES["sRGB"]), 0, 1))); labels.append(name)
    h, w = tiles[0].shape[:2]; cols = 3; rows = (len(tiles) + cols - 1) // cols
    sheet = np.ones((rows * (h + 24), cols * (w + 8), 3))
    from PIL import ImageDraw
    for k, t in enumerate(tiles):
        rr, cc = divmod(k, cols); sheet[rr*(h+24)+20:rr*(h+24)+20+h, cc*(w+8):cc*(w+8)+w] = t
    img = Image.fromarray((sheet * 255).astype(np.uint8)); d = ImageDraw.Draw(img)
    for k, l in enumerate(labels):
        rr, cc = divmod(k, cols); d.text((cc*(w+8)+4, rr*(h+24)+4), l, fill=(0, 0, 0))
    img.save(out_path); print("wrote", out_path)


# -- black and white ------------------------------------------------------------
BW_AIM = 1.0            # a neutral 18% grey prints to a visual density of 1.0, as the colour stocks do


class BWPrintChain(PrintChain):
    """A blue-sensitive black-and-white print struck from the colour negative:
    one layer, one curve, neutral on screen. Its rendering of skin and sky
    comes from how the negative's dyes transmit to a blue-sensitive emulsion."""
    def __init__(self, print_stock="kodak-2302", negative="kodak-5219", curve="7min", calibrate=True):
        self.name = print_stock
        self.neg = Emulsion(Stock(negative), status=STATUS_M)
        self.stock = Stock(print_stock); self.curve = self.stock.curves["sensitometric"][curve]
        self.sens = 10 ** self.stock.sensitivity("blue-sensitive")
        self.lad = np.array([BW_AIM, BW_AIM, BW_AIM])
        if calibrate: self._fit_camera(); self._calibrate()
    def printing_density(self, t_neg):
        seen = (t_neg * (PRINTER * self.sens)[None, :]).sum(axis=1); open_gate = (PRINTER * self.sens).sum()
        return -np.log10(seen / open_gate)[:, None]
    def _calibrate(self):
        apd = self.printing_density(self.negative_transmittance(np.array([[GREY, GREY, GREY]])))[0, 0]
        xs = np.linspace(self.curve.x[0], self.curve.x[-1], 2000); ys = np.maximum.accumulate(self.curve(xs))
        self.offset = np.array([np.interp(BW_AIM, ys, xs) + apd])
    def print_densities(self, rgb):
        t_neg = self.negative_transmittance(np.asarray(rgb, dtype=float).reshape(-1, 3))
        d = self.curve(-self.printing_density(t_neg)[:, 0] + self.offset[0])
        return np.stack([d, d, d], axis=1)
    def render(self, rgb):
        d = self.print_densities(rgb)[:, 0]
        base = float(self.curve.y.min())                                 # clear base and fog = white
        light = 10 ** -(d - base)
        return np.stack([light, light, light], axis=1).reshape(np.shape(rgb))


def chain_for(name, calibrate=True):
    """A chain by stock name. `calibrate=False` leaves the set-up to the
    caller (revised() does its own, once)."""
    if name == "kodak-2383-v3":
        # Correct only the characteristic-curve axis digitisation. Keep the
        # earlier curves/tables intact: their IDs are stored in photo history.
        ch = PrintChain("kodak-2383", calibrate=False)
        ch.print.stock.curves["sensitometric"] = Stock(name).curves["sensitometric"]
        ch.name = name
        return revised(ch)
    if name.endswith(REVISED):
        first = name[:-len(REVISED)]
        # The second version is the cinema model's correction; a still
        # negative's chain has no dyes of its own to rebuild from.
        if first in STILLS: raise ValueError(f"{name}: the still stocks have a single version")
        return revised(chain_for(first, calibrate=False))
    if name in STILLS: return StillChain(name)
    return BWPrintChain(name, calibrate=calibrate) if name == "kodak-2302" else PrintChain(name, calibrate=calibrate)


# -- the cinema stocks, second version ------------------------------------------
# The first cinema tables held each sensitivity at its end value beyond the
# span its sheet draws, and counted the 5219's base twice (its curves include
# the base, and the base spectrum went on again). The revised chain takes the
# base off once, as the still negatives do, and sees nothing outside the
# drawn spans; it is then set up again at the same grey and white aims. It
# ships as new stocks ("<stock>-v2") so photos edited on the first tables
# keep their look (docs/PRINT-STOCKS.md, section 12).
REVISED = "-v2"


def revised(ch, bounded=True, base_once=True):
    if base_once:
        negative = ch.neg.stock
        ch.neg = NegativeEmulsion(negative, negative, STATUS_M)
    if bounded:
        zero_outside(ch.neg, ch.neg.stock)
        if isinstance(ch, BWPrintChain):
            lo, hi = ch.stock.curves["sensitivity"]["blue-sensitive"].span
            ch.sens = np.where((NM >= lo) & (NM <= hi), ch.sens, 0.0)
        else:
            zero_outside(ch.print, ch.print.dyes)
    ch._fit_camera(); ch._calibrate()
    return ch


# -- still photography: a C-41 negative printed on RA-4 paper -------------------
# Each negative's own sheet gives its characteristic curves, its spectral
# sensitivities, its minimum density (the orange mask) and the spectral
# density of a midscale neutral, but not its three dyes one by one. Those are
# stood in for by Vision3 5219's (the Portra and Ektar sheets say they
# incorporate Kodak's Vision film technology; that is not a statement that
# the dyes are the same), each shifted, widened or narrowed and scaled so
# that together they rebuild this stock's own published neutral over the
# span the sheet draws. The fit and its residual are recorded.
#
# The prints are on the two papers of Kodak's E-4021 sheet, which gives
# their curves and their three dyes: Portra Endura, the lower-contrast
# portrait paper made for the Portra films, and Supra Endura, the more
# saturated professional paper, for Ektar, Gold and UltraMax. (The current
# Endura Premier is steeper still: printed from these negatives it gives a
# system contrast of about 2.2 through the midtones, against about 1.6 on
# Portra Endura.) Prints are seen under 5000 K, the ANSI PH2.30 viewing
# light Kodak's paper sheets cite.
STILLS = {   # table name: (negative, paper); each negative's Log H Ref comes from its SOURCE.md
    "kodak-portra-160-endura": ("kodak-portra-160", "kodak-portra-endura"),
    "kodak-portra-400-endura": ("kodak-portra-400", "kodak-portra-endura"),
    "kodak-portra-800-endura": ("kodak-portra-800", "kodak-portra-endura"),
    "kodak-ektar-100-endura": ("kodak-ektar-100", "kodak-supra-endura"),
    "kodak-gold-200-endura": ("kodak-gold-200", "kodak-supra-endura"),
    "kodak-ultramax-400-endura": ("kodak-ultramax-400", "kodak-supra-endura"),
}
DYE_DONOR = "kodak-5219"
# 18% grey prints as an 18% grey: reflection density -log10(0.18) in each
# layer; the white card's second point then holds the paper's neutrals.
PAPER_AIM = -np.log10(GREY)
DAYLIGHT = colour.SDS_ILLUMINANTS["D55"].copy().align(SHAPE).values    # the sheets' daylight exposure
PAPER_VIEW = colour.SDS_ILLUMINANTS["D50"].copy().align(SHAPE).values  # 5000 K print viewing (ANSI PH2.30)
SHIFT = 20                                                             # nm either way a borrowed dye may move


def log_h_ref(stock):
    """The sheet's Log H of a normal exposure, as its SOURCE.md records it
    ("Log H Ref: -1.44")."""
    import re
    text = open(os.path.join(stock.dir, "SOURCE.md")).read()
    m = re.search(r"Log H Ref\D{0,20}?(-?\d+\.\d+)", text)
    if not m: raise ValueError(f"{stock.name}: no Log H Ref in SOURCE.md")
    return float(m.group(1))


class FittedDyes:
    """A negative's own sensitivities and base, with the donor's three dyes
    fitted to its own neutral."""
    def __init__(self, stock, donor):
        self.stock, self.donor = stock, donor
        target = stock.dye("midscale-neutral") - stock.dmin()
        lo, hi = stock.curves["dyes"]["midscale-neutral"].span
        inside = (NM >= lo) & (NM <= hi)                  # judged only where the sheet draws
        names = ("yellow", "magenta", "cyan")
        # Each dye may move and widen or narrow about its own peak.
        def shaped(n, shift, width):
            peak = NM[np.argmax(donor.dye(n))]
            return donor.dye(n, peak + (NM - peak - shift) / width)
        shifts = range(-SHIFT, SHIFT + 1, 2); widths = (0.85, 0.9, 0.95, 1.0, 1.05, 1.1, 1.15)
        options = [(s, w) for s in shifts for w in widths]
        cache = {(n, o): shaped(n, *o) for n in names for o in options}
        def score(choice):
            basis = np.stack([cache[(n, o)] for n, o in zip(names, choice)], axis=1)
            amounts, *_ = np.linalg.lstsq(basis[inside], target[inside], rcond=None)
            if (amounts <= 0).any(): return None
            return float(np.sqrt(np.mean((basis[inside] @ amounts - target[inside]) ** 2))), tuple(choice), amounts
        # Coordinate descent (one dye's shape at a time) from a coarse grid
        # of starts; the best of all descents is kept.
        best = None
        for start in [(a, b, c) for a in (-SHIFT, 0, SHIFT) for b in (-SHIFT, 0, SHIFT) for c in (-SHIFT, 0, SHIFT)]:
            choice = [(v, 1.0) for v in start]; here = score(choice)
            if here is None: continue
            moved = True
            while moved:
                moved = False
                for k in range(3):
                    for o in options:
                        trial = list(choice); trial[k] = o; r = score(trial)
                        if r is not None and r[0] < here[0] - 1e-9: here, choice, moved = r, trial, True
            if best is None or here[0] < best[0]: best = here
        self.residual, self.shapes, self.amounts = best
        self.curves = {n: cache[(n, o)] * a for n, o, a in zip(names, self.shapes, self.amounts)}
        self.peak = float(target[inside].max())
    def sensitivity(self, layer, nm=NM): return self.stock.sensitivity(layer, nm)
    def dye(self, d, nm=NM): return np.interp(nm, NM, self.curves[d])
    def dmin(self, nm=NM): return self.stock.dmin(nm)


class NegativeEmulsion(Emulsion):
    """A negative whose characteristic curves include its base: the base's
    own Status density comes off before the dyes are solved for, and the
    base goes back as its measured spectrum, so the mask counts once."""
    def __init__(self, stock, dyes_from, status):
        super().__init__(stock, dyes_from=dyes_from, status=status)
        self.base_status = np.array([-np.log10((band(status[l]) * 10 ** -self.dmin).sum()) for l in self.layers])
    def transmittance(self, status_d):
        amounts = np.clip((status_d - self.base_status[None, :]) @ self.Kinv.T, 0, None)
        spectral = amounts @ np.stack([self.dye[d] for d in ("yellow", "magenta", "cyan")]) + self.dmin[None, :]
        return 10 ** -spectral


def zero_outside(emulsion, provider):
    """Sensitivity outside a curve's plotted span is none at all, not its
    end value held: a sheet stops drawing where the layer stops seeing."""
    for l in emulsion.layers:
        c = provider.curves["sensitivity"][FORMING[LAYER_OF[l]]]
        lo, hi = c.span
        emulsion.sens[l] = np.where((NM >= lo) & (NM <= hi), emulsion.sens[l], 0.0)


class StillChain(PrintChain):
    camera_light = DAYLIGHT; view_light = PAPER_VIEW
    def __init__(self, name):
        self.name = name
        negative, paper = STILLS[name]
        neg = Stock(negative)
        self.fitted = FittedDyes(neg, Stock(DYE_DONOR))
        self.neg = NegativeEmulsion(neg, self.fitted, STATUS_M)
        zero_outside(self.neg, neg)
        self.grey_log_e = log_h_ref(neg)
        self.print = Emulsion(Stock(paper), status=STATUS_A)     # its own three dyes: no calibration of the bands
        zero_outside(self.print, self.print.stock)
        self._neutral_black()
        self.lad = np.array([PAPER_AIM] * 3)
        self.two_point = False                                     # the printer set-up below replaces it
        self._fit_camera(); self._calibrate()

    def _neutral_black(self):
        """The paper's maximum black, read on its own Status A curves, is a
        visual neutral on the real paper; the bands standing in for Status A
        are not, and would print it a stop blue. Scale the blue and red
        bands' readings (green fixed) so the curves' Dmax prints neutral
        under the viewing light: the same kind of correction the cinema
        print stocks get from their neutral."""
        from scipy.optimize import least_squares
        dmax = np.array([self.print.stock.curves["sensitometric"][l].y.max() for l in self.print.layers])
        white = xyz_of(np.ones(len(NM)), self.view_light); white = white / white.sum()
        K0 = self.print.K.copy()
        def residual(x):
            self.print.K = K0 * np.array([x[0], 1.0, x[1]])[:, None]; self.print.Kinv = np.linalg.inv(self.print.K)
            xyz = xyz_of(self.print.transmittance(dmax[None, :])[0], self.view_light)
            return (xyz / xyz.sum())[:2] - white[:2]
        fit = least_squares(residual, [1.0, 1.0])
        residual(fit.x); self.black_scale = fit.x

    def _calibrate(self):
        """As a lab sets up a printer with Kodak's printer control negatives
        (very under, under, normal, over, very over; E-4021 names them): the
        three printer lights for balance, so an 18% grey card prints as a
        neutral 18% grey, and the blue and red slope (density gains) so the
        exposures either side print neutral too, from three stops under to
        three over, as seen under the paper's viewing light. Status A
        densities, stood in for by bands, start it off."""
        from scipy.optimize import least_squares
        super()._calibrate()
        start = np.concatenate([self.offset, self.gain[[0, 2]]])
        stops = np.array([-3.0, -2.0, -1.0, 1.0, 2.0, 3.0])
        cards = np.concatenate([[[GREY] * 3], np.repeat((GREY * 2.0 ** stops)[:, None], 3, axis=1)])
        def residual(x):
            self.offset = x[:3]; self.gain = np.array([x[3], 1.0, x[4]])
            out = self.render(cards)
            ramp = np.log2(np.clip(out[1:], 1e-6, None))
            return np.concatenate([4 * np.log2(out[0] / GREY), (ramp[:, 0] - ramp[:, 1]), (ramp[:, 2] - ramp[:, 1])])
        fit = least_squares(residual, start, x_scale=[0.1, 0.1, 0.1, 0.05, 0.05])
        residual(fit.x)
        self.neutral_error = float(np.abs(fit.fun[:3]).max() / 4)


# -- the engine's tables ---------------------------------------------------------
# OMPT version 3. Every steep, per-layer step is a 1-D curve kept exact; the
# smooth colour steps are 3-D cubes. In order, per pixel:
#   M          3x3, scene-linear Rec.2020 (R,G,B) -> negative layer exposures (R,G,B layers)
#   A2         three curves: log exposure over NEG_LOG_MIN..MAX -> negative density / NEG_D_MAX
#   A3         cube: negative densities -> printing density / APD_MAX          (then trims, gains)
#   B1         three curves: printing density -> print density / PRINT_D_MAX
#   B2         cube: print densities -> log2 screen light over LOG_LIGHT_MIN..0
APD_MAX = 4.0; PRINT_D_MAX = 6.0; NEG_D_MAX = 3.5; NEG_LOG_MIN, NEG_LOG_MAX = -5.0, 2.0
LOG_LIGHT_MIN = -16.0; CURVE_N = 1024


def bake_pair(name, out_path, size=33):
    ch = chain_for(name); t = np.linspace(0, 1, size); u = np.linspace(0, 1, CURVE_N)
    b, g, r = np.meshgrid(t, t, t, indexing="ij"); grid = np.stack([r.ravel(), g.ravel(), b.ravel()], axis=1)
    # M in R,G,B layer order (the chain keeps layers as blue, green, red)
    M = ch.M[:, [2, 1, 0]]
    log_e = NEG_LOG_MIN + u * (NEG_LOG_MAX - NEG_LOG_MIN)
    a2 = np.stack([ch.neg.stock.density(l, log_e) for l in ("red", "green", "blue")], axis=1)
    dens_bgr = grid[:, [2, 1, 0]] * NEG_D_MAX
    a3 = ch.printing_density(ch.neg.transmittance(dens_bgr))
    if isinstance(ch, BWPrintChain):
        a3 = np.repeat(a3, 3, axis=1); dcurve = ch.curve(-(u * APD_MAX) + ch.offset[0]); b1 = np.stack([dcurve] * 3, axis=1)
        light = 10 ** -(grid[:, 0] * PRINT_D_MAX - float(ch.curve.y.min())); b2 = np.stack([light] * 3, axis=1)
    else:
        a3 = a3[:, [2, 1, 0]]
        b1 = ch.print.densities(ch.print_log_exposure(np.stack([u * APD_MAX] * 3, axis=1)))[:, [2, 1, 0]]
        t_print = ch.print.transmittance(grid[:, [2, 1, 0]] * PRINT_D_MAX)
        xyz = np.stack([xyz_of(tp, ch.view_light) for tp in t_print]); white = xyz_of(10 ** -ch.print.dmin, ch.view_light); xyz = xyz / white[1]
        xyz = colour.adaptation.chromatic_adaptation_VonKries(xyz, white / white[1], colour.xy_to_XYZ(BT2020.whitepoint), transform="Bradford")
        b2 = colour.XYZ_to_RGB(xyz, BT2020, illuminant=BT2020.whitepoint)
    A2 = np.clip(a2 / NEG_D_MAX, 0, 1); A3 = np.clip(a3 / APD_MAX, 0, 1); B1 = np.clip(b1 / PRINT_D_MAX, 0, 1)
    B2 = np.clip((np.log2(np.clip(b2, 2.0 ** LOG_LIGHT_MIN, None)) - LOG_LIGHT_MIN) / -LOG_LIGHT_MIN, 0, 1)
    with open(out_path, "wb") as f:
        f.write(b"OMPT"); f.write(np.array([3, size, CURVE_N], dtype="<u4").tobytes())
        f.write(np.array([GREY, ch.grey_log_e, NEG_LOG_MIN, NEG_LOG_MAX, NEG_D_MAX, APD_MAX, PRINT_D_MAX, LOG_LIGHT_MIN], dtype="<f4").tobytes())
        f.write(M.astype("<f4").tobytes())
        for arr in (A2, A3, B1, B2): f.write(np.round(arr * 65535).astype("<u2").tobytes())
    return M, A2, A3, B1, B2


def read_tables(path):
    raw = open(path, "rb").read(); assert raw[:4] == b"OMPT"
    version, size, n = np.frombuffer(raw[4:16], dtype="<u4"); assert version in (3, 4)
    consts = np.frombuffer(raw[16:48], dtype="<f4"); M = np.frombuffer(raw[48:84], dtype="<f4").reshape(3, 3); o = 84
    assert (version == 4 and consts[7] == 0) or (version == 3 and consts[7] < 0)
    def take(count):
        nonlocal o; arr = np.frombuffer(raw[o:o + count * 2], dtype="<u2").astype(float).reshape(-1, 3) / 65535; o += count * 2; return arr
    return int(size), int(n), consts, M, take(n * 3), take(size ** 3 * 3), take(n * 3), take(size ** 3 * 3)


def trilinear(table, size, t):
    t = np.clip(t, 0, 1) * (size - 1); i = np.floor(t).astype(int); i = np.minimum(i, size - 2); f = t - i
    def at(dr, dg, db): return table[((i[:, 2] + db) * size + (i[:, 1] + dg)) * size + (i[:, 0] + dr)]
    c00 = at(0, 0, 0) * (1 - f[:, :1]) + at(1, 0, 0) * f[:, :1]; c10 = at(0, 1, 0) * (1 - f[:, :1]) + at(1, 1, 0) * f[:, :1]
    c01 = at(0, 0, 1) * (1 - f[:, :1]) + at(1, 0, 1) * f[:, :1]; c11 = at(0, 1, 1) * (1 - f[:, :1]) + at(1, 1, 1) * f[:, :1]
    c0 = c00 * (1 - f[:, 1:2]) + c10 * f[:, 1:2]; c1 = c01 * (1 - f[:, 1:2]) + c11 * f[:, 1:2]
    return c0 * (1 - f[:, 2:3]) + c1 * f[:, 2:3]


def curves1d(curves, n, u):
    x = np.clip(u, 0, 1) * (n - 1); i = np.minimum(np.floor(x).astype(int), n - 2); f = x - i
    return np.stack([curves[i[:, c], c] * (1 - f[:, c]) + curves[i[:, c] + 1, c] * f[:, c] for c in range(3)], axis=1)


def apply_tables(path, rgb, trim=(0, 0, 0), gain=(1, 1, 1)):
    """What the engine does: M -> A2 -> A3 -> trims -> B1 -> B2."""
    size, n, (grey, grey_log_e, nl_min, nl_max, nd_max, apd_max, d_max, ll_min), M, A2, A3, B1, B2 = read_tables(path)
    expo = np.clip(np.asarray(rgb, dtype=float).reshape(-1, 3) @ M, 1e-6, None)
    log_e = np.log10(expo / grey) + grey_log_e
    dens = curves1d(A2, n, (log_e - nl_min) / (nl_max - nl_min)) * nd_max
    apd = trilinear(A3, size, dens / nd_max) * apd_max
    apd = apd * np.asarray(gain)[None, :] - np.asarray(trim)[None, :]
    pdens = curves1d(B1, n, apd / apd_max) * d_max
    if ll_min == 0:  # OMPT4: convex interpolation of linear display Rec.709
        return colour.RGB_to_RGB(trilinear(B2, size, pdens / d_max), colour.RGB_COLOURSPACES["sRGB"], BT2020)
    return 2.0 ** (ll_min + trilinear(B2, size, pdens / d_max) * -ll_min)


if __name__ == "__main__":
    for name in sys.argv[2:] or ["kodak-2383"]: report(name, sys.argv[1])
