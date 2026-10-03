# Kodak Vision3 500T Color Negative Film 5219 / 7219

- Publication H-1-5219t, November 2007 (Eastman Kodak Company). The 2022 revision
  shows the same curves as raster images.
- Curves extracted as vector paths with `tools/film/extract-curves.py`; the
  sensitometric axes from the rendered frame (its labels are outlines), the
  spectral axes from tick labels.
- `sensitometric-*.csv`: Status M density vs log exposure (lux-seconds), 3200 K
  tungsten 1/50 s, Process ECN-2. The sheet's camera-stops axis puts 0 stops at
  the rated exposure; recorded here as the log exposure axis only.
- `sensitivity-*.csv`: log sensitivity vs wavelength, effective exposure 1/25 s,
  Status M, density 0.2 above D-min.
- `dyes-*.csv`: diffuse spectral density, ECN-2, D-mins subtracted; C, M, Y
  peak-normalised; plus the midscale neutral and the minimum density.
- The 2007 plot interchanges the printed cyan/yellow labels. Our curve names
  follow the corrected March 2022 plot: yellow absorbs in the blue region
  (peak about 450 nm), magenta in green (about 540 nm), cyan in red (about
  680 nm). Checked against the 2022 sheet on 1 October 2026; no curve data
  or table was changed by that check.
- Trademarks belong to Eastman Kodak Company. The sheet is not redistributed.
