# Film simulations: sources and attribution

Independently developed film simulations based on published manufacturer
technical data. No manufacturer affiliation or endorsement.

OmaRAW digitises numerical characteristic curves and spectral measurements,
then uses its own rendering model to generate the film tables. Axis calibration,
resampling, spectral substitutes and display mapping affect the result. These
looks are interpretations with documented approximations; the measurements
were not made by OmaRAW. The manufacturers' technical sheets, page artwork,
logos and third-party comparison LUTs are not distributed.

## Source publications

Manufacturer and product names below identify the sources. They remain in the
source directory names for traceability; the interface uses neutral look names.

| Interface look or model input | Publication credit and extraction notes |
|---|---|
| Cinema 2383 | Eastman Kodak, H-1-2383t: [original extraction](kodak-2383/SOURCE.md), [revised axes](kodak-2383-v3/SOURCE.md) |
| Cinema 2393 | Eastman Kodak, H-1-2393t: [source](kodak-2393/SOURCE.md) |
| Cinema Mono 2302 | Eastman Kodak, H-1-2302: [source](kodak-2302/SOURCE.md) |
| Cinema 3510, 3513DI, 3521XD, 3523XD | Fujifilm, Motion Picture Film Manuals KB-0707E and KB-1101E: [3510](fuji-3510/SOURCE.md), [3513DI](fuji-3513di/SOURCE.md), [3521XD](fuji-3521xd/SOURCE.md), [3523XD](fuji-3523xd/SOURCE.md). These also use 2383 spectral substitutes. |
| Cinema CP30 | Agfa Motion Picture Division, Technical Data CP30: [source](agfa-cp30/SOURCE.md) |
| Portrait 160, 400, 800 | Kodak Alaris, E-4051, E-4050, E-4040: [160](kodak-portra-160/SOURCE.md), [400](kodak-portra-400/SOURCE.md), [800](kodak-portra-800/SOURCE.md) |
| Fine Grain 100, Warm 200, Vivid 400 | Kodak Alaris, E-4046, E-7022, E-7023: [100](kodak-ektar-100/SOURCE.md), [200](kodak-gold-200/SOURCE.md), [400](kodak-ultramax-400/SOURCE.md) |
| Camera-negative model and still-film dye donor | Eastman Kodak, H-1-5219t: [5219](kodak-5219/SOURCE.md) |
| Photographic-paper models for still films | Eastman Kodak, E-4021: [Portra Endura](kodak-portra-endura/SOURCE.md), [Supra Endura](kodak-supra-endura/SOURCE.md) |

The source notes give editions, measured quantities, extraction methods and
calibration references. Older cinema models include the 5219 camera-negative
stage; [digital cinema models](DIGITAL-SOURCE.md) omit it. Still-film models
combine the relevant negative, fitted 5219 dye shapes and photographic paper.
[Colour-science credits](COLOUR-SOURCES.md) cover the observer, illuminants,
colour-chart measurements and generation library.

## Rights and attribution

This notice credits the sources and describes OmaRAW's independent simulations.
No manufacturer licence, affiliation or endorsement is claimed. OmaRAW's code
licence does not relicense third-party publications or trademarks. See
[film data and rights](RIGHTS.md) for the scope of the included material and
the separate colour-science licences.
