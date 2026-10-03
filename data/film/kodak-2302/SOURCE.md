# Kodak Black-and-White Print Film 2302 / 3302

- Publication H-1-2302, August 2022 (Eastman Kodak Company); its plots are
  scans, so the curves are traced from the image by colour with
  `tools/film/trace-raster.py`.
- `sensitometric-5min.csv`, `sensitometric-7min.csv`: diffuse visual density
  vs log exposure (lux-seconds), tungsten 1/50 s, Kodak D-97 developer at
  70°F, for 5 and 7 minutes' development. The sheet's gamma-vs-time plot puts
  the recommended control gamma of 2.4–2.6 at about 7 minutes; the model uses
  the 7-minute curve. Dashes in the sheet's line are bridged by interpolation.
- `sensitivity-blue-sensitive.csv`: log sensitivity vs wavelength, 0.10 s
  tungsten exposure, D-97, at 0.2 above D-min, traced from the sheet's plot
  (380–500 nm); beyond the plotted span the emulsion is given no sensitivity.
- Trademarks belong to Eastman Kodak Company. The sheet is not redistributed.
