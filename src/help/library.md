# Library

The Library is where you look at everything you have, choose what is worth developing, and keep it organised.

The left panel has **Library** and **Browse** tabs. Library shows your catalog; Browse explores folders and drives, previews their photos in the main grid, and imports only the photos you tick. Ctrl+I goes directly to Browse. Import settings appear on the right, with no separate photo import window.

## The layout

- **Sources** (left): All Photographs, Quick Collection, Previous Import, Recently Added, Recently Edited, Recently Exported, Rejected, Offline, the folder tree, albums, smart albums, keywords and mounted devices. Every count is live.
- **Browser** (centre): the grid of cards. The toolbar above it holds search, the star, flag and label filters, the funnel for more filters, saved filters, sort order and the card size.
- **Inspector** (right): the histogram, every metadata group, keywords and the sidecar state. Switch to **Develop** to adjust a photo or apply a preset.
- **Filmstrip** (bottom): the same photos as a strip, with the current one marked. Drag the scrollbar beneath the previews to browse left or right; the arrow buttons select the previous or next photo.

## Views

- **Grid** (G): cards with thumbnail, stars, flag, label dot and badges. A badge shows the format, an edited mark, what the edit holds (crop, local adjustment, retouch), a pin for a place and a warning triangle for a sidecar changed outside OmaRAW.
- **Loupe** (E or double-click): one photo large. Roll the mouse wheel over the picture to zoom around the pointer; drag to pan. Wheel zoom also works on individual pictures in Compare and Survey.
- **Compare** (C): the current photo beside the other selected one, or the next.
- **Survey** (N): every selected photo at once.
- **Detail list** (L): the browser as a table. Click a column header to sort by it.

## Selecting photos

Click a photo to select it. Hold **Ctrl** while clicking to add or remove
individual photos. Click the first photo, then **Shift-click** the last to
select the range between them. **Shift+Left** and **Shift+Right** extend the
selection one photo at a time; in the grid, **Shift+Up** and **Shift+Down** extend
it a row. These controls work in Grid, Detail list and the Filmstrip. **Ctrl+A** selects every photo in the current view;
**Ctrl+D** clears the selection. Selected photos have an accent-coloured border.

## Albums

Select one or more photos, right-click a selected photo and choose **Add to Album**,
then choose an existing album. The same command is under **Library ▸ Albums**.
Nested albums include their parent names, for example **Travel / Italy**.
**New Album from Selection…** creates an album and adds the selected photos.
You can also right-click an album in Sources and choose **Add Selection**.
Smart albums collect matching photos automatically through their rules.

## Transferring adjustments

Right-click an edited photo and choose **Copy Settings…**, tick the adjustment
groups to copy, then click **Copy**. Select the destination photos, right-click
one of them and choose **Paste Settings**.

For a single batch, select the source and destination photos together,
right-click the source and choose **Sync Settings…**. The dialog names the
source and the other selected photos. Choose the adjustment groups and click
**Sync**. These commands work directly in Library, including Detail list.
Groups you do not choose keep their settings on each destination photo. Check
copied masks and crop positions when the photos have different compositions.
These commands act on catalog photos; return from **Browse** to **Library** first.

## Finding photos

Search covers file name, camera, lens, path, keywords, title and caption. The star, flag and label buttons filter; the funnel adds type (RAW or not), edited state, place and camera. Sort by capture time, import time, file name, rating or edit time, and by Custom Order inside an album. **Saved filters** (the bookmark) keep a whole set under a name.

## The status bar

Real totals for what is shown and selected, the current catalog, the engine's state and the colour configuration in use.

Photos with embedded GPS show latitude and longitude in the inspector. **Open location in browser** opens OpenStreetMap and shares those coordinates with the site only when you click it. Output’s **Location (GPS)** switch controls whether exported files include location data.

## Automatic tags

The inspector's **Automatic tagging** switch enables or disables offline subject
scanning. It also controls tagging on new folder imports. Off stops the worker
and keeps existing tags, corrections and queued scans. Turn it back on to resume.
Use **Scan selected** for existing photos while enabled, or Pause/Resume to stop
work temporarily without changing the import switch.

Detected tags appear as removable labels. Click a label's cross to exclude it, or
use **Add / correct…** to search 80 supported subjects and tick missed ones.
Corrections survive rescanning. **Use detections** restores the last scan's labels.
Manual corrections work even while scanning is off. Virtual copies share their
original's tags; your keywords, edits, originals and exported metadata are unchanged.

**Settings…** chooses automatic collections for this catalog. People, Animals,
Transport, Sports equipment and Food are selected initially and appear only once
matches exist. Other objects and individual collections can be enabled separately.
Unticking hides a collection and preserves its name and tags. Custom smart albums
can use individual subjects or whole groups in the **Auto tag** rule.

Detection is a sorting aid: small, hidden or unusual subjects may be missed, and
pictures or toys can be mistaken for real objects. Scenery such as landscapes,
beaches and sunsets is outside this model's supported subjects.

## Catalog maintenance and browsing speed

Open **File ▸ Catalog Maintenance ▸ Overview…** to see the active catalog's
location and size, the latest backup folder, and browsing-cache usage.
**Check Catalog Integrity…** runs in the background and leaves a result in this
window. **Show report** includes the check time, locations and individual results;
**Copy report** copies it for diagnosis. The check covers catalog structure and
references plus the Develop databases' structure. It does not verify original
photos or how edits look, and it makes no repairs. If errors are reported, keep
the current catalog and restore a verified backup into a separate folder.

**Back up now** leaves a visible success or failure message, including the saved
location. Backups include catalog and Develop data; original photos and external
profiles need separate backups.

OmaRAW keeps browsing previews on disk between sessions and recent photo records
in memory. **Cache and memory settings…** opens Preferences ▸ Performance, where
you can see the cache folder, usage and size limit (4 GiB by default). Older
browsing previews are removed automatically. Clearing this cache keeps photos,
edits, edited preview masters and Smart Previews; browsing previews rebuild as
needed, so clearing it is not a routine speed-up step.
