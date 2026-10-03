---
name: omaraw
description: >
  Edit, organise and export photographs in an OmaRAW catalog using its headless
  command line. Use for RAW development, colour, masks, denoising, albums,
  keywords, reading existing edits and batch exports. Not for developing the
  OmaRAW application's source code.
---

# Driving OmaRAW from the command line

OmaRAW is a RAW photo editor for Omarchy. Every verb runs headless, answers
with one JSON object on stdout, and exits 0 done / 1 failed / 2 not
understood. The full reference is installed at `/usr/share/doc/omaraw/cli.md`
and is also available as `docs/cli.md` in the source.

```
omaraw --catalog <db> inspect [--photo <id|path>] [--full]
omaraw --catalog <db> apply   [ops.json|-] [--photo <id|path>] [--dry-run] [--keep-going]
omaraw --catalog <db> ops     [--filter <word>]
omaraw --catalog <db> [--new-catalog] import <folder> [--mode add|copy|move|verify] [--dest <folder>]
omaraw --catalog <db> list   [--rating N] [--flag pick|reject|none] [--label <colour>] [--text <words>]
omaraw --catalog <db> export <folder> [same filters] [--format jpeg] [--bits 8|16] [--max-edge 2048] [--quality 90] [--suffix -web] [--location] [--history]
omaraw skill [--link] [--force]
```

## Before anything: one OmaRAW at a time

The develop data can only be open once. If the OmaRAW window is open — on any
catalog — every verb refuses with "already open in another OmaRAW window".
Ask the person to close it; do not retry in a loop. Without `--catalog` the
catalog the application last used is opened; ask which one is meant when there
could be several, and never pass `--new-catalog` unless starting one on purpose.

## The loop that works

1. **`inspect`** the catalog to get photo `id`s (never guess one).
2. **`inspect --photo ID`** to read the edit: controls already moved (with
   range and default), curves, Lab colour, masks, crop, lens, history.
3. **`ops --filter <word>`** for the exact name and argument types of anything
   you are unsure of.
4. **`apply`** a list of operations (`--dry-run` first if it is long).
5. **Look at it**: export a small JPEG in the same `apply`
   (`engine.exportJpeg`) and view the file. Numbers do not tell you what a
   photograph looks like.
6. **`inspect --photo ID`** again to confirm, then `export` for real.

## Writing operations

```json
{"photo": 12, "ops": [
  {"op": "engine.setParam", "args": ["exposure", "exposure", 0.4]},
  {"op": "engine.setLabColour", "args": {"key": "separation", "value": 25}},
  {"op": "catalog.setTitle", "args": {"text": "Rosenborg in spring", "id": 12}},
  {"op": "catalog.addKeyword", "args": ["crocus"]},
  {"op": "engine.exportJpeg", "args": ["/tmp/look-12.jpg", 1600, 90]}
]}
```

- `catalog.` = the catalog (organising, presets, snapshots, variants, files);
  `engine.` = Develop (the open photo's edit, and exporting); `ai.` = local AI
  masking/removal; `denoise.` = AI RAW denoise and linear DNG copies; `match.` = Image Match;
  `capture.` = explicit tethered-camera sessions. The prefix may be
  dropped when the name is unique.
- Arguments positional or named (names from `ops`). A setting (from `ops`'
  `properties`) takes `"value"`: `{"op": "engine.maskStrength", "value": 0.6}`.
  Omit `value` to read it in that run: `{"op":"engine.cameraProfileState"}`.
- `"photo"` at the top or on any operation opens that photo first; `--photo`
  does the same. Catalog operations that take no id act on the selection,
  which follows the named photo; with no photo named (and no `catalog.select…`
  earlier in the run) they are refused.
- Nothing runs unless every operation checks out; the answer lists the
  problems. `--keep-going` runs what it can.
- Each operation finishes (render, export, import) before the next starts, and
  its return value is in `results[i].result`.
- Every Develop change is a history step: it can be undone in the
  application's History panel, and Reset in Develop returns the camera original.

## The operations worth knowing

| What | Operation |
|---|---|
| Any slider | `engine.setParam(op, field, value)` — `op`/`field`/range from `inspect --photo` (`--full` for all) or `ops`' `params` — always the engine's own value and range. The window shows unitless controls as −100…+100 (0 at their default) or 0…100: never send those figures |
| Back to default | `engine.resetParam(op, field)`, `engine.resetModule(op)`, `engine.resetHistory()` (how the photo looked after import: starting exposure, colour calibration, tone mapper and the automatic first edit), `engine.resetToOriginal()` (camera original, nothing on) |
| Reading values | A row with `"enabled": false` still shows a value (exposure 0.7, say), but that value is not in the picture: build on `value` only when the row is enabled |
| Exposure / white balance | `setParam("exposure","exposure",EV)`, `engine.autoExposure()`, `engine.autoWhiteBalance()`, `engine.whiteBalanceAsShot()`, `setParam("channelmixerrgb","temperature",K)` / `"tint"` |
| Tone regions | `engine.setParametric(region 0 highlights…3 shadows, -100..100)` |
| Point curve | `engine.setCurve(channel, xs, ys, type)` (0..1 lists), `engine.setCurveLinked(bool)` |
| Lab colour | `engine.setLabColour(key, -100..100)`, keys `separation greens magentas blues yellows tintA tintB` |
| Colour mixer | `engine.setZone(band 0-7, channel 0 lum/1 sat/2 hue, -100..100)` |
| Colour wheels | Grading's wheels: `engine.setPrimaryGrade(zone, hue, chroma, level, false)` with zone `lift` (level -0.5..0.5, 0 neutral, chroma ≤0.25), `gamma` (0.25..4, 1 neutral), `gain` (0..4, 1 neutral), `offset` (-1..1, 0 neutral, chroma ≤0.25). Colour's balance wheels: `engine.setColourBalance(zone, hue, chroma, luminance, false)` with zone `shadows` `midtones` `highlights` `global`, luminance -1..1. Hue in degrees |
| Noise | `engine.applyValues` with what the Detail panel sends — see "Noise" below |
| Crop / straighten | `engine.setCrop(cx, cy, cw, ch, ratioN, ratioD)` (left, top, right, bottom as fractions of the picture), `engine.straightenAlong(x0,y0,x1,y1)` (a line that should be level), `engine.correctGeometry(operation, cropPolicy, guides)` — see `ops --filter Geometry` |
| Masked (local) edits | `engine.addLocal(1 radial / 2 gradient)`, then `engine.setShape(index, cx, cy, a, b, rotation, opacity)` and `engine.setLocalParam(priority, field, value)` — `inspect --photo` lists `locals` with their `priority` |
| Presets | `catalog.presets()` to list; select one with `catalog.applyPreset(name)` or `engine.applyPresetValues(<its values>)` (replaces the previous preset; `applyValues` only patches settings). `catalog.saveCurrentPreset(name, groups)` uses the keys from `engine.settingsGroups()`, e.g. `film`, not the display label `Film` |
| Camera profiles | `inspect --photo` → `develop.cameraProfile` lists the camera, compatible `profiles` and included `cameraLook`. `engine.applyCameraLook()` applies the included look; `engine.selectCameraProfile(key)` selects a returned DCP key (empty restores automatic colour); `engine.importCameraProfile(path)` imports the user's DCP. `catalog.setCameraProfilesFolder(path)` adds a search folder |
| Creative profiles | `develop.creativeProfiles` lists keys for `engine.selectCreativeProfile(key)`. `engine.importCreativeProfile(path)` imports CUBE/supported enhanced XMP. Amount: `engine.setParam("omarawprofile","amount",value)` |
| AI masks / removal | `ops --filter ai.` and `inspect` → `ai` report the optional runtime. Use the single-run sequence below; `ai.useGpu=false` uses CPU |
| Copy an edit | `engine.copySettings(groups)` then `engine.pasteSettings()` on another photo, or `engine.applyValuesTo(items, values)` |
| Snapshots / variants | `catalog.saveSnapshot(id, name)`, `catalog.createVariant(id)` |
| Organise | `catalog.setTitle(text, id)`, `setCaption(text, id)`, `addKeyword(k)` / `removeKeyword(k)` (on the selection), `createAlbum(name, parentId 0)` → id, `addSelectionToAlbum(albumId)`, `select(id)` |
| Export | `engine.exportJpeg(path, maxEdge, quality)` for one look; the `export` verb for a batch |

**Noise**, matching the Detail panel's *Everything* at amount A (0–100, 50 =
what the camera profile calls for) and keep-detail K (0–100): pass
`[{"op":"denoiseprofile","field":"mode","value":3}, {"op":"denoiseprofile","field":"strength","value":A/50}, {"op":"denoiseprofile","field":"overshooting","value":1}, {"op":"denoiseprofile","field":"central_pixel_weight","value":0.1+K*0.029}]`
as the single argument of `engine.applyValues`. *Colour only*: mode 4,
`wavelet_color_mode` 1, strength 1, `y[4][0..6]` 0 and `y[5][0..6]` A/100.

**Local AI** drafts live for one `apply` process. Keep `ai.start("mask")`,
`ai.point(x,y)` or `ai.selectBox(x0,y0,x1,y1)`, and `ai.acceptMask()` in the
same operations list, with a named photo. Read `engine.locals` afterwards for
the accepted mask's priority. Coordinates are 0–1 in the edited photo; a
third `true` argument to `ai.point` excludes a point.

For removal, use `ai.start("remove")`, selection, `ai.remove()`, then
`ai.apply()` in one run. This automatically names a 16-bit TIFF result, stacks
it with the source and opens it; `wrote` names it. `ai.saveCopy(path)` provides an
explicit filename. The current look is baked into removal results. Read
`catalog.aiVersions` for original/intermediate/latest IDs. With a named photo,
`catalog.openAiOriginal()`, `catalog.openLatestAiVersion()` or
`catalog.openAiVersion(id)` switches versions, preserving independent edits.
Later operations still address their named `photo`; explicitly address the result
to continue editing it. Version links persist in the catalogue. `ai.brushStroke` takes a
list of `[x,y]` points plus an optional subtraction boolean. Read AI state
from each result row or properties such as `ai.hasSelection`. Calls wait for
processing and report worker failures. Editing the photo cancels a draft.
`ai.install()` explicitly downloads tools/models if needed; it is not automatic.
**AI denoise** uses its own optional runtime: `denoise.installed`,
`denoise.install()`. Set `denoise.strength` (0–1, default 0.6),
`denoise.backend` (`auto` qualifies Vulkan against CPU; `cpu` forces CPU),
then `denoise.apply()` with a named Bayer or X-Trans RAW. It chooses an unused
`-denoised.dng` name beside the original. `denoise.saveCopy("/absolute/unused-name.dng")`
chooses an explicit path instead. Sensor/model selection is automatic.
The full-size linear DNG is added to the catalogue, stacked with its original,
and opened. Current adjustments are copied as editable settings; sensor-only
processing remains with the original. No develop settings are baked into pixels. Identify the new catalogue photo before editing/exporting it; the CLI's
named `photo` still refers to the original. Read `wrote` for the new path. Per-photo sensor/backend diagnostics reset on switching to the copy.
`denoise.preview()` generates a detail comparison with current global colour edits for the GUI; its image
provider URLs live only in that process. `previewX`/`previewY` are 0–1 sensor
coordinates; `isoOverride=0` uses capture metadata for Bayer only. X-Trans
uses Restormer without ISO conditioning. Read `detectedSensor` and `actualModel`
after an operation; both reset when the photo changes. Restormer CPU saves
can take hours (CLI timeout 12 hours). Linear RAW and DNGs
with unsupported calibration opcodes are refused. No photos are uploaded.

## Automatic tags, Image Match and cameras

- Import with `--auto-tag` to scan the 80 supported subjects offline. For existing
  photos use `catalog.autoTagEnabled=true`, an explicit `catalog.select(id)` or
  `selectAll`, then `catalog.scanSelectionAutoTags()`. Scans wait for completion
  and report missing photos/worker failures. Off preserves tags; use
  `catalog.autoTagPaused` for pause/resume. Read `catalog.autoTagOptions`,
  `autoTagGroups`, `autoTagCollections` to discover keys. Correct a named photo
  with `catalog.setCurrentAutoTag(tag,present)`; choose collections with
  `catalog.setAutoTagCollectionEnabled(key,enabled)`. The prefix is `catalog.`,
  not the QML object's `backend.`. Existing culling restrictions still apply.
- Image Match uses `match.setReference(path)` or `match.useCurrent()` on a named
  edited reference, `match.options`, `match.preview()` on the target, then
  `match.apply()`, all in one `apply` run. Read the returned `state.report` for
  confidence/warnings. `match.savePreview(newPath,"before"|"after"|"reference")`
  writes an sRGB PNG for visual review and refuses overwrites. Drafts are
  process-local: recreate them after reviewing an unapplied preview in another
  run. Batch: `match.applySelection([{path,variant},…])`; results identify failures,
  and `match.undoBatch()` works in that same run. Use `ops --filter match.`.
- Cameras: `capture.refresh()` explicitly discovers devices. Read `capture.state`
  for indices, capabilities and controls, set `capture.session` to a map containing
  folder/name/nameTemplate/deleteFromCamera, then `capture.connectCamera(index)`
  and `capture.capture()` in one run. Captures are saved and catalogued; returned
  `files` includes successful files even if a companion transfer fails. Deletion
  from the camera defaults to false. `capture.setControl(name,value)` uses the
  camera's reported choices. `capture.preview(newPath)` writes a PNG live frame;
  `capture.listen(seconds)` listens for body shutter presses for 1–3600 seconds.
  Connection alone does not listen; inspection/dry-run do not probe hardware.
  Camera sessions end with the process. Use `ops --filter capture.` and the full
  CLI reference for worked examples. Do not treat partial-transfer errors as if
  no files were saved.

## Things that will bite

- **Refused on purpose:** `setRating`, `setFlag`, `setLabel`, `toggleFlag`,
  `toggleLabel`. Culling belongs to the person; read marks with `list` or
  `inspect`, never set them. Window-only operations (reveal, shortcuts,
  viewer) are refused too.
- **Files are real.** `catalog.moveSelectionTo`, `renamePhoto`, `relinkFolder`,
  `moveSelectionToTrash`, `removeSelectionFromCatalog` act on the originals
  exactly as the Library does. Use them only for the file operation the user
  requested, and inspect the selection first. Clarify ambiguous destinations
  or removal scope before changing files.
- **A wrong name or value can be ignored without an error.** Several engine
  calls quietly do nothing with a zone, key or channel they do not know (a
  `setPrimaryGrade` zone of `midtones`, for one), and the run still reports
  `ok`. After any edit that matters, `inspect --photo` and check the value
  actually moved — `historyEnd` going up is the quick sign.
- **"No id" means the selection.** Catalog operations that take no id, or an
  id of 0, act on the selected photos: name one with `"photo"`, or choose
  them with `catalog.select…` first — `catalog.selectAll` makes the next one
  act on every photo in the catalog. With neither, the operation is refused.
- **A print stock replaces the tone mapper.** While Film ▸ Print stock is on,
  the Light section's tone-mapper sliders (contrast, skew…) keep their values
  but stay off; turn the print off to have them back.
- **Values are engine units**, not the panel's figures, for most `setParam`
  fields: exposure in EV, most sliders -1..1. `inspect --photo ID --full`
  shows each control's `min`, `max`, `def` and `value`.
- **A new photo gets the automatic first edit** (capture sharpening, colour
  noise out, lens) the first time it is opened, from `apply` as from Develop;
  the answer says so. Do not undo it unless asked.
- **The `export` verb never overwrites**: a taken name gets `-2`.
  `engine.exportJpeg` is the exception, for looks: it replaces the file at its
  path (the result row's `wrote` says which), with no location or history in it.
- **Big `inspect` output**: without `--full` only moved controls are listed;
  pipe through `jq` rather than reading it whole.

## Installation

The Arch/Omarchy package includes this skill and links it for the installing
user. Start a new agent session after installation. `omaraw skill` reports its
location and links; `omaraw skill --link` repairs missing links or installs it
for another user. Links point to the packaged copy, so app updates update the
skill. Existing custom skills are preserved; do not use `--force` unless the
user requests replacing one.
