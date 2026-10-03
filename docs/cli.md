# Driving OmaRAW from the command line

A verb runs the work headless, answers with one JSON object on stdout, and
exits. Without a verb the application opens a window as usual.

```
omaraw --catalog <file> [--new-catalog] import <folder> [--mode add|copy|move|verify] [--dest <folder>]
omaraw --catalog <file> list   [filters]
omaraw --catalog <file> export <folder> [filters] [--format jpeg] [--bits 8|16] [--quality 90] [--max-edge 2048] [--suffix -web] [--location] [--history]
omaraw --catalog <file> ops     [--filter <word>]
omaraw --catalog <file> inspect [--photo <id|path>] [--full]
omaraw --catalog <file> apply   [ops.json|-] [--photo <id|path>] [--dry-run] [--keep-going]
omaraw skill [--link] [--force]
```

`import`, `list` and `export` are the quick path for a shoot. `ops`, `inspect`
and `apply` reach everything else: every Develop control, presets, snapshots,
variants, albums, keywords, titles and captions, the export queue — the same
operations the application's own interface calls.

Filters are the Library's own, so `list` and `export` see exactly what the
browser would show under them:

```
--rating N        at least N stars, 0 to 5
--flag pick|reject|none   none = neither picked nor rejected
--label <colour>
--text <words>
```

A verb is what makes a run headless. Anything else in that position is a file
or folder to open, because the desktop entry is `omaraw %f` — so a mistyped
verb opens a window rather than reporting itself. Pass `--headless` to force
the verb reading and get the refusal in JSON:

```sh
omaraw --headless --catalog "$CAT" develop     # {"ok": false, "error": "develop is not a verb…"}
```

Exit codes: `0` the work was done, `1` the work failed, `2` the command was
not understood (a mistyped option included). `ok` in the JSON says the same
thing, and `error` carries the reason when it is false.

## The loop that works

1. **`import`** the shoot. `--mode add` (the default) catalogues the files
   where they are; `copy`, `move` and `verify` need `--dest`, which is made if
   it is not there yet, and `verify` reads each copy back and compares
   checksums before trusting it. The window's other import choices (skipping
   duplicates, metadata to add) apply; its destination, second copy and eject
   never do, and a command-line import does not change them. A file that did
   not copy or verify, or a catalog that refused, makes the answer `ok: false`
   (exit 1), with `failed` and `backupFailed` counted beside `imported`.
2. **`list`** with the filters you mean to export by, and look at the count
   before rendering anything. This is the cheap step — it opens no engine.
   (`import` does: edits carried in the photos' sidecars are applied as they
   come in.)
3. **`export`** with the same filters.

`export` writes the same metadata as the window's export: camera details,
title, caption and keywords. Where the photo was taken and its Develop history
stay out unless you add `--location` or `--history`. `--format` takes what the
installed engine can write (the error lists them); `--bits 16` is for `tiff`,
`png` and `psd`.

Culling itself — ratings, flags, labels — is a human job in the Library. The
command line reads those marks; it does not make them (`apply` refuses
`setRating`, `setFlag`, `setLabel`, `toggleFlag` and `toggleLabel`).

## Worked example

```sh
CAT=~/Pictures/Jobs/rooftop/catalog.db

omaraw --catalog "$CAT" --new-catalog import /run/media/CARD/DCIM \
  --mode verify --dest ~/Pictures/Jobs/rooftop/raw | jq '{imported, skipped}'

# …cull in the application…

omaraw --catalog "$CAT" list --rating 3 | jq '.count'
omaraw --catalog "$CAT" export ~/Pictures/Jobs/rooftop/web \
  --rating 3 --max-edge 2048 --quality 88 --suffix -web | jq '{done, failed}'
```

## Everything else: ops, inspect, apply

**`ops`** lists what exists: every operation with its arguments' names and
types (`catalog.` for the catalog, `engine.` for Develop, `ai.` for local AI,
`denoise.` for RAW denoise, `match.` for Image Match, `capture.` for cameras), every setting that
can be read or set, and the table of Develop controls (`op`, `field`, label,
group). `--filter exposure` narrows all three.

Values in and out are the engine's own, with the ranges `ops` and `inspect`
report. The window shows unitless controls differently (Contrast as 0 at its
default, −100…+100; Sharpening amount as 0…100); those figures are for reading
only and are never what `apply` takes.

**`inspect`** reads the state. Without `--photo` it is the catalog: every photo
with its `id`, albums, smart albums, keywords and presets. With `--photo` (an
`id` or the file's path) it opens that photo the way Develop does and gives its
catalog record, keywords, snapshots, variants and the edit itself — the
controls moved from their default (each with its range and default), curves,
tone regions, Lab colour, colour mixer, masks, retouch spots, crop, lens and
history. `--full` gives every control and every readable setting.

**`apply`** runs a list of operations, in order, and waits for each to finish
— renders, exports and imports included — before the next:

```json
{"photo": 12, "ops": [
  {"op": "engine.setParam", "args": ["exposure", "exposure", 0.4]},
  {"op": "engine.setLabColour", "args": {"key": "separation", "value": 25}},
  {"op": "engine.maskStrength", "value": 0.6},
  {"op": "catalog.setTitle", "args": {"text": "Rosenborg in spring", "id": 12}},
  {"op": "catalog.addKeyword", "args": ["crocus"]},
  {"op": "catalog.saveSnapshot", "args": [12, "first look"]},
  {"op": "engine.exportJpeg", "args": ["/tmp/look.jpg", 1600, 90]},
  {"op": "setParam", "photo": 13, "args": ["exposure", "exposure", -0.3]}
]}
```

- Arguments are positional (`"args": [...]`) or named (`"args": {...}`) using
  the names `ops` gives. A setting takes `"value"` instead. Omit `"value"` to
  read a setting in the same run, including read-only state such as
  `engine.cameraProfileState` or `ai.hasSelection`; its value is in `result`.
- The prefix can be left off when only one side has the name.
- `"photo"` at the top, on an operation, or `--photo` opens that photo first
  (for a catalog operation it only selects it, so titles and keywords work on
  photos whose files are offline). A catalog operation on the selection
  (`addKeyword`, `moveSelectionToTrash`…) needs a photo named this way, or a
  `catalog.select…` earlier in the run; otherwise it is refused, never left to
  act on whichever photo the catalog happens to list first. Saving a snapshot
  or a preset opens the photo in Develop, since it needs the edit.
  A photo imported but never opened gets the automatic first edit before the
  operations, as it would in Develop, and the answer says so.
- Every operation is checked before any runs: one that does not exist, does
  not take those arguments or is refused stops the whole run with `ok: false`
  and a list of problems, and nothing is done. `--dry-run` does only the
  checking. `--keep-going` runs what it can and reports each failure.
- Each result carries what the operation returned (a new snapshot's id, a
  list, a value), so a later step can use it. Something the engine refuses
  after the call has returned (a value it cannot take, an export that fails)
  still fails that operation.
- `engine.exportJpeg` is for a quick look: it replaces a file already at that
  path, writes camera details and keywords but not the location or the
  Develop history, and its result row says the file it `wrote`.
- The answer ends with the open photo's `historyEnd`; every Develop change is
  a history step that the application's History panel can undo. Full-photo
  `engine.resetHistory` and `engine.resetToOriginal` instead discard the previous
  edit and redo history; those resets cannot be undone.
- Operations that only mean something with a window (reveal in the file
  manager, open another window, shortcuts, the viewer's detail view) are
  refused. Moving, renaming, relinking and deleting files are not: they do
  exactly what the Library's own commands do.

```sh
CAT=~/Pictures/Jobs/rooftop/catalog.db
omaraw --catalog "$CAT" inspect | jq '.photos[] | {id, filename}'
omaraw --catalog "$CAT" ops --filter lab | jq -c '.ops[]'
omaraw --catalog "$CAT" apply look.json | jq '{ok, done, historyEnd}'
omaraw --catalog "$CAT" inspect --photo 12 | jq '.develop.params'
```

## Camera colour, creative profiles and presets

`inspect --photo ID` includes `develop.cameraProfile` with the camera,
compatible `profiles` and included `cameraLook`, plus `develop.creativeProfiles`.
Use the returned keys, rather than constructing filenames:

- `engine.applyCameraLook()` applies the included look for that camera.
- `engine.selectCameraProfile(key)` selects a compatible DCP; an empty key
  restores automatic colour. `engine.importCameraProfile(path)` manages a
  DCP supplied by the user. `catalog.setCameraProfilesFolder(path)` sets an
  additional search folder; `engine.refreshCameraProfiles()` refreshes it.
- `engine.importCreativeProfile(path)` imports a CUBE or supported enhanced
  XMP profile; `engine.selectCreativeProfile(key)` picks a listed look.
  `engine.setParam("omarawprofile", "amount", value)` changes its strength.
- `catalog.presets()` returns built-in and user presets with their `values`.
  Select one by name with `{"op":"catalog.applyPreset","args":["Punchy"]}`,
  or use `engine.applyPresetValues(values)` / `engine.applyPresetValuesTo(items, values)`.
  These replace the previous preset, including its film pipeline, and retain
  unrelated edits. `engine.applyValues` remains an ordinary parameter patch.
  Repeated selection does not accumulate effects; Undo restores the prior look.
  `catalog.saveCurrentPreset(name, groups, category, tags)` saves selected
  groups using the keys from `engine.settingsGroups()` (for example `film`,
  not its display label `Film`); `catalog.saveCurrentModulePreset(name, operation)`
  captures one tool.

Profile import/selection failures return `ok: false` and exit 1, and stop the
remaining operations unless `--keep-going` is set. DNG decoding and sidecar
import use the same pipeline as the GUI; their format/profile limitations also
apply to the CLI.

Film looks use the normal `engine.setParam`, `engine.applyValues` and
`engine.setModuleEnabled` operations on `omarawprint`. Current cinema stock IDs
23 and 15–21 automatically use the digital Cineon workflow; no global colour
setting is needed. Their display rendering is built in, so `paper` has no effect
on those choices. Still-film and imported camera-profile handling is unchanged.
Set `strength` to 0 or disable `omarawprint` to restore the normal rendering.

## Local AI masking and removal

`ops --filter ai.` lists the AI methods and settings. `inspect` reports whether
the included runtime is available. Models and runtime libraries come with the
package; no download or setup operation is needed. Reinstall the package if
`ai.installed` is false. `ai.useGpu` defaults to CPU and accepts a boolean setting.

An AI draft lasts for one `apply` process. Start, select and accept/save in the
same operations list. Coordinates run from 0 to 1 across the displayed photo,
including its current crop and orientation. For a local mask:

```json
{"photo": 12, "ops": [
  {"op": "ai.useGpu", "value": false},
  {"op": "ai.start", "args": ["mask"]},
  {"op": "ai.point", "args": [0.5, 0.5]},
  {"op": "ai.hasSelection"},
  {"op": "ai.acceptMask"},
  {"op": "engine.locals"}
]}
```

`ai.point(x, y, true)` excludes a point; `ai.selectBox(x0, y0, x1, y1)` selects
within a box. Accepted masks are normal editable local adjustments and persist
in history. The returned `engine.locals` lists their priorities for subsequent
`engine.setLocalParam` calls.

For removal, start with `"remove"`, select the object, call `ai.remove()`, then
`ai.apply()` to add an editable repair to the current photo. The photo ID/path
stay unchanged; existing controls remain editable. Repeat the selection,
`ai.remove()` and `ai.apply()` for another object. Each Apply is a separate
Undo step. Native `omarawrepair` parameters preserve the repair state in
snapshots and variants; its files belong to the catalogue's `repairs` folder.

`ai.saveCopy("/absolute/new-name.dng")` optionally exports the proposed repair
and current rendering to a separate 32-bit linear DNG. This explicitly rendered
copy has its look baked in; the original's editable state stays intact.
Existing files/sidecars are refused. `ai.brushStroke` takes a list of `[x, y]`
points and an optional subtraction boolean in both `"mask"` and `"remove"`
sessions, including before any AI point or box prompt. In masking it guides
AI object selection by default; set `ai.objectBrush` to `false` after starting
the session to paint the selection directly. Removal always paints directly.
Set `ai.brushSize` for either tool and `ai.removalMargin` for removal coverage.
`ai.refineEdges` adjusts an existing mask draft to nearby colour boundaries;
it requires a `"mask"` session and is reversible with `ai.undoPoint`.
`ai.undoPoint`, `ai.clear`, `ai.invert`
and `ai.cancel` are also available.

`catalog.aiVersions` lists the selected photo's original and AI results in creation
order, including `id`, `path`, `operation`, `sourceId`, `label` and `current`.
`catalog.openAiOriginal()`, `catalog.openLatestAiVersion()` and
`catalog.openAiVersion(id)` switch within that family without resetting edits.
These commands require a named photo; unavailable files return a useful failure.
As with denoise, subsequent operations still address their explicitly named
`photo`: use `wrote` or the version list to address the result in another request.
Version links are catalogue data, separate from ordinary stacks.

Each AI operation waits for processing, metadata preparation and saving before
the next begins. Results include AI `state`; successful saving includes
`wrote`. Missing tools, invalid draft state and worker errors fail the operation.
Editing the photo invalidates an unfinished selection/removal draft, as in the GUI.

AI denoise includes both sensor models and uses its own namespace
(`ops --filter denoise.`). No download or setup is needed. It automatically detects Bayer or X-Trans from the original RAW before edits:

```json
{"photo": 12, "ops": [
  {"op": "denoise.strength", "value": 0.6},
  {"op": "denoise.backend", "value": "auto"},
  {"op": "denoise.apply"}
]}
```

The DNG is a full-resolution, float32 linear camera-RGB copy with camera white
balance and calibration retained. `denoise.apply` chooses an unused name beside
the original, imports/stacks the copy and opens it with the current editable
adjustments preserved. `denoise.saveCopy` takes an explicit unused `.dng` path
and otherwise follows the same workflow. Existing files and sidecars are never
overwritten; sensor-only settings are not transferred to the linear DNG. The
CLI's named `photo` still identifies the original: inspect/list the new path
to find the copy's id before editing it. `wrote` identifies the saved path;
each result includes `state`. Selection of the new image resets per-photo sensor
and backend diagnostics. Saving waits up to
12 hours. `auto` checks Vulkan correctness and speed against CPU; `cpu`
forces CPU. `preview`, `previewX`, `previewY`, `isoOverride`, `cancel`,
`status`, `progress` and `canCancel` are also exposed. Preview image-provider URLs are
process-local. Both previews include current global colour/exposure settings;
crop, masks and other detail effects are excluded. Display adjustments are not
baked into the saved DNG. `isoOverride=0` uses the photo's ISO for Bayer. X-Trans does not need ISO.
Read `detectedSensor` (`bayer`/`xtrans`) and `actualModel` after preview/save;
they are empty until an operation detects the current photo. No model argument
is needed. Large Restormer CPU jobs may take hours; CLI waits up to 12 hours
and the worker reports tile progress. See [AI denoise](AI-DENOISE.md)
for supported RAW types and output limits.

False colour is a viewer diagnostic. Set `engine.scopeMode` to 4 and read
`engine.falseColour` and `engine.falseColourBands` in `apply`;
exports remain normal photographs. The on-screen guide needs a window.

## Driving it from an agent

The package includes `skills/omaraw/SKILL.md` at
`/usr/share/omaraw/skills/omaraw/`. Installation links it for the installing
user, following Omarchy's layout. Start a new agent session to discover it.
When installing without an identifiable user, or setting up a second account,
run the link command from that account:

```sh
$ omaraw skill          # where it is, and which agents have it
$ omaraw skill --link   # put it where they look
```

Omarchy keeps one skill directory per agent and links its own skills into each,
so `--link` does the same: `~/.agents/skills`, `~/.claude/skills`, and
`~/.codex/skills`, `~/.pi/agent/skills`, `~/.hermes/skills` and each Hermes
profile when those agents are installed. The shared `.agents` location and
Claude location are always linked; other agent locations are used when present.

```sh
$ omaraw skill --link
{ "ok": true, "skill": "/usr/share/omaraw/skills/omaraw/SKILL.md", "linked": 5,
  "places": [ { "place": "/home/you/.agents/skills/omaraw", "linked": true }, … ],
  "advice": "The agents on this computer will find it in a new session." }
```

Asking twice is not an error. Something already sitting at one of those names is
left alone, and named in the answer, unless you say `--force`. `skill` needs no
catalog and opens none.

## What it refuses to do

- **Invent a catalog.** A `--catalog` that is not there is an error, so a typo
  does not quietly become an empty catalog. `--new-catalog` says you mean it.
- **Ask a question.** There is no chooser and no dialog; a catalog that cannot
  be opened is reported and the process exits 2.
- **Adopt someone else's queue.** If the export queue already has rows from the
  application, `export` stops and says so rather than rendering work you did
  not ask for here. Clear it in the application first.
- **Export without an engine.** `list` needs no engine (an `import` without one
  catalogues the photos but leaves any edit in their sidecars for later); `export`
  says so plainly when there is none rather than writing nothing and claiming
  success.

## Notes

- **One OmaRAW at a time, whatever the catalog.** The engine's configuration
  and its `data.db` live in one place per user unless a catalog carries its
  own `engine-config/`, and they cannot be shared, so a run refuses while any
  window is open — even on a different catalog:

  ```
  {"ok": false, "error": "Its Develop data is already open in another OmaRAW window."}
  ```

  Anything driving this should expect that refusal and wait, rather than treat
  it as a failed export. Close the application before an unattended run.
- The engine prints its own progress. Standard output is kept for the answer
  and everything else goes to stderr, so `| jq` works without filtering.
- Output names never collide: a taken name gets a counter, and each row's
  actual `out` path is in the answer.
- `import` waits up to six hours and `export` up to twelve; a run that passes
  those reports that it did not finish rather than hanging. In `apply`, each
  operation has ten minutes to settle (25 minutes for explicit AI installation).

### Offline import tags

Add `--auto-tag` to `import` to scan the 80 supported subjects and populate the
enabled automatic collections. This flag is explicit and does not change the saved
GUI import choice. Import waits for tagging; its result reports `autoTagFailed`.
For existing photos, select them and use `catalog.scanSelectionAutoTags` through
`apply`. `catalog.setCurrentAutoTag(tag, present)` corrects one subject on the
current photo, and `catalog.resetCurrentAutoTagCorrections` restores model labels.
`catalog.setAutoTagCollectionEnabled(key, enabled)` shows or hides a collection
without deleting its tags. Group keys use `group:animals`, `group:transport`,
`group:sports` and `group:food`; individual keys include `people`, `birds` and
`bicycles`. The CLI flag temporarily enables scanning and restores both the
previous enabled and paused states after import.

For existing photographs, enable scanning and choose the selection explicitly:

```json
{"ops":[
  {"op":"catalog.autoTagEnabled","value":true},
  {"op":"catalog.selectAll"},
  {"op":"catalog.scanSelectionAutoTags"},
  {"op":"catalog.autoTagPending"}
]}
```

`selectAll` means every photo currently shown: use `catalog.select(id)` or a
named `photo` to limit the scan. Scans wait for persisted results; disabled scans,
missing originals and worker failures return `ok: false` and exit 1. Active,
unpaused tagging also settles before an `apply` operation returns. Tag commands
have a six-hour limit; a timeout pauses remaining work rather than losing it.
`catalog.autoTagPaused` controls pause/resume; turning `autoTagEnabled` off stops
scans and keeps tags. Read `autoTagOptions`, `autoTagGroups` and
`autoTagCollections` through `apply` to discover supported keys and choices.
Corrections require an explicit photo/selection and work while scanning is off.

### Image Match

`ops --filter match.` describes the full reference/preview/apply service.
Reference images and draft comparisons live for one `apply` process: keep the
steps together. Commands wait for analysis, previews and history writes and
report failures. Applied matches remain editable in Develop.

```json
{"ops":[
  {"op":"match.setReference","args":["/photos/reference.jpg"]},
  {"op":"match.options","value":{"mode":"creative","protectSkin":true,"grain":false}},
  {"op":"match.preview","photo":12},
  {"op":"match.savePreview","args":["/tmp/match-before.png","before"]},
  {"op":"match.savePreview","args":["/tmp/match-after.png","after"]},
  {"op":"match.apply"}
]}
```

Omit the final apply to review a draft without changing the photo; another run
must recreate the draft before applying it. `savePreview` writes an sRGB PNG to a
new path and refuses overwrites. It also accepts `reference`, `referenceDetail`,
`beforeDetail`, `afterDetail`; call `match.detailAt(x,y)` (0–1 coordinates) first
for detail images. `match.report` and each match operation's `state` report
confidence, warnings and fitted parameters. `match.options` accepts the same
settings as the panel; omitted fields take defaults. `match.useCurrent` with a
named photo captures an edited reference instead of an external image.

For a batch, open a target using `photo`, then call `match.applySelection` with
one list argument of `{ "path": "/photos/target.nef", "variant": 0 }` items.
Read `batchResults` for individual outcomes; any failure makes the command fail,
while successful changes remain recorded. `match.undoBatch` reverses that batch
in the same run. Normal history Undo remains available after the process exits.

### Tethered capture

`ops --filter capture.` lists camera commands. `capture.state` reports devices,
connection, capabilities, controls and status; `capture.session` reads/writes
folder, name, nameTemplate and deleteFromCamera. Inspection and dry-run do not
probe USB or operate a camera. `capture.refresh` explicitly discovers cameras;
an empty list is a successful discovery with no connected devices.

Connection and camera state live for one `apply` run. Connect, configure and
shoot together; disconnect happens when the process exits:

```json
{"ops":[
  {"op":"capture.refresh"},
  {"op":"capture.session","value":{"folder":"/photos/session","name":"Portraits","deleteFromCamera":false}},
  {"op":"capture.connectCamera","args":[0]},
  {"op":"capture.reloadControls"},
  {"op":"capture.capture"},
  {"op":"capture.disconnectCamera"}
]}
```

Device indices come from the current discovery result. Read `capture.state`
to inspect returned controls; set one with `capture.setControl(name,value)`
using the camera's reported choices. `capture.preview("/tmp/new-preview.png")`
saves a live-view frame without importing it. `capture.listen(seconds)` listens
for body shutter presses for 1–3600 seconds, imports delivered photos, then stops.
Connecting alone does not listen or import. Capture naming follows the GUI and
avoids overwrites; deletion from the camera defaults to false for each run.

Every completed shot is imported into this catalogue using the normal capture
workflow, including automatic tagging when enabled. Results include saved
`files` and updated camera `state`; a partial transfer or catalogue failure is
reported as a failure while retaining successfully saved files. Unsupported or
unconnected cameras and rejected settings fail explicitly. Individual camera
requests wait up to two minutes; a blocked camera driver can take longer to
release its worker during shutdown. Reconnect in a new run after a timeout.

Headless commands do not replace the desktop session’s remembered photo, collection
or filters. Opening the desktop again resumes its last selection.
