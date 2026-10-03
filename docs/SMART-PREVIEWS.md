# Smart Previews

Smart Previews keep a smaller editable source beside your catalog so you can
work with the original drive disconnected. They contain no baked-in Develop
adjustments. Your edits, masks, variants and snapshots stay in the same
catalog and apply to the original when it is available again.

## Build and use

1. Select photos in Library.
2. Choose **Library → Smart Previews → Build Smart Previews for Selection**.
3. Wait for the status line to report completion before disconnecting the drive.

Alternatively, enable **Build Smart Previews after import** in the Import
browser. The choice is remembered. Previews are built for newly imported
photos; files skipped as duplicates can be selected and built through Library.
An import can queue its preview work behind another offline job.

Library's inspector shows the preview dimensions and disk space. Develop
shows **Editing Smart Preview** when the original and full offline copy are
unavailable. OmaRAW checks the current photo's source availability every two
seconds while idle and switches back automatically when the drive returns.

The source order is: readable original, verified full offline copy, verified
Smart Preview. Variants share one preview. Crop, colour, masks and history
remain editable; 100% shows the preview's available pixels. Fine detail,
noise and RAW reconstruction can look different at reduced resolution.
Sensor-clipping inspection requires the full source.

**Exports and full-quality prints require the original or a full offline
copy.** OmaRAW reports a reconnect message rather than exporting from the
reduced source. Thumbnail contact sheets remain available as before.

## Storage and removal

Previews are at most **2560 pixels on the long edge**. RAW previews preserve
a reduced sensor mosaic and camera metadata. RGB sources retain reduced,
unedited RGB pixels and their embedded colour profile. Compression is
lossless after downsampling; the file size depends on the source. Large RAW
files usually shrink substantially, while an already compressed JPEG or a
simple PNG can be smaller than its editable preview.

The files live in **engine-library.db.smart-previews/** beside the engine
database. They are persistent, separate from the disposable thumbnail cache,
and are not automatically evicted. A preview is shared by every variant of
its original. Renaming or relinking through OmaRAW updates its reference.

Choose **Library → Smart Previews → Discard Smart Previews of Selection** to
reclaim space. A matching original or verified full offline copy must be
available. Discarding does not delete originals or edits. Cancel stops the
remaining job and retains completed previews.

Catalog backup bundles currently contain edits/configuration/imported masks,
not Smart Preview files. To retain offline availability while moving or
restoring a catalog, also copy its complete smart-previews directory. Always
back up originals separately: reduced previews cannot replace them.

## Limits and recovery

Interrupted preview creation leaves the originals and completed previews intact.
Missing or damaged previews can be rebuilt when the matching original or full
offline copy is connected. If an original's contents have changed, OmaRAW reports
that instead of silently replacing its preview.

Not every RAW format can be reduced into a Smart Preview. Some compressed RAWs
and four-colour sensors require a full offline copy. A camera's embedded JPEG
preview can be visible even when its RAW data cannot be decoded. A full offline
copy preserves the complete file; it cannot add support for an unsupported camera.
