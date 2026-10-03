# Third-party components and their licences

OmaRAW is free software under the GNU General Public License, version 3
or any later version (`LICENSE`). It is built on, links to, or adapts the
following. Every notice below is installed under
`/usr/share/licenses/omaraw/` with the package, beside this file.

## Adapted or bundled code

| Component | Licence | How OmaRAW uses it | Notice |
|---|---|---|---|
| darktable (processing engine, at the pinned revision recorded in `build-info`) | GPL-3.0-or-later | Built as a private engine with OmaRAW's patches (`patches/`); the full patched source ships in the release's source bundle | `LICENSE.darktable`, `darktable/AUTHORS` |
| darktable's diffuse module and RCD/colour kernels | GPL-3.0-or-later | Adapted line by line into OmaRAW's Vulkan passes, `gpu/src/texture.h` and `gpu/src/chain.h`, which keep the darktable copyright and the SPDX identifier | `LICENSE.gpu` |
| rawspeed (bundled in the engine) | LGPL-2.1-or-later | RAW decoding | `rawspeed/LICENSE` |
| LibRaw (bundled in the engine) | LGPL-2.1 (chosen from the LGPL-2.1 / CDDL-1.0 dual licence) | RAW decoding of formats rawspeed lacks | `LibRaw/LICENSE.LGPL`, `LibRaw/LICENSE.CDDL` |
| libxcf (bundled in the engine) | GPL-3.0-or-later | XCF export | `libxcf/LICENSE` |
| whereami (bundled in the engine) | MIT / WTFPLv2 | Locating the engine's data | `whereami/LICENSE.MIT`, `whereami/LICENSE.WTFPLv2` |
| Lucide icons, including Feather-derived icons by Cole Bemis | ISC / MIT (per the retained notice) | Interface icons, `src/icons/` | `LICENSE.lucide` |
| JetBrains Mono 2.304 | SIL Open Font License 1.1 | Unmodified Regular face, embedded for the launch screen; [upstream release](https://github.com/JetBrains/JetBrainsMono/tree/v2.304) | `OFL.JetBrainsMono.txt` |
| darktable data (noise profiles, white-balance presets, styles, watermarks) | GPL-3.0-or-later; retained file-specific notices apply | Installed with the private engine under `/usr/lib/omaraw/engine/share/darktable/` | `LICENSE.darktable`, `darktable/AUTHORS` |
| RawSpeed camera database | CC-BY-SA-3.0 | Camera identification and calibration; unmodified data with the pinned source | `rawspeed/CAMERA-DATA.txt` and the notice in `cameras.xml` |
| Khronos OpenCL-Headers | Apache-2.0 | Engine OpenCL interface declarations | `OpenCL/LICENSE` |
| LibRaw-cmake build support, including Gilles Caulier's build script | BSD-3-Clause; GPL-2.0-or-later on the build script | Building the bundled decoder; file-specific notices remain in the source bundle | `LibRaw-cmake/LICENSE`, `LibRaw-cmake/NOTICE` |
| Selected original RawTherapee camera profiles (54 files, pinned in `profiles/camera/manifest.json`) | CC0-1.0 (8) / explicit public domain (46), verified from each original file | Camera-matched **Community colour**, embedded as unmodified DCP resources | `camera-profiles/NOTICE.md`, `camera-profiles/CC0-1.0.txt`, `camera-profiles/manifest.json` |

The OCIO configuration `Oma Colour 1` (`colour/oma-v1/config.ocio`) is
OmaRAW's own work: its matrices are derived from the ICC colourants of the
standard spaces, and it contains no third-party lookup tables.

## Libraries linked at runtime (system packages)

| Library | Licence |
|---|---|
| Qt 6 (Core, Gui, Qml, Quick, Quick Controls 2, Multimedia, Widgets, PrintSupport, Sql, Network, DBus, SVG, image formats) | LGPL-3.0 |
| OpenColorIO | BSD-3-Clause |
| libplacebo | LGPL-2.1-or-later |
| Vulkan ICD loader | Apache-2.0 |
| LibRaw | LGPL-2.1 / CDDL-1.0 |
| Exiv2 | GPL-2.0-or-later |
| libgphoto2 | LGPL-2.1-or-later |
| lensfun | LGPL-3.0-or-later |
| Little CMS 2 | MIT |
| libheif, libavif, libjxl, libwebp, libpng, libjpeg-turbo, libtiff, OpenEXR/Imath, OpenJPEG | BSD-style, libpng, IJG/BSD, libtiff, BSD-3-Clause, BSD-2-Clause |
| OpenCV | Apache-2.0 |
| potrace | GPL-2.0-or-later |
| SQLite | Public domain |
| GLib, GTK 3, GDK-Pixbuf, Pango, Cairo, librsvg, json-glib, libxml2, pugixml, curl, ICU, zlib | LGPL-2.1-or-later, LGPL-2.1-or-later, LGPL-2.1-or-later, LGPL-2.1-or-later, LGPL-2.1-or-later, LGPL-2.1-or-later, LGPL-2.1-or-later, MIT, MIT, curl, ICU, zlib |
| CUPS | Apache-2.0 |

All of these are compatible with distributing OmaRAW under GPL-3.0-or-later.
Nothing is statically linked from a GPL-incompatible source, and the
GPL-2.0-or-later components (Exiv2, potrace) are used under their
"or later" option.

## Included AI tools and denoise

Automatic X-Trans processing uses RawForge’s Restormer release asset, locally
promoted to float32 with fixed tile dimensions. Bayer retains the Heavy model.
Both original and converted digests are recorded in `src/denoise/models.json`.
Camera calibration fallback reads the already packaged RawSpeed camera database
(CC-BY-SA 3.0, RawSpeed contributors); no new camera/profile bundle is added.

The CFA input transform follows [RawHandler](https://github.com/rymuelle/RawHandler)
(MIT, copyright 2025 rymuelle). The Malvar demosaicing filters are adapted from
[colour-demosaicing](https://github.com/colour-science/colour-demosaicing)
(BSD-3-Clause). OmaRAW's modifications provide tiled float32 processing,
phase-preserving borders and residual denoising. Full notices are embedded in
`src/denoise/LICENSES.txt` and installed as `AI-DENOISE.txt`.

The package includes SAM 2.1 selection and LaMa removal models under their
upstream Apache-2.0 licences, reproduced in `src/ai/LICENSES.txt`.
The Bayer Heavy and X-Trans Restormer ONNX assets are distributed in RawForge's
MIT-licensed project release `onnx_v1.0.0`; no separate model-weight licence is
published. OmaRAW distributes its float32 conversions under that upstream
project licence, preserving the copyright notice and conversion details. The
manifest pins original and converted SHA-256 hashes. Training photographs are
not included; no separate author permission is claimed.

A private Python 3.14 interpreter (PSF-2.0) is included with its standard library
and licence. It does not depend on the system Python version. The complete
source bundle also contains the matching Python source archive.

The included rawpy, LibRaw, NumPy, Pillow, ONNX, ONNX Runtime, WebGPU provider,
tifffile and supporting Python packages retain their notices in the isolated
runtime and `ai-runtime/` licence directory. `pkgbuild/ai-sources.json` pins every
binary/source input. The complete source bundle includes rawpy's matching source
and LibRaw submodules for its bundled shared library.
See [AI denoise](docs/AI-DENOISE.md) for provenance and limitations.

## Trademarks

darktable is the name of the darktable project; OmaRAW uses its engine
and says so, and uses none of its logos. Camera and format names belong
to their owners.

## Film data

Independently developed film simulations based on published manufacturer
technical data. No manufacturer affiliation or endorsement. The source index
and attribution are in `data/film/NOTICE.md`, installed as `film/NOTICE.md`.

The film tables are generated by OmaRAW from digitised numerical curves and
spectral data. Each stock's `data/film/*/SOURCE.md` identifies the manufacturer,
technical sheet, extraction method and modelling substitutions. Those source
credits are installed under `film/`; `film/DIGITAL-SOURCE.md` describes the
digital variants. The original technical sheets and comparison LUTs are not
distributed. Plots in the source tree are generated from the extracted data.

OmaRAW uses the numerical film-response information as input to its own
simulation. No express manufacturer redistribution licence or endorsement is
claimed. OmaRAW's code licence does not relicense third-party publications or
trademarks. See [film data and rights](data/film/RIGHTS.md) for the scope of the
included material and source attribution.

The offline table generator uses Colour 0.4.7 (BSD-3-Clause, Copyright 2013
Colour Developers), CIE standard-observer and D50/D55/D65 illuminant data,
and N. Ohta's colour-chart measurements supplied by Colour. Its licence
and the CIE source/attribution details are installed as
`film/COLOUR-SCIENCE-LICENSE.txt` and `film/COLOUR-SOURCES.md`.
These permissions do not grant rights in the manufacturers' film data.

## Original artwork

The Spectrum intro animation is original artwork by Gavin Nugent, copyright
2026. Its poster is a frame from that animation. These are OmaRAW assets under
the project's GPL-3.0-or-later licence; the launch-screen font has its own OFL
notice listed above.

## DNG interoperability

This product includes DNG technology under license by Adobe.

OmaRAW's DNG ProfileGainTableMap/2 and enhanced XMP table readers are original
GPL-3.0-or-later implementations. JPEG XL DNG decoding uses the system libjxl
(BSD-3-Clause) through our private RawSpeed integration. The implementations are original code; SDKs, sample photographs and proprietary
creative-profile tables are not included.
The DNG format patent grant is separate from SDK copyright permissions. See
https://www.adobe.com/support/downloads/dng/dng_sdk.html.

## Bundled subject detection

YOLOX-Nano, official `0.1.1rc0` ONNX weights, Copyright Megvii, Inc. and
its affiliates, Apache-2.0. The 3.66 MB model and its unmodified licence are
embedded in OmaRAW. See [provenance and hash](data/autotag/SOURCE.md) and
[licence](data/autotag/LICENSE). OpenCV DNN performs CPU inference.

## Documentation screenshot photographs

The README screenshot uses a demonstration catalog containing the following
StockSnap photographs, supplied under [CC0](https://stocksnap.io/license).
The main image and filmstrip use these credited samples; no personal catalog
or original user photographs are included in the screenshot.

| Photograph | Photographer | Source |
| --- | --- | --- |
| Writing Drawing | Green Chameleon | [StockSnap](https://stocksnap.io/photo/writing-drawing-8Y0EDX4VP9) |
| Whiteboard Post-Itnotes | Startup Stock Photos | [StockSnap](https://stocksnap.io/photo/whiteboard-post-itnotes-YR89OQFMT1) |
| Interior Design | Breather | [StockSnap](https://stocksnap.io/photo/interior-design-N3DP51WC97) |
| Pocketwatch Clock | Veri Ivanova | [StockSnap](https://stocksnap.io/photo/pocketwatch-clock-68OYUIZOUP) |
| Business Man | Direct Media | [StockSnap](https://stocksnap.io/photo/business-man-ARKMDJBQTW) |
| Office Work | Benjamin Child | [StockSnap](https://stocksnap.io/photo/office-work-FSU5SG0X4U) |

These images demonstrate application features and do not imply endorsement by
the photographers or people depicted.
