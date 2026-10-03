# Kodak Professional Portra 400 Film

- Publication E-4050, January 2025 (revised 1-25) (Kodak Alaris Inc.), https://kodakprofessional.com/sites/default/files/2025-07/e4050.pdf
  — page 4 (characteristic curves, spectral sensitivity, spectral dye density).
- The February 2016 edition carries the same curve paths; the September 2010 edition shows the same curves.
- Curves extracted as vector paths with `tools/film/extract-curves.py`; each axis calibrated
  from the plot frame and gridlines/ticks (tick-label centres are offset by their minus signs).

- `sensitometric-*.csv`: Status M density vs log exposure (lux-seconds), daylight exposure,
  Process C-41, D-min included as drawn. **Log H Ref: -1.44** (the sheet's normal-exposure
  log H).
- `sensitivity-*.csv`: log sensitivity (sensitivity = reciprocal of the exposure in erg/cm²
  for the specified density) vs wavelength; daylight, effective exposure 1/50 s, Status M,
  density 0.2 above D-min.
- `dyes-*.csv`: diffuse spectral density vs wavelength, Process C-41: typical densities for a
  midscale neutral subject (`midscale-neutral`, D-min included) and the minimum density
  (`minimum-density`). The sheet gives no separate cyan, magenta and yellow dye curves.
- Axis and drawing notes: The figures carry Portra 800's figure codes (E4040C, E4040P); the curves and the Log H Ref are Portra 400's own.
- Kodak, Kodak Professional, Portra, Ektar, Gold, Ultra, Max, Endura and Vision are trademarks
  used by Kodak Alaris under licence from Eastman Kodak Company. The sheet is not redistributed.
