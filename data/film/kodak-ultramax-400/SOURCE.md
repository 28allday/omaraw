# Kodak Ultra Max 400 Film

- Publication E-7023, February 2016 (revised 2/16) (Kodak Alaris Inc.), https://imaging.kodakalaris.com/sites/prod/files/files/resources/E7023_max_400.pdf
  — page 4 (characteristic curves, spectral sensitivity, spectral dye density).
- No later edition was found. The midscale-neutral curve is drawn dashed on the sheet; its path is continuous and is taken whole.
- Curves extracted as vector paths with `tools/film/extract-curves.py`; each axis calibrated
  from the plot frame and gridlines/ticks (tick-label centres are offset by their minus signs).

- `sensitometric-*.csv`: Status M density vs log exposure (lux-seconds), daylight exposure,
  Process C-41, D-min included as drawn. **Log H Ref: -1.44** (the sheet's normal-exposure
  log H).
- `sensitivity-*.csv`: log sensitivity (sensitivity = reciprocal of the exposure in erg/cm²
  for the specified density) vs wavelength; daylight, effective exposure 1/100 s, Status M,
  density 0.2 above D-min.
- `dyes-*.csv`: diffuse spectral density vs wavelength, Process C-41: typical densities for a
  midscale neutral subject (`midscale-neutral`, D-min included) and the minimum density
  (`minimum-density`). The sheet gives no separate cyan, magenta and yellow dye curves.
- Kodak, Kodak Professional, Portra, Ektar, Gold, Ultra, Max, Endura and Vision are trademarks
  used by Kodak Alaris under licence from Eastman Kodak Company. The sheet is not redistributed.
