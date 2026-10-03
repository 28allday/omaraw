# Kodak Gold 200 Film

- Publication E-7022, June 2023 (revised 06-23) (Kodak Alaris Inc.), https://kodakprofessional.com/sites/default/files/wysiwyg/pro/resources/E7022%20Gold%20tech%20sheet.pdf
  — page 4 (characteristic curves, spectral sensitivity, spectral dye density).
- The March 2022 and February 2016 editions and the February 2007 edition (Gold 100 and 200) show the same Gold 200 curves.
- Curves extracted as vector paths with `tools/film/extract-curves.py`; each axis calibrated
  from the plot frame and gridlines/ticks (tick-label centres are offset by their minus signs).

- `sensitometric-*.csv`: Status M density vs log exposure (lux-seconds), daylight exposure,
  Process C-41, D-min included as drawn. **Log H Ref: -1.14** (the sheet's normal-exposure
  log H).
- `sensitivity-*.csv`: log sensitivity (sensitivity = reciprocal of the exposure in erg/cm²
  for the specified density) vs wavelength; daylight, effective exposure 1/50 s, Status M,
  density 0.2 above D-min.
- `dyes-*.csv`: diffuse spectral density vs wavelength, Process C-41: typical densities for a
  midscale neutral subject (`midscale-neutral`, D-min included) and the minimum density
  (`minimum-density`). The sheet gives no separate cyan, magenta and yellow dye curves.
- Axis and drawing notes: The characteristic plot's negative exposure labels are printed in bar notation (a bar over 3.0, 2.0, 1.0, meaning -3, -2, -1); the axis is calibrated from the frame as -3.0 at the left edge and 1.0 at the right. The spectral-sensitivity curves are drawn as many short segments; they are joined end to end in order. The cyan-forming curve is drawn in two pieces, a low blue-region lobe (about 390-470 nm) and the main red band (about 485-690 nm), falling below the plot floor between them; both pieces are kept in one file and the gap is not filled. The characteristic plot states no process; the sheet's process is C-41.
- Kodak, Kodak Professional, Portra, Ektar, Gold, Ultra, Max, Endura and Vision are trademarks
  used by Kodak Alaris under licence from Eastman Kodak Company. The sheet is not redistributed.
