# Colour-science inputs to the film model

The offline generator uses Colour 0.4.7, Copyright 2013 Colour Developers,
under BSD-3-Clause. The unmodified licence is in
`COLOUR-SCIENCE-LICENSE.txt`; [upstream source and licence](https://github.com/colour-science/colour/tree/v0.4.7).
NumPy and Colour are generation dependencies, not bundled Python runtimes.

`tools/film/print_chain.py` uses the CIE 1931 2-degree standard observer,
illuminants D50, D55 and D65, and standard colour-space transforms through Colour.
It selects wavelengths from 380 to 780 nm in 5 nm steps and uses these values
in spectral integration and the computed tables. The resulting film model is
OmaRAW's work; it is not a CIE reference implementation or endorsement.

Attribution for the underlying reference quantities, published by the
International Commission on Illumination (CIE), Vienna, Austria:

- CIE 2019, *Colour-matching functions of CIE 1931 standard colorimetric
  observer*, [DOI 10.25039/CIE.DS.xvudnb9b](https://doi.org/10.25039/CIE.DS.xvudnb9b).
- CIE 2018, *Relative spectral power distributions of CIE illuminant D55*,
  [DOI 10.25039/CIE.DS.qewfb3kp](https://doi.org/10.25039/CIE.DS.qewfb3kp).
- CIE 2019, *CIE standard illuminant D65*,
  [DOI 10.25039/CIE.DS.hjfjmt59](https://doi.org/10.25039/CIE.DS.hjfjmt59).
- CIE 2022, *CIE standard illuminant D50*,
  [DOI 10.25039/CIE.DS.etgmuqt5](https://doi.org/10.25039/CIE.DS.etgmuqt5).

CIE's published dataset metadata identifies these reference datasets as
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
The licence requires attribution, a licence link, identification of changes
and its ShareAlike terms when applicable. Creative Commons lists
[GPLv3 as compatible for contributions to adaptations](https://creativecommons.org/compatible-licenses/).
These references document the underlying quantities; the generator loads
Colour's packaged values, rather than downloading these CIE files at runtime.

The still-film and older cinema models also fit their camera-input matrix using
Colour's `ColorChecker N Ohta` spectral reflectances (24 patches, resampled to
the same wavelength grid). Colour credits N. Ohta (1997), *The basis of color
reproduction engineering*, and the Munsell Color Science Laboratory's Macbeth
ColorChecker data. See the [Colour 0.4.7 dataset module](https://github.com/colour-science/colour/blob/v0.4.7/colour/characterisation/datasets/colour_checkers/sds.py),
which declares BSD-3-Clause and Copyright 2013 Colour Developers. The selected
measurements are distinct from the BabelColor Average dataset in that module.
OmaRAW uses them to calculate a fitted matrix; the generated film tables do not
contain the original 24 reflectance spectra. The digital cinema models omit
this camera-fitting stage. Neither the chart makers nor the measurement
authors endorse OmaRAW.

Neither this notice nor these licences clear the manufacturer-derived film
curves described in `RIGHTS.md`.
