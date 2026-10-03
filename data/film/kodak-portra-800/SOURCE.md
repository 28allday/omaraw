# Kodak Professional Portra 800 Film

- Publication E-4040, January 2025 (revised 1-25) (Kodak Alaris Inc.), https://kodakprofessional.com/sites/default/files/2025-07/e4040.pdf
  — page 4 (characteristic curves, spectral sensitivity), page 5 (spectral dye density).
- The February 2016 edition shows the same curves. Only the EI 800 (normal process) characteristic curves are taken; the sheet also gives EI 1600 (push 1) and EI 3200 (push 2).
- Curves extracted as vector paths with `tools/film/extract-curves.py`; each axis calibrated
  from the plot frame and gridlines/ticks (tick-label centres are offset by their minus signs).

- `sensitometric-*.csv`: Status M density vs log exposure (lux-seconds), daylight exposure,
  Process C-41, D-min included as drawn. **Log H Ref: -1.74** (the sheet's normal-exposure
  log H).
- `sensitivity-*.csv`: log sensitivity (sensitivity = reciprocal of the exposure in erg/cm²
  for the specified density) vs wavelength; daylight, effective exposure 1/200 s, Status M,
  density 0.2 above D-min.
- `dyes-*.csv`: diffuse spectral density vs wavelength, Process C-41: typical densities for a
  midscale neutral subject (`midscale-neutral`, D-min included) and the minimum density
  (`minimum-density`). The sheet gives no separate cyan, magenta and yellow dye curves.
- Axis and drawing notes: The EI 800 characteristic plot's exposure labels are out of order (-4.0, -2.0, -3.0, -1.0, 0.0, 1.0 left to right at even spacing); the axis is calibrated from the frame, -4.0 at the left edge and 1.0 at the right, which gives the true -4, -3, -2, -1, 0, 1 sequence (the push plots on the same page are labelled that way).
- Kodak, Kodak Professional, Portra, Ektar, Gold, Ultra, Max, Endura and Vision are trademarks
  used by Kodak Alaris under licence from Eastman Kodak Company. The sheet is not redistributed.
