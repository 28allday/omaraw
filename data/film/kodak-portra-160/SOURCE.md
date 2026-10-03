# Kodak Professional Portra 160 Film

- Publication E-4051, January 2025 (revised 1-25) (Kodak Alaris Inc.), https://kodakprofessional.com/sites/default/files/2025-07/e4051.pdf
  — page 4 (characteristic curves, spectral sensitivity, spectral dye density).
- The February 2016 edition carries the same curve paths.
- Curves extracted as vector paths with `tools/film/extract-curves.py`; each axis calibrated
  from the plot frame and gridlines/ticks (tick-label centres are offset by their minus signs).

- `sensitometric-*.csv`: Status M density vs log exposure (lux-seconds), daylight exposure,
  Process C-41, D-min included as drawn. **Log H Ref: -1.051** (the sheet's normal-exposure
  log H).
- `sensitivity-*.csv`: log sensitivity (sensitivity = reciprocal of the exposure in erg/cm²
  for the specified density) vs wavelength; daylight, effective exposure 1/50 s, Status M,
  density 0.2 above D-min.
- `dyes-*.csv`: diffuse spectral density vs wavelength, Process C-41: typical densities for a
  midscale neutral subject (`midscale-neutral`, D-min included) and the minimum density
  (`minimum-density`). The sheet gives no separate cyan, magenta and yellow dye curves.
- Axis and drawing notes: The spectral-sensitivity y axis has its 2.0 label misprinted as "20" and runs from -1.0 to 3.0; calibrated from the gridlines (-1.0 frame bottom, 0.0, 1.0, 2.0, 3.0 frame top), not from that label.
- Kodak, Kodak Professional, Portra, Ektar, Gold, Ultra, Max, Endura and Vision are trademarks
  used by Kodak Alaris under licence from Eastman Kodak Company. The sheet is not redistributed.
