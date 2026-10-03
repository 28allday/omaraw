# Fujicolor Positive Film Eterna-CP 3521XD

- Fujifilm Motion Picture Film Manual KB-0707E (2007), pp. 56–57 (PDF page 30).
- Characteristic curves only: the manual carries no spectral sensitivity or
  spectral dye density curves for the positive films. Until Fuji's spectral
  data is found, the model runs this stock with a documented stand-in dye set.
- Curves extracted as vector paths with `tools/film/extract-curves.py`; axes
  from the rendered gridlines (0.5 density per line; the sheet's 0.5 log H
  scale bar spans one cell), density axis 0 to 5.0.
- `sensitometric-*.csv`: Status A three-colour diffusion density vs *relative*
  log exposure (lux-seconds), 2854 K tungsten 1/100 s through Fuji SC-41 and
  CC-90Y + CC-60M print colour-correction filters, standard processing. The
  sheet draws G shifted 1.0 and B 2.0 log H to the left to avoid overlap; both
  shifts are undone here, which puts the three curves within 0.1 log H of each
  other at density 2.0, as a print stock's should be.
- Aim print density (Status A, xenon projection): 1.10 red, 1.05 green, 1.05 blue.
- Trademarks belong to FUJIFILM Corporation. The manual is not redistributed.
