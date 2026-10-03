# Offline originals

Select photos in Library, then choose **File → Offline Originals → Keep
Selection Available Offline**. OmaRAW makes verified, full-quality working
copies on the machine. A RAW copy takes as much space as its original.
Variants share one copy of their source file.

Wait for the job to finish before disconnecting the original drive. Progress
appears in the status line; **Cancel Offline Copy Job** stops remaining work
and retains completed copies. The Library inspector shows the saved copy's
size and whether the original is disconnected. Export checks recognize
available copies.

Develop edits, masks, variants, original comparisons, detail views and
exports use the same image identities and histories while offline. All
source pixels remain available, so export dimensions and formats are
unchanged. Export's **Copy original** option can use the verified working
copy and keeps the original filename. Edits already belong to the catalog:
reconnecting the drive does not require transferring an edit history.

Use **Remove Offline Copies of Selection** to reclaim space. Removal affects
all variants of each selected source. OmaRAW requires a readable original
with the saved SHA-256 before removing a copy. A disconnected, changed or
damaged original is insufficient. A symlink back into the working-copy
store is also insufficient. Originals are never deleted by these commands.
If the working copy is damaged but the matching original is available,
**Keep Selection Available Offline** rebuilds it.

## Storage and recovery

Copies live in `engine-library.db.offline/` beside the engine database in
the catalog's data directory. This is persistent application data, separate
from the preview cache. OmaRAW does not evict these files automatically.
Its `index.json` maps original paths to copy filenames, sizes and SHA-256s.
Renaming or relinking through OmaRAW updates that mapping without copying
the RAW again.

Copies are streamed into temporary files, flushed, checked by reading them
back, then published under unique names. The index is replaced atomically.
Cancellation and transfer failures retain the original and any earlier
copy. A failed index write can leave a complete unreferenced file in the
store; it is retained for recovery instead of being silently discarded.
An unreadable or unsupported index is preserved and disables mutations.

The engine verifies a working copy before its first use after a restart.
It rechecks after file identity, size or nanosecond modification/change
timestamps differ. A failed check prevents processing from that copy,
including exporting a frame already held in memory. Repeated processing
of an unchanged verified copy avoids rehashing every pipeline stage.

Catalog backup bundles contain the edits and configuration, but **do not
include these full-size working copies or original photos**. Back up photos
separately. To preserve offline availability while moving a catalog, also
copy its complete `engine-library.db.offline/` directory.

These are full-quality offline originals. [Smart Previews](SMART-PREVIEWS.md)
provide a separate, smaller source for offline editing. Full copies take
precedence over Smart Previews and continue to support full-quality export.
