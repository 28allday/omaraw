# Creative profiles

Open **Develop → Colour & Look → Creative profile**. Choose a profile, then adjust
**Amount** from 0 to 200. For built-in looks and CUBE LUTs, zero preserves the existing rendering exactly; 100
gives the designed look; higher values strengthen it. The effect switch bypasses
the profile while keeping the selection and amount. **None** removes it.

RGB profiles add a separate colour look after film/print rendering. Exposure,
white balance, curves, print stock, grain, masks and other controls keep their
settings. A profile can be combined with an existing preset; choosing a curated
preset also leaves the selected creative profile in place.

## Included looks

| Profile | Character |
| --- | --- |
| Warm Portrait | Gentle colour with warm highlights |
| Clear Landscape | Richer colour and firmer contrast |
| Soft Colour | Quiet colour, softer contrast and lifted blacks |
| Cinema Dusk | Cool shadows, warm highlights and restrained colour |
| Silver Monochrome | Neutral black and white with firm contrast |
| Warm Monochrome | Softer black and white with a warm tone |

These are original OmaRAW recipes, included under the application's
GPL-3.0-or-later licence. They do not include proprietary profiles or sample
photographs, and do not claim to reproduce a measured film stock. Monochrome
profiles retain monochrome rendering above Amount 100 while strengthening tone
and any colouring. Below 100 they blend towards the original colour.

## Import your own LUT

Choose **Import profile…** and select a `.cube` file. OmaRAW validates it and stores
a versioned copy in its creative-profile library. Moving or editing the original
file does not change saved photographs. Imported LUTs appear in the profile menu.

Set **LUT colour space** to the space the LUT was designed for: sRGB, RGB (1998),
linear Rec. 709 or linear Rec. 2020. CUBE files do not reliably identify this
space. Camera-log LUTs requiring a log input transform should be converted to a
supported space before importing.

The importer accepts 3D CUBE tables with 2–64 entries per axis, optional
`DOMAIN_MIN`/`DOMAIN_MAX`, and files up to 32 MB. It rejects 1D shapers, unknown
directives, duplicate declarations, non-finite values and incomplete or excess
table rows. Interpolation is trilinear. Values beyond the LUT domain retain
their residual instead of being hard clipped to that domain.

If a stored LUT is missing or invalid, export reports an error when that profile
is active. Reimport the original LUT and select it to restore the look, or choose
None. Active imported profiles must remain available even at Amount zero.

## Import enhanced XMP profiles

Use the same **Import profile…** button for self-contained `.xmp` creative
profiles (`PresetType="Look"`). These are distinct from Develop presets.

OmaRAW imports embedded RGB tables (1D or 3D) and HSV look tables, individually
or together. HSV tables run after input colour conversion, exposure, final
white balance and DNG gain correction, before creative grading; RGB tables run
after film/print rendering. Table primaries, transfer
functions, gamut extension and per-table Amount ranges are retained. The
colour-space control is hidden because XMP records the space. If the profile
disables Amount, the slider is hidden and its fixed amount is used.

An XMP profile can define a nonzero minimum: Amount 0 then means its minimum
strength, not bypass. Use the effect switch or **None** for complete bypass.

OmaRAW uses its own base rendering; this is not pixel-identical XMP/ACR
rendering. Profiles requiring non-neutral source adjustment settings, camera
model restrictions, an unavailable base camera profile, separate monochrome
treatment, unknown table flags or missing external tables are rejected with the
reason. No partial look is imported. A profile's RAW/rendered support flags are
checked when selecting it and exporting.

The original XMP (including its metadata and copyright) is retained beside a
validated `.omaprofile` runtime file. No proprietary SDK or creative assets ship with
OmaRAW. Import only profiles you have rights to use.

## Saving and moving edits

The tool's menu can save a **Creative profile** adjustment preset. The same
group is available when saving a broader preset. Selection, amount, colour space
and the imported LUT reference are retained in presets, snapshots, undo history,
the catalogue and OmaRAW sidecars.

Built-in profiles need no external files. Imported LUTs live in
`creative-profiles` beneath OmaRAW's application-data directory.
Presets and sidecars currently reference these files; they do not embed the LUT.
When transferring an edit to another machine, also transfer/import the LUT and
select that imported profile. Catalogue backups do not yet package this global
profile library.

Processing has a CPU implementation and requires no particular GPU. This is a
native creative-profile feature. The separate approximate XMP Develop-preset
importer remains available in Presets.
