# Kodak Professional Supra Endura Paper

- Publication E-4021, "Kodak Professional Portra Endura Paper and Kodak Professional Supra Endura
  Paper", September 2008 (revised 9-08, Eastman Kodak Company). Public copy:
  https://125px.com/docs/paper/kodak/e4021.pdf (a third-party mirror of the Kodak sheet)
  — page 7 (lower characteristic-curve plot, figure F002_1275AC), page 8 (spectral sensitivity and
  spectral dye density, figure E4021D).
- Spectral data shared with Portra Endura: the sheet prints one spectral-sensitivity plot and one
  spectral-dye-density plot for both papers, so `sensitivity-*.csv` and `dyes-*.csv` here are
  identical copies of those in the Portra Endura directory.
- Curves extracted as vector paths with `tools/film/extract-curves.py`.
- `sensitometric-*.csv`: Status A (reflection) density vs log exposure (lux-seconds), exposure
  0.5 s, Process RA-4 at 35 °C (95 °F), 45 s. The exposing illuminant is not stated. Drawn from
  -3.005 to -0.130. The sheet describes it as slightly higher in contrast and colour saturation than Portra Endura; the curves drawn bend over slightly past -0.3 log exposure, kept as drawn. Paper-white D-min is the toe of these curves (about 0.10).
  The curves are drawn as short joined segments; each colour is one run of consecutive segments,
  checked continuous end to end. Red, green and blue follow the sheet's R, G, B labels.
  Axis: the exposure labels are printed with overbars (3.0, 2.0, 1.0 with a bar = -3, -2, -1)
  and run -3.0 to 0.0 across the frame; x is calibrated from the frame. Density is calibrated
  from the 1.0 and 2.0 ticks, which sit about 1 pt below the drawn frame line.  On this plot the -1.0 tick and its label sit about 0.25 log exposure too far right of a linear axis; the frame and the other ticks agree, so the misplaced tick is ignored.
- `sensitivity-*.csv`: log sensitivity (reciprocal of the exposure in erg/cm² for the specified
  density) vs wavelength, effective exposure 0.5 s, Process RA-4; axis -2.0 to 2.0 over 250 to
  750 nm. The sheet does not state the density at which sensitivity is measured. Each layer is a
  single path: yellow-forming 379-510 nm, magenta-forming 389-585 nm (its short-wavelength
  part, 389-470 nm, is drawn as part of the same curve), cyan-forming 495-740 nm. The cyan-forming
  curve has no separate blue lobe on this sheet.
- `dyes-*.csv`: diffuse spectral density vs wavelength, 400-700 nm, Process RA-4; yellow,
  magenta and cyan each peak-normalised to 1.0 as drawn. The sheet gives no D-min spectrum and no
  neutral: the low curve along the bottom of the plot at the short-wavelength end is the tail of
  the cyan dye curve (about 0.12 at 400 nm), not a separate D-min curve.
- Viewing conditions named by the sheet: a source of 5000 K ± 1000, CRI 85-100, at least
  538 lux (50 footcandles), per ANSI PH2.30-1989.
- Kodak, Kodak Professional, Portra, Supra and Endura are trademarks of Eastman Kodak Company.
  The sheet is not redistributed.
