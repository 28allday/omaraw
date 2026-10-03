# Camera profiles

Open **Develop → Prepare → Camera profile**. OmaRAW identifies the camera and
calibrates its colour automatically. No profile download or setup is required.

## Included choices

- **Apply camera look** is a one-click contrast and saturation preset inspired
  by your camera's JPEGs. It appears only when a compatible darktable camera
  style is included. It adjusts Tone and Colour balance, replacing an active
  Print stock or DCP rendering. Exposure, white balance, crop, local adjustments,
  local contrast, lens corrections and detail settings stay. **Undo** restores
  the previous look. If you later adjust its settings, you can apply it again.
- **Automatic colour** uses the engine's camera calibration and your regular
  tone controls. Choosing it after a DCP restores the parked tone controls;
  it does not reset other adjustments or undo a camera-look preset.
- **Community colour · included** offers an alternative colour and contrast
  rendering for cameras covered by the included RawTherapee collection. It
  appears automatically for a matching RAW. All 54 profiles work offline.

These are independent community renderings, not manufacturer picture-style
packs. They do not automatically reproduce the picture style selected in the
camera or every manufacturer mode such as Portrait, Landscape or film simulation.

If a camera has no included look, automatic colour still works. OmaRAW does
not offer another camera model's profile as a substitute.

## Add your own profiles

Open **Additional profiles → Import profile…** to add a `.dcp` camera profile. OmaRAW checks the file and
keeps its original bytes in a managed library. If it matches the open RAW, it is
selected immediately; otherwise it becomes available for that camera's RAWs.

Use **Profiles folder…** for an existing collection. The folder and its subfolders
are scanned for DCP files. Only files matching the photo's normalized camera or
original EXIF model are offered. Punctuation and case are ignored; similar model
numbers are not treated as interchangeable. Identical files appear once. Use
**Refresh** after changing the folder's contents. Larger lists have search.

Profiles whose metadata identifies the right camera but whose contents cannot
be rendered appear under **profiles unavailable**, with an explanation.

Selecting a profile replaces base colour and tone through the **Print stock**
slot. It parks the regular tone mapper and resets print refinements to neutral.
Exposure, white balance, curves, crop, local adjustments and other edits retain
their settings. A profile and a film print stock cannot occupy that slot together.
**Colour & Look → Creative profile** remains an independent layer for another look.

Choose **Automatic colour** to disable the DCP/print look and restore the parked
tone mapper. Use **Undo** to recover a previous print stock. This does not reset
the photograph's other edits or advanced input calibration. The **Advanced input
colour** fold contains the original input profile, rendering intent, gamut and
working-space controls.

## What is supported

Compatible user-supplied DCPs may carry names such as Camera Standard, Camera
Neutral or Camera Portrait. Those names do not make a look available without its
file. OmaRAW uses the credited community profiles and does not automatically
reproduce the creative picture style recorded in camera metadata. JPEGs already
contain their camera rendering; this selector is for RAW and camera-linear DNG.

The existing DCP implementation supports its own tone curve, hue/saturation maps,
look tables, a forward-colour correction and baseline exposure offset. A profile
must contain an explicit tone curve. For two calibration illuminants the parser
uses the map nearer daylight rather than interpolating with the current white
balance. This is an approximate base rendering, not pixel-identical XMP,
another RAW editor or an in-camera JPEG. Embedded DNG calibration is still handled
by the engine; this panel does not enumerate alternate profiles embedded in a DNG.

The separate XMP preset importer retains its existing approximation of
built-in source profile names. Selecting a DCP here does not expand the
supported subset of enhanced creative XMP profiles.

## Saving and moving edits

Camera-look adjustments are retained in ordinary history and sidecars. Presets
and snapshots retain the full tone and colour settings, including fields hidden
by the simplified controls. Include Tone and Colour balance when saving that look.

DCP selection is retained in history, snapshots, the catalogue, sidecars and presets
that include **Print stock**. Identified saved tables cannot be applied through a
preset to the wrong camera or to a JPEG. Older tables without identification
metadata retain their legacy behaviour.

Imported sources and generated tables are stored under `camera-profiles` in
OmaRAW's application-data directory. Keep these files when moving a catalog
to another computer.
Choosing directly from a folder retains the baked tables, but not a separate copy
of the source; use Import DCP to keep the source too.

Sidecars and presets reference the baked tables rather than embedding them.
Catalogue backups do not package the global profile library. On another machine,
select the included profile again, or import the original custom DCP and select
it again; transfer any associated preset
adjustments as well. Missing or invalid active tables stop export with an error.
Select the source profile again to regenerate them, or choose Automatic colour.

## Licences and sources

The included DCP collection contains 46 profiles explicitly declared public
domain and eight declared RawTherapee CC0. Each file's original bytes, source
revision, licence declaration and SHA-256 are recorded in
`profiles/camera/manifest.json`; see its `NOTICE.md` and `CC0-1.0.txt`.
The application embeds the profiles, so a user does not need to find a folder.
The selected files all have explicit tone curves supported by the current
renderer. Profiles without a clear redistribution declaration or a supported
tone curve were excluded.

Camera-look presets adapt only the tone and saturation portion of darktable's
GPL-3.0-or-later styles. They intentionally omit the styles' exposure, local
contrast and occasional lens/other edits. Attribution and corresponding source
remain part of the engine distribution. Neither option requires a vendor GPU.

Processing runs on the CPU and requires no particular GPU. Imported profiles
remain subject to their own licences; only bring in files you have rights to use.
