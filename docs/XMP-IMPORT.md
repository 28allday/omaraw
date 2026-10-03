# Importing XMP presets

OmaRAW imports XMP Develop presets from `.xmp` and
`.lrtemplate` files through **Adjustments > Presets > Import**. Select one or several
files; OmaRAW JSON files can be included in the same selection. Extract ZIP
packs first.

After import, a report lists each file, the settings converted and anything
that was not translated. Reopen it through **Adjustments > Presets > Last Import
Report** and use **Copy report** to retain it. Imported XMP presets
carry `XMP` and `approximate` tags, and retain the XMP group when one is
present. Older LRTEMPLATE files use the `Imported XMP` category.

Applying a converted preset uses OmaRAW's processing. Even matching slider
numbers do not guarantee matching rendered pixels. Adjust the result on a
photo before saving your own version. These conversions are starting points,
not an exact recreation of another rendering engine.

## What is converted

Only settings actually present in the source preset are considered. Ordinary
fields outside the converted set keep their current values, except settings
restored when replacing the previous preset. Selecting an imported preset uses
the same exclusive replacement as built-in and saved presets. Point-curve conversion replaces the RGB curve collection; missing
channels start linear. A vignette amount also sets vignette saturation to zero
to avoid adding an unrelated colour effect from that module's default.

| XMP setting | OmaRAW conversion |
|---|---|
| Exposure2012 / Exposure | Exposure in EV; automatic exposure/tone is skipped |
| Contrast2012 | Tone contrast `1.5 × 2^(value/100)`; an approximate strength mapping |
| Legacy Contrast | Tone contrast `1.5 × 2^((value−25)/100)`; legacy neutral is 25 |
| Saturation, Vibrance | Colour controls, scaled from percentage to module units; saturation also clears a prior monochrome chroma setting |
| ConvertToGrayscale | Enables the Black and white converter when true; disables it when false |
| GrayMixer (in black and white) | Eight Black and white mix bands (`value/100`), including zero resets. Export includes the converter state and all eight bands. Band responses differ, so appearance is approximate; the HSL values the source editor keeps but does not use in black and white are left out on import. |
| HSL hue/saturation/luminance | Eight Colour mixer bands; hue uses `value × 0.3` degrees, saturation/brightness use `1 + value/100`. Aqua maps to cyan and Purple to lavender. Band shapes and colour math differ. |
| Master point curve | 2–20 points, normalized from 0–255 into the linked RGB curve; a curve that stops short of 0 or 255 is held level to the end; curve stage/interpolation differ |
| RGB point curves | Independent curves; missing channels start linear. With a non-linear master too, the master is folded into each colour's curve (the master first, then the colour, both as smooth splines), sampled at both curves' points |
| Parametric curve | Tone regions, at the same four regions and strengths. Tone regions use the source editor's default splits (25/50/75); moved splits are reported |
| SplitToning | Approximate tints on Primary correction Lift/Gain; hue in degrees, saturation / 100 × 0.12 for Lift and × 0.25 for Gain; Balance is reported |
| Red/Green/Blue Hue and Saturation (calibration) | Primaries: hue ±100 turns the primary up to ±30°, saturation ±100 scales its purity ±50%; ShadowTint is reported |
| GrainSize | Grain coarseness, the source editor's default 25 at the engine's default; roughness (GrainFrequency) is reported |
| CameraProfile | See *Camera profiles* below. built-in source profiles are reported: OmaRAW's camera colour stands in for them |
| Sharpness / SharpenRadius | Amount × 0.02, capped at 2 with a warning; radius in pixels. The sharpening algorithm differs. |
| GrainAmount | Grain strength, using OmaRAW's grain algorithm |
| PostCropVignetteAmount | Vignette brightness × 0.01; other vignette properties are not translated |
| Temperature / Tint | A RAW's white balance in kelvin, tint scaled from the source editor's ±150; "As Shot" keeps the camera's, "Auto" is reported |
| Highlights2012 / Shadows2012 | Shadows and highlights, together, so the module's own defaults never act on the one not given |
| Blacks2012 | Black level (small, opposite sign); Whites2012 is reported |
| Clarity2012, Texture, Dehaze | Local contrast detail × 0.01, texture as given, dehaze × 0.01 |
| LuminanceSmoothing | Noise reduction, Everything, strength value/50 with the detail slider kept |
| Crop | Left/top/right/bottom when the crop is not turned; a straightened crop is reported |
| LensProfileEnable | OmaRAW's lens correction from its own lens database |

Disabled supported panels and automatic exposure/contrast are respected by
skipping the affected conversions and identifying them in the report.
Malformed or out-of-range supported values reject the file instead of saving
a damaged preset. A file with no supported adjustments is not imported.

## Camera profiles

For direct selection, use **Develop → Colour → Camera profile**. It lists
compatible DCPs, offers managed import and explains unavailable files. See
[camera profiles](CAMERA-PROFILES.md) for supported features and limitations.
The preset-import behaviour described below is unchanged.

Film-look packs usually come with camera profiles (`.dcp` files) that do most
of the look, and their presets name one (`CameraProfile`). A profile is made
for one camera model, so a pack has one per camera per look. Tell OmaRAW
where they are with **Adjustments ▸ Presets ▸ Camera Profiles Folder…**
(subfolders are searched too). OmaRAW ships no profiles.

When such a preset is applied, OmaRAW finds the profile with that name for
the photo's camera, reads it, and bakes it into tables that render through
the Film section's print module, in place of the tone mapper, exactly as a
print stock does: **Stock** reads *Camera profile*, **Strength** blends it
with the plain rendering, and turning it off brings the tone mapper back.
The preset's parametric and point curves, which the source editor applies after
the profile, are baked into the profile's tables after its tone curve, on
display values (ProPhoto, sRGB-encoded), so they shape the look as they do
there; the photo's own Tone regions and point curve are left straight. The
rest of the preset applies as usual. A photo from a camera the folder
has no profile for, a JPEG, or a damaged profile keeps its own rendering; the
status line says which and why.

The profile is rendered the way the DNG specification describes: its forward
matrix colour, its hue/saturation map, its look table and its tone curve,
with two OmaRAW choices. OmaRAW's scene light is brought to the level the
profile's tone curve expects (its middle grey is about half OmaRAW's), and
light above white rolls off over about two stops instead of clipping. Only
profiles with their own tone curve are supported. The baked tables are kept
under OmaRAW's data folder (`camera-profiles/`) and named by the profile's
contents and the preset's curves, so a changed profile is baked again.

A damaged profile is refused rather than rendered.

## A photo's own sidecar

A RAW's edit and metadata can be saved in an XMP sidecar named after the photo
(`DSC_0833.NEF` → `DSC_0833.xmp`). Enable sidecar writing in the source editor
before importing. OmaRAW reads and writes the same file:

- **Import** takes the rating, colour label, keywords (the hierarchy from
  `lr:hierarchicalSubject`), title, caption, creator and copyright, and the
  XMP edit, converted with the table above. Sidecars can include every slider; the ones at the source editor's defaults are left at OmaRAW's,
  and exposure starts from OmaRAW's own rendering of a RAW (+0.7 EV), since
  the source editor's 0 is its own. The result is a starting point, not the source editor’s
  rendering.
- **Writing** (File ▸ Sidecars) puts the catalog's metadata there, OmaRAW's
  exact edit (the engine's own history, which another OmaRAW catalog — or
  darktable — reads back exactly), and beside it the same edit as CRS
  settings, approximately, for compatible editors. A CRS section OmaRAW did not write is the source editor's own edit and
  is never replaced by OmaRAW's approximation.
- **Read Sidecars for Selection** takes the metadata and edit back from the
  files.
- DNG, JPEG and TIFF files support embedded XMP.
  File ▸ Sidecars ▸ Write Into DNG, JPEG and TIFF Files does the same (it
  changes the original files; off by default). Without it those formats get
  a sidecar with the whole file name (`photo.jpg.xmp`). A JPEG exported with
  its edit already in its pixels is never edited again on import.
- Older OmaRAW sidecars (`DSC_0833.NEF.xmp`) are still read, and move to the
  new name when next written — unless darktable's own edit is in them, since
  darktable reads that name.

## What remains untranslated

The report identifies unsupported settings by their XMP field names,
including zero-valued settings: clearing a setting can matter too. These
include proprietary camera/creative profiles and LUT looks, automatic and
relative (JPEG) white balance, Whites, parametric-curve splits, the shadow
tint, split-toning balance, grain roughness, colour grading, colour noise
reduction, manual lens corrections, straightened crops and other geometry,
local/AI masks and retouch. Named curves without supported point data are
also untranslated.

DCP files and profile XMP files are not presets (a DCP is found through the
camera profiles folder instead); adjustment-brush or export LRTEMPLATE files, DNG-based
mobile presets and `.lrcat` catalogs are not Develop preset imports. A photo's
own sidecar is read on import instead (above); it is not accepted as a named
XMP preset.

Existing preset names are kept, regardless of case. Each file is handled
independently: a rejected file does not prevent other valid files from being
imported. A native OmaRAW JSON document retains its existing transactional
validation. Limits are 1,000 files and 64 MiB per selection, with 4 MiB per
XMP file. The LRTEMPLATE reader accepts literal tables and never runs
Lua code; the XMP reader does not resolve external XML resources.

## Format reference

The [CRS namespace specification](https://developer.adobe.com/xmp/docs/xmp-namespaces/crs/)
defines property names, ranges and ordered curve points. Conversion is
approximate and uses OmaRAW's rendering.
