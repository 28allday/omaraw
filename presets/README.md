# Everyday presets

Import [Everyday.json](Everyday.json) with **File > Presets > Import**.
The six looks appear under **Everyday** in Develop's preset categories.
The pack uses OmaRAW preset format 2, supported by the installed 0.1.0-20
build as well as the current development build. Importing it again skips
existing names; it does not overwrite custom presets.

| Preset | Starting look |
|---|---|
| Clean Natural | Standard tone contrast with a small vibrance lift |
| Soft Portrait | Gentler contrast, restrained colour and a slight warm midtone tint |
| Rich Landscape | Stronger contrast, vibrance and colour separation |
| Warm Evening | Warm midtones and highlights with gentle contrast |
| Muted Editorial | Restrained colour, softer contrast, cool shadows and warm highlights |
| Silver Mono | Neutral black-and-white with stronger contrast |

Set exposure and white balance for the photo, then apply a look and adjust
to taste. These are general starting points, not film-stock or camera-profile
emulations. They change the two Tone controls and the exposed Colour grading
controls. Each sets the same fields so this pack's grades replace one another
when switching looks. Exposure, white balance, curves, crop, lens correction,
detail, grain, local adjustments and retouch retain their existing settings.
Earlier edits in those other controls can affect the result. History lets you
return to the state before applying a preset.

To save your own version, use **+** in the Presets header and choose the
adjustment groups to include. Select **Basic tone** and **Colour** for a
complete saved grade; review the groups because Basic tone also includes
exposure. Give it a new name. **File > Presets > Export** exports saved presets as an OmaRAW JSON
file for backup or transfer to another catalog.

## Older presets

OmaRAW imports its own version-1 and version-2 JSON files, including early
unversioned exports. Duplicate names are skipped regardless of letter case.
Categories and tags are available in format 2.

The development build can also import XMP `.xmp` and `.lrtemplate`
Develop presets as approximate looks, with a report of converted and
untranslated settings. See [XMP import and limits](../docs/XMP-IMPORT.md).
This importer still needs packaging before it appears in the installed app.
Other editors' style formats remain unsupported. Metadata XMP
sidecar import is separate from Develop preset import.

## Validation

Checked with the current engine on disposable copies of Nikon NEF and
Fujifilm RAF photos: all six looks imported and rendered, export/import
preserved settings and labels, and duplicate imports added no extra presets.
Selected parameters matched the pack; excluded settings and crop dimensions
were unchanged. Silver Mono stayed neutral, and returning to Clean Natural
after the other looks reproduced its earlier pixels. The existing legacy
preset import regression also passed.
