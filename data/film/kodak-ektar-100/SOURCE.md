# Kodak Professional Ektar 100 Film

- Publication E-4046, January 2025 (revised 1/25) (Kodak Alaris Inc.), https://kodakprofessional.com/sites/default/files/2025-07/e4046.pdf
  — page 4 (characteristic curves, spectral sensitivity, spectral dye density).
- The February 2016 edition carries the same curve paths.
- Curves extracted as vector paths with `tools/film/extract-curves.py`; each axis calibrated
  from the plot frame and gridlines/ticks (tick-label centres are offset by their minus signs).

- `sensitometric-*.csv`: Status M density vs log exposure (lux-seconds), daylight exposure,
  Process C-41, D-min included as drawn. **Log H Ref: -0.84** (the sheet's normal-exposure
  log H).
- `sensitivity-*.csv`: log sensitivity (sensitivity = reciprocal of the exposure in erg/cm²
  for the specified density) vs wavelength; daylight, effective exposure 1/25 s, Status M,
  density 0.2 above D-min.
- `dyes-*.csv`: diffuse spectral density vs wavelength, Process C-41: typical densities for a
  midscale neutral subject (`midscale-neutral`, D-min included) and the minimum density
  (`minimum-density`). The sheet gives no separate cyan, magenta and yellow dye curves.
- Axis and drawing notes: The characteristic plot's exposure axis runs -3.0 to 2.0 (not -4.0 to 1.0 as on the Portra sheets); the spectral-sensitivity axis runs 0.0 to 3.0. The spectral-dye-density curves end at about 685 nm as drawn.
- Kodak, Kodak Professional, Portra, Ektar, Gold, Ultra, Max, Endura and Vision are trademarks
  used by Kodak Alaris under licence from Eastman Kodak Company. The sheet is not redistributed.
